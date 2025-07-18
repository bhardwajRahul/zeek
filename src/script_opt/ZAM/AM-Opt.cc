// See the file "COPYING" in the main distribution directory for copyright.

// Logic associated with optimization of the low-level Abstract Machine,
// i.e., code improvement that's done after the compiler has generated
// an initial, complete intermediary function body.

#include "zeek/Desc.h"
#include "zeek/Reporter.h"
#include "zeek/input.h"
#include "zeek/script_opt/Reduce.h"
#include "zeek/script_opt/ScriptOpt.h"
#include "zeek/script_opt/ZAM/Compile.h"

namespace zeek::detail {

// Tracks per function its maximum remapped interpreter frame size.  We
// can't do this when compiling individual functions since for event handlers
// and hooks it needs to be computed across all of their bodies.
//
// Note, this is now not actually needed, because we no longer use any
// interpreter frame entries other than those for the function's arguments.
// We keep the code in case that changes, for example when deciding to
// compile functions that include "return when" conditions.
std::unordered_map<const Func*, int> remapped_intrp_frame_sizes;

void finalize_functions(const std::vector<FuncInfo>& funcs) {
    // Given we've now compiled all of the function bodies, we can reset
    // the interpreter frame sizes to what's actually used.  This can be
    // a huge win for massively inlined event handlers, which otherwise
    // can have frames sized for 100s of variables, none of which (other
    // than the arguments) need TLC such as via calls to Frame::Reset().

    // Find any functions with bodies that weren't compiled and
    // make sure we don't reduce their frame size.
    std::unordered_set<const Func*> leave_alone;

    for ( auto& f : funcs )
        if ( f.Body() && f.Body()->Tag() != STMT_ZAM )
            // This function has a body that wasn't compiled,
            // don't mess with its size.
            leave_alone.insert(f.Func());

    for ( auto& f : funcs ) {
        auto func = f.Func();

        if ( leave_alone.count(func) > 0 )
            continue;

        if ( remapped_intrp_frame_sizes.count(func) == 0 )
            // No entry for this function, keep current frame size.
            continue;

        auto& ft = func->GetType();
        auto& params = ft->Params();
        func->SetFrameSize(params->NumFields());

        // Don't bother processing any future instances.
        leave_alone.insert(func);
    }
}

// The following is for activating detailed dumping for debugging
// optimizer problems.
static bool dump_intermediaries = false;

void ZAMCompiler::OptimizeInsts() {
    // Do accounting for targeted statements.
    for ( auto& i : insts1 ) {
        if ( i->target && i->target->live )
            ++(i->target->num_labels);
    }

    TallySwitchTargets(int_casesI);
    TallySwitchTargets(uint_casesI);
    TallySwitchTargets(double_casesI);
    TallySwitchTargets(str_casesI);

    for ( unsigned int i = 0; i < insts1.size(); ++i )
        if ( insts1[i]->op == OP_NOP )
            // We can always get rid of these.
            KillInst(i);

    if ( analysis_options.dump_ZAM ) {
        printf("Original ZAM code for %s:\n", func->GetName().c_str());
        DumpInsts1(nullptr);
    }

    bool something_changed;

    do {
        something_changed = false;

        while ( RemoveDeadCode() ) {
            something_changed = true;

            if ( dump_intermediaries ) {
                printf("Removed some dead code:\n");
                DumpInsts1(nullptr);
            }
        }

        while ( CollapseGoTos() ) {
            something_changed = true;

            if ( dump_intermediaries ) {
                printf("Did some collapsing:\n");
                DumpInsts1(nullptr);
            }
        }

        ComputeFrameLifetimes();

        if ( PruneUnused() ) {
            something_changed = true;

            if ( dump_intermediaries ) {
                printf("Did some pruning:\n");
                DumpInsts1(nullptr);
            }
        }
    } while ( something_changed );

    ReMapFrame();
    ReMapInterpreterFrame();
}

template<typename T>
void ZAMCompiler::TallySwitchTargets(const CaseMapsI<T>& switches) {
    for ( auto& targs : switches )
        for ( auto& targ : targs )
            ++(targ.second->num_labels);
}

bool ZAMCompiler::RemoveDeadCode() {
    if ( insts1.empty() )
        return false;

    bool did_removal = false;

    // Note, loops up to the last instruction but not including it.
    for ( unsigned int i = 0; i < insts1.size() - 1; ++i ) {
        auto& i0 = insts1[i];

        if ( ! i0->live )
            continue;

        if ( analysis_options.no_ZAM_control_flow_opt )
            continue;

        auto i1 = NextLiveInst(i0);

        // Look for degenerate branches.
        auto t = i0->target;

        if ( t == pending_inst && ! i1 ) {
            // This is a branch-to-end, and that's where we'll
            // wind up anyway.
            KillInst(i0);
            did_removal = true;
            continue;
        }

        if ( t && t->inst_num > i0->inst_num && (! i1 || t->inst_num <= i1->inst_num) ) {
            // This is effectively a branch to the next instruction.
            // We can remove it *unless* the instruction has side effects.
            // Conditionals don't, but loop-iteration-advancement
            // instructions do.
            if ( ! i0->IsLoopIterationAdvancement() ) {
                KillInst(i0);
                did_removal = true;
                continue;
            }
        }

        if ( i0->DoesNotContinue() && i1 && i1->num_labels == 0 ) {
            // i1 can't be reached - nor anything unlabeled
            // after it.
            KillInsts(i1);
            did_removal = true;
        }
    }

    return did_removal;
}

bool ZAMCompiler::CollapseGoTos() {
    if ( analysis_options.no_ZAM_control_flow_opt )
        return false;

    bool did_change = false;

    for ( auto& i0 : insts1 ) {
        auto orig_t = i0->target;

        if ( ! i0->live || ! orig_t || orig_t == pending_inst )
            continue;

        // Resolve branch chains.  We both do a version that
        // follows branches (to jump to the end of any chains),
        // and one that does (so we can do num_labels bookkeeping
        // for our initial target).
        auto first_branch = FirstLiveInst(orig_t, false);
        if ( ! first_branch )
            // We're jump-to-end, so there's no possibility of
            // a chain.
            continue;

        auto t = FirstLiveInst(orig_t, true);

        if ( ! t )
            t = pending_inst;

        if ( t != orig_t ) {
            // Update branch.
            if ( first_branch->live )
                --first_branch->num_labels;
            i0->target = t;
            ++t->num_labels;
            did_change = true;
        }
    }

    return did_change;
}

bool ZAMCompiler::PruneUnused() {
    bool did_prune = false;

    for ( unsigned int i = 0; i < insts1.size(); ++i ) {
        auto inst = insts1[i];

        if ( ! inst->live ) {
            ASSERT(inst->num_labels == 0);
            continue;
        }

        if ( i == insts1.size() - 1 && inst->op == OP_RETURN_X ) {
            // A non-value return at the very end of the body
            // doesn't actually do anything.
            did_prune = true;
            KillInst(i);
            continue;
        }

        if ( inst->IsLoad() && ! VarIsUsed(inst->v1) ) {
            did_prune = true;
            KillInst(i);
            continue;
        }

        if ( inst->IsNonLocalLoad() ) {
            // Any straight-line load of the same global/capture
            // is redundant.
            for ( unsigned int j = i + 1; j < insts1.size(); ++j ) {
                auto i1 = insts1[j];

                if ( ! i1->live )
                    continue;

                if ( i1->DoesNotContinue() )
                    // End of straight-line block.
                    break;

                if ( i1->num_labels > 0 )
                    // Inbound branch ends block.
                    break;

                if ( i1->aux && i1->aux->can_change_non_locals )
                    break;

                if ( ! i1->IsNonLocalLoad() )
                    continue;

                if ( i1->v2 == inst->v2 && i1->IsGlobalLoad() == inst->IsGlobalLoad() ) { // Same global/capture
                    did_prune = true;
                    KillInst(i1);
                }
            }
        }

        if ( ! inst->AssignsToSlot1() )
            continue;

        int slot = inst->v1;
        if ( denizen_ending.count(slot) > 0 )
            // Variable is used, keep assignment.
            continue;

        auto& id = frame_denizens[slot];
        if ( id->IsGlobal() || IsCapture(id) ) {
            // Extend the global/capture's range to the end of the
            // function.
            denizen_ending[slot] = insts1.back();
            continue;
        }

        // Assignment to a local that isn't otherwise used.
        if ( ! inst->HasSideEffects() ) {
            did_prune = true;
            // We don't use this assignment.
            KillInst(i);
            continue;
        }

        // If we get here then there's a dead assignment but we
        // can't remove the instruction entirely because it has
        // side effects.  Transform the instruction into its flavor
        // that doesn't make an assignment.
        if ( assignmentless_op.count(inst->op) == 0 )
            reporter->InternalError("inconsistency in re-flavoring instruction with side effects");

        inst->op_type = assignmentless_op_class[inst->op];
        inst->op = assignmentless_op[inst->op];

        inst->v1 = inst->v2;
        inst->v2 = inst->v3;
        inst->v3 = inst->v4;

        // While we didn't prune the instruction, we did prune the
        // assignment, so we'll want to reassess variable lifetimes.
        did_prune = true;
    }

    return did_prune;
}

void ZAMCompiler::ComputeFrameLifetimes() {
    // Start analysis from scratch, since we might do this repeatedly.
    inst_beginnings.clear();
    inst_endings.clear();

    denizen_beginning.clear();
    denizen_ending.clear();

    for ( unsigned int i = 0; i < insts1.size(); ++i ) {
        auto inst = insts1[i];
        if ( ! inst->live )
            continue;

        if ( inst->AssignsToSlot1() )
            CheckSlotAssignment(inst->v1, inst);

        // Some special-casing.
        switch ( inst->op ) {
            case OP_NEXT_TABLE_ITER_fb:
            case OP_NEXT_TABLE_ITER_VAL_VAR_Vfb: {
                // These assign to an arbitrary long list of variables.
                auto& iter_vars = inst->aux->loop_vars;
                auto depth = inst->loop_depth;

                for ( auto v : iter_vars ) {
                    if ( v < 0 )
                        // This happens for '_' dummy
                        continue;

                    CheckSlotAssignment(v, inst);

                    // Also mark it as usage throughout the
                    // loop.  Otherwise, we risk pruning the
                    // variable if it happens to not be used
                    // (which will mess up the iteration logic)
                    // or doubling it up with some other value
                    // inside the loop (which will fail when
                    // the loop var has memory management
                    // associated with it).
                    ExtendLifetime(v, EndOfLoop(inst, depth));
                }

                // No need to check the additional "var" associated
                // with OP_NEXT_TABLE_ITER_VAL_VAR_Vfb as that's
                // a slot-1 assignment.  However, similar to other
                // loop variables, mark this as a usage.
                if ( inst->op == OP_NEXT_TABLE_ITER_VAL_VAR_Vfb )
                    ExtendLifetime(inst->v1, EndOfLoop(inst, depth));
            } break;

            case OP_NEXT_TABLE_ITER_NO_VARS_fb: break;

            case OP_NEXT_TABLE_ITER_VAL_VAR_NO_VARS_Vfb: {
                auto depth = inst->loop_depth;
                ExtendLifetime(inst->v1, EndOfLoop(inst, depth));
            } break;

            case OP_NEXT_VECTOR_ITER_VAL_VAR_VVsb: {
                CheckSlotAssignment(inst->v2, inst);

                auto depth = inst->loop_depth;
                ExtendLifetime(inst->v1, EndOfLoop(inst, depth));
                ExtendLifetime(inst->v2, EndOfLoop(inst, depth));
            } break;

            case OP_NEXT_VECTOR_BLANK_ITER_VAL_VAR_Vsb: {
                auto depth = inst->loop_depth;
                ExtendLifetime(inst->v1, EndOfLoop(inst, depth));
            } break;

            case OP_NEXT_VECTOR_ITER_Vsb:
            case OP_NEXT_STRING_ITER_Vsb:
                // Sometimes loops are written that don't actually
                // use the iteration variable.  However, we still
                // need to mark the variable as having usage
                // throughout the loop, lest we elide the iteration
                // instruction.  An alternative would be to transform
                // such iterators into variable-less versions.  That
                // optimization hardly seems worth the trouble, though,
                // given the presumed rarity of such loops.
                ExtendLifetime(inst->v1, EndOfLoop(inst, inst->loop_depth));
                break;

            case OP_NEXT_VECTOR_BLANK_ITER_sb:
            case OP_NEXT_STRING_BLANK_ITER_sb: break;

            case OP_INIT_TABLE_LOOP_Vf:
            case OP_INIT_VECTOR_LOOP_Vs:
            case OP_INIT_STRING_LOOP_Vs: {
                // For all of these, the scope of the aggregate being
                // looped over is the entire loop, even if it doesn't
                // directly appear in it, and not just the initializer.
                // For all three, the aggregate is in v1.
                ASSERT(i < insts1.size() - 1);
                auto succ = insts1[i + 1];
                ASSERT(succ->live);
                auto depth = succ->loop_depth;
                ExtendLifetime(inst->v1, EndOfLoop(succ, depth));

                // Important: we skip the usual UsesSlots analysis
                // below since we've already set it, and don't want
                // to perturb ExtendLifetime's consistency check.
                continue;
            }

            case OP_STORE_GLOBAL_g: {
                // Use of the global goes to here.
                auto slot = frame_layout1[globalsI[inst->v1].id.get()];
                ExtendLifetime(slot, EndOfLoop(inst, 1));
                break;
            }

            case OP_DETERMINE_TYPE_MATCH_VV: {
                auto aux = inst->aux;
                int n = aux->n;
                for ( int i = 0; i < n; ++i ) {
                    auto slot_i = aux->elems[i].Slot();
                    if ( slot_i >= 0 ) {
                        CheckSlotAssignment(slot_i, inst);
                        // The variable gets used in the switch that
                        // immediately follows this instruction, hence
                        // "i + 1" in the following.
                        ExtendLifetime(slot_i, insts1[i + 1]);
                    }
                }
                break;
            }

            case OP_LAMBDA_Vi: {
                auto aux = inst->aux;
                int n = aux->n;
                for ( int i = 0; i < n; ++i ) {
                    auto slot_i = aux->elems[i].Slot();
                    if ( slot_i >= 0 )
                        ExtendLifetime(slot_i, EndOfLoop(inst, 1));
                }
                break;
            }

            default:
                // Look for slots in auxiliary information.
                auto aux = inst->aux;
                if ( ! aux || ! aux->elems_has_slots )
                    break;

                int n = aux->n;
                for ( auto j = 0; j < n; ++j ) {
                    auto slot_j = aux->elems[j].Slot();
                    if ( slot_j < 0 )
                        continue;

                    ExtendLifetime(slot_j, EndOfLoop(inst, 1));
                }
                break;
        }

        int s1;
        int s2;
        int s3;
        int s4;

        if ( ! inst->UsesSlots(s1, s2, s3, s4) )
            continue;

        CheckSlotUse(s1, inst);
        CheckSlotUse(s2, inst);
        CheckSlotUse(s3, inst);
        CheckSlotUse(s4, inst);
    }
}

void ZAMCompiler::ReMapFrame() {
    // General approach: go sequentially through the instructions,
    // see which variables begin their lifetime at each, and at
    // that point remap the variables to a suitable frame slot.

    frame1_to_frame2.resize(frame_layout1.size(), -1);
    managed_slotsI.clear();

    for ( zeek_uint_t i = 0; i < insts1.size(); ++i ) {
        auto inst = insts1[i];

        if ( inst_beginnings.count(inst) == 0 )
            continue;

        auto vars = inst_beginnings[inst];
        for ( auto v : vars ) {
            // Don't remap variables whose values aren't actually used.
            int slot = frame_layout1[v];
            if ( denizen_ending.count(slot) > 0 )
                ReMapVar(v, slot, i);
        }
    }

#if 0
	// Low-level debugging code.
	printf("%s frame remapping:\n", func->Name());

	for ( unsigned int i = 0; i < shared_frame_denizens.size(); ++i )
		{
		auto& s = shared_frame_denizens[i];
		printf("*%d (%s) %lu [%d->%d]:",
			i, s.is_managed ? "M" : "N",
			s.ids.size(), s.id_start[0], s.scope_end);

		for ( auto j = 0; j < s.ids.size(); ++j )
			printf(" %s (%d)", s.ids[j]->Name(), s.id_start[j]);

		printf("\n");
		}
#endif

    // Update the globals we track, where we prune globals that
    // didn't wind up being used.
    std::vector<GlobalInfo> used_globals;
    std::vector<int> remapped_globals;

    for ( auto& g : globalsI ) {
        g.slot = frame1_to_frame2[g.slot];
        if ( g.slot >= 0 ) {
            remapped_globals.push_back(used_globals.size());
            used_globals.push_back(g);
        }
        else
            remapped_globals.push_back(-1);
    }

    globalsI = used_globals;

    // Gulp - now rewrite every instruction to update its slot usage.
    // In the process, if an instruction becomes a direct assignment
    // of <slot-n> = <slot-n>, then we remove it.

    int n1_slots = frame1_to_frame2.size();

    for ( unsigned int i = 0; i < insts1.size(); ++i ) {
        auto inst = insts1[i];

        if ( ! inst->live )
            continue;

        if ( inst->AssignsToSlot1() ) {
            auto v1 = inst->v1;
            ASSERT(v1 >= 0 && v1 < n1_slots);
            inst->v1 = frame1_to_frame2[v1];
        }

        // Handle special cases.
        switch ( inst->op ) {
            case OP_INIT_TABLE_LOOP_Vf:
            case OP_NEXT_TABLE_ITER_fb:
            case OP_NEXT_TABLE_ITER_VAL_VAR_Vfb: {
                // Rewrite iteration variables. Strictly speaking we only
                // need to do this for the INIT, not the NEXT, since the
                // latter currently doesn't access the variables directly but
                // instead uses pointers set up by the INIT. We do both types
                // here, though, to keep things consistent and to help avoid
                // surprises if the implementation changes in the future.
                auto& iter_vars = inst->aux->loop_vars;
                for ( auto& v : iter_vars ) {
                    if ( v < 0 )
                        continue;
                    ASSERT(v < n1_slots);
                    v = frame1_to_frame2[v];
                }
            } break;

            default:
                // Update slots in auxiliary information.
                auto aux = inst->aux;
                if ( ! aux || ! aux->elems_has_slots )
                    break;

                for ( auto j = 0; j < aux->n; ++j ) {
                    auto slot = aux->elems[j].Slot();

                    if ( slot < 0 )
                        // This is instead a constant.
                        continue;

                    auto new_slot = frame1_to_frame2[slot];

                    if ( new_slot < 0 ) {
                        ODesc d;
                        inst->loc->Loc()->Describe(&d);
                        reporter->Error("%s: value used but not set: %s", d.Description(),
                                        frame_denizens[slot]->Name());
                    }

                    aux->elems[j].SetSlot(new_slot);
                }
                break;
        }

        if ( inst->IsGlobalLoad() ) {
            // Slot v2 of these is the index into globals[]
            // rather than a frame.
            int g = inst->v2;
            ASSERT(remapped_globals[g] >= 0);
            inst->v2 = remapped_globals[g];

            // We *don't* want to UpdateSlots below as that's
            // based on interpreting v2 as slots rather than an
            // index into globals.
            continue;
        }

        if ( inst->IsGlobalStore() ) { // Slot v1 of these is the index into globals[].
            int g = inst->v1;
            ASSERT(remapped_globals[g] >= 0);
            inst->v1 = remapped_globals[g];

            // We don't have any other slots to update.
            continue;
        }

        inst->UpdateSlots(frame1_to_frame2);

        if ( inst->IsDirectAssignment() && inst->v1 == inst->v2 )
            KillInst(i);
    }

    frame_sizeI = shared_frame_denizens.size();
}

void ZAMCompiler::ReMapInterpreterFrame() {
    // First, track function parameters.  We could elide this if we
    // decide to alter the calling sequence for compiled functions.
    auto args = scope->OrderedVars();
    int nparam = func->GetType()->Params()->NumFields();
    int next_interp_slot = 0;

    for ( const auto& a : args ) {
        if ( --nparam < 0 )
            break;

        ASSERT(a->Offset() == next_interp_slot);
        ++next_interp_slot;
    }

    // Update frame sizes for functions that might have more than
    // one body.
    auto f = func.get();
    if ( remapped_intrp_frame_sizes.count(f) == 0 || remapped_intrp_frame_sizes[f] < next_interp_slot )
        remapped_intrp_frame_sizes[f] = next_interp_slot;
}

void ZAMCompiler::ReMapVar(const ID* id, int slot, zeek_uint_t inst) {
    // A greedy algorithm for this is to simply find the first suitable
    // frame slot.  We do that with one twist: we also look for a
    // compatible slot for which its current end-of-scope is exactly
    // the start-of-scope for the new identifier.  The advantage of
    // doing so is that this commonly occurs for code like "a.1 = a"
    // from resolving parameters to inlined functions, and if "a.1" and
    // "a" share the same slot then we can elide the assignment.
    //
    // In principle we could perhaps do better than greedy using a more
    // powerful allocation method like graph coloring.  However, far and
    // away the bulk of our variables are short-lived temporaries,
    // for which greedy should work fine.
    //
    // Note, we also need to make sure that denizens sharing a slot
    // are all consistently either managed, or non-managed, types.
    // One subtlety in this regard is that identifiers that are types
    // should always be deemed "managed", even if the type they refer
    // to is not managed, because what matters for uses of those
    // identifiers is interpreting them as "any" values having an
    // internal type of TYPE_TYPE.
    bool is_managed = ZVal::IsManagedType(id->GetType()) || id->IsType();

    int apt_slot = -1;
    for ( unsigned int i = 0; i < shared_frame_denizens.size(); ++i ) {
        auto& s = shared_frame_denizens[i];

        // Note that the following test is <= rather than <.
        // This is because assignment in instructions happens after
        // using any variables to compute the value to assign.
        // ZAM instructions are careful to allow operands and
        // assignment destinations to refer to the same slot.

        if ( s.scope_end <= static_cast<int>(inst) && s.is_managed == is_managed ) { // It's compatible.
            if ( s.scope_end == static_cast<int>(inst) ) {                           // It ends right on the money.
                apt_slot = i;
                break;
            }

            else if ( apt_slot < 0 )
                // We haven't found a candidate yet, take
                // this one, but keep looking.
                apt_slot = i;
        }
    }

    int scope_end = denizen_ending[slot]->inst_num;

    if ( apt_slot < 0 ) {
        // No compatible existing slot.  Create a new one.
        apt_slot = shared_frame_denizens.size();

        FrameSharingInfo info;
        info.is_managed = is_managed;
        shared_frame_denizens.push_back(std::move(info));

        if ( is_managed )
            managed_slotsI.push_back(apt_slot);
    }

    auto& s = shared_frame_denizens[apt_slot];

    s.ids.push_back(id);
    s.id_start.push_back(inst);
    s.scope_end = scope_end;

    frame1_to_frame2[slot] = apt_slot;
}

void ZAMCompiler::CheckSlotAssignment(int slot, const ZInstI* inst) {
    ASSERT(slot >= 0 && static_cast<zeek_uint_t>(slot) < frame_denizens.size());
    // We construct temporaries such that their values are never used
    // earlier than their definitions in loop bodies.  For other
    // denizens, however, they can be, so in those cases we expand the
    // lifetime beginning to the start of any loop region.
    if ( ! reducer->IsTemporary(frame_denizens[slot]) )
        inst = BeginningOfLoop(inst, 1);

    SetLifetimeStart(slot, inst);
}

void ZAMCompiler::SetLifetimeStart(int slot, const ZInstI* inst) {
    if ( denizen_beginning.count(slot) > 0 ) {
        // Beginning of denizen's lifetime already seen, nothing
        // more to do other than check for consistency.
        ASSERT(denizen_beginning[slot]->inst_num <= inst->inst_num);
    }

    else { // denizen begins here
        denizen_beginning[slot] = inst;

        if ( inst_beginnings.count(inst) == 0 )
            // Need to create a set to track the denizens
            // beginning at the instruction.
            inst_beginnings[inst] = {};

        inst_beginnings[inst].insert(frame_denizens[slot]);
    }
}

void ZAMCompiler::CheckSlotUse(int slot, const ZInstI* inst) {
    if ( slot < 0 )
        return;

    ASSERT(static_cast<zeek_uint_t>(slot) < frame_denizens.size());

    if ( denizen_beginning.count(slot) == 0 ) {
        ODesc d;
        inst->loc->Loc()->Describe(&d);
        reporter->Error("%s: value used but not set: %s", d.Description(), frame_denizens[slot]->Name());
    }

    // See comment above about temporaries not having their values
    // extend around loop bodies.  HOWEVER if a temporary is defined
    // at a lower loop depth than that for this instruction, then we
    // extend its lifetime to the end of this instruction's loop.
    if ( reducer->IsTemporary(frame_denizens[slot]) ) {
        ASSERT(denizen_beginning.count(slot) > 0);
        int definition_depth = denizen_beginning[slot]->loop_depth;

        if ( inst->loop_depth > definition_depth )
            inst = EndOfLoop(inst, inst->loop_depth);
    }
    else
        inst = EndOfLoop(inst, 1);

    ExtendLifetime(slot, inst);
}

void ZAMCompiler::ExtendLifetime(int slot, const ZInstI* inst) {
    ASSERT(slot >= 0);
    auto id = frame_denizens[slot];
    auto& t = id->GetType();

    if ( denizen_ending.count(slot) > 0 ) {
        // End of denizen's lifetime already seen.  Check for
        // consistency and then extend as needed.

        auto old_inst = denizen_ending[slot];

        // Don't complain for temporaries that already have
        // extended lifetimes, as that can happen if they're
        // used as a "for" loop-over target, which already
        // extends lifetime across the body of the loop.
        if ( inst->loop_depth > 0 && reducer->IsTemporary(frame_denizens[slot]) &&
             old_inst->inst_num >= inst->inst_num )
            return;

        // We expect to only be increasing the slot's lifetime ...
        // *unless* we're inside a nested loop, in which case
        // the slot might have already been extended to the
        // end of the outer loop.
        ASSERT(old_inst->inst_num <= inst->inst_num || inst->loop_depth > 1);

        if ( old_inst->inst_num < inst->inst_num ) { // Extend.
            inst_endings[old_inst].erase(frame_denizens[slot]);

            if ( inst_endings.count(inst) == 0 )
                inst_endings[inst] = {};

            inst_endings[inst].insert(frame_denizens[slot]);
            denizen_ending.at(slot) = inst;
        }
    }

    else { // first time seeing a use of this denizen
        denizen_ending[slot] = inst;

        if ( inst_endings.count(inst) == 0 ) {
            IDSet denizens;
            inst_endings[inst] = denizens;
        }

        inst_endings[inst].insert(frame_denizens[slot]);
    }
}

const ZInstI* ZAMCompiler::BeginningOfLoop(const ZInstI* inst, int depth) const {
    auto i = inst->inst_num;

    while ( i >= 0 && insts1[i]->loop_depth >= depth )
        --i;

    if ( i == inst->inst_num )
        return inst;

    // We moved backwards to just beyond a loop that inst is part of.
    // Move to that loop's (live) beginning.
    ++i;
    while ( i != inst->inst_num && ! insts1[i]->live )
        ++i;

    return insts1[i];
}

const ZInstI* ZAMCompiler::EndOfLoop(const ZInstI* inst, int depth) const {
    auto i = inst->inst_num;

    while ( i < int(insts1.size()) && insts1[i]->loop_depth >= depth )
        ++i;

    if ( i == inst->inst_num )
        return inst;

    // We moved forwards to just beyond a loop that inst is part of.
    // Move to that loop's (live) end.
    --i;
    while ( i != inst->inst_num && ! insts1[i]->live )
        --i;

    return insts1[i];
}

bool ZAMCompiler::VarIsUsed(int slot) const {
    for ( auto& inst : insts1 ) {
        if ( inst->live && inst->UsesSlot(slot) )
            return true;

        auto aux = inst->aux;
        if ( aux && aux->elems_has_slots ) {
            for ( int j = 0; j < aux->n; ++j )
                if ( aux->elems[j].Slot() == slot )
                    return true;
        }
    }

    return false;
}

ZInstI* ZAMCompiler::FirstLiveInst(ZInstI* i, bool follow_gotos) {
    if ( i == pending_inst )
        return nullptr;

    auto n = FirstLiveInst(i->inst_num, follow_gotos);
    if ( n < insts1.size() )
        return insts1[n];
    else
        return nullptr;
}

zeek_uint_t ZAMCompiler::FirstLiveInst(zeek_uint_t i, bool follow_gotos) {
    zeek_uint_t num_inspected = 0;
    while ( i < insts1.size() ) {
        auto i0 = insts1[i];
        if ( i0->live ) {
            if ( follow_gotos && i0->IsUnconditionalBranch() ) {
                if ( ++num_inspected > insts1.size() ) {
                    reporter->Error("%s contains an infinite loop", func->GetName().c_str());
                    return i;
                }

                i = i0->target->inst_num;
                continue;
            }

            return i;
        }

        ++i;
        ++num_inspected;
    }

    return i;
}

void ZAMCompiler::KillInst(zeek_uint_t i) {
    auto inst = insts1[i];

    ASSERT(inst->live);

    inst->live = false;
    auto t = inst->target;
    if ( t ) {
        if ( t->live ) {
            --(t->num_labels);
            ASSERT(t->num_labels >= 0);
        }
        else
            ASSERT(t->num_labels == 0);
    }

    int num_labels = inst->num_labels;
    // We're about to transfer its labels.
    inst->num_labels = 0;

    if ( inst->IsUnconditionalBranch() ) {
        ASSERT(t);

        // No direct flow after this point ... unless we're
        // branching to the next immediate live instruction.
        auto after_inst = NextLiveInst(inst, true);
        auto live_target = FirstLiveInst(t, true);

        if ( after_inst != live_target ) {
            // No flow after inst.  Don't propagate its labels.
            // Given that, it had better not have any!
            ASSERT(num_labels == 0);
        }
    }

    ZInstI* succ = nullptr;

    if ( num_labels > 0 ) {
        for ( auto j = i + 1; j < insts1.size(); ++j ) {
            if ( insts1[j]->live ) {
                succ = insts1[j];
                break;
            }
        }
        if ( succ )
            succ->num_labels += num_labels;
    }

    // Look into propagating control flow info.
    if ( inst->aux && ! inst->aux->cft.empty() ) {
        auto& cft = inst->aux->cft;

        if ( cft.count(CFT_ELSE) > 0 ) {
            // Push forward unless this was the end of the block.
            if ( cft.count(CFT_BLOCK_END) == 0 ) {
                ASSERT(succ);
                AddCFT(succ, CFT_ELSE);
            }
            else
                // But if it *was* the end of the block, remove that block.
                --cft[CFT_BLOCK_END];
        }

        BackPropagateCFT(i, CFT_BREAK);
        BackPropagateCFT(i, CFT_BLOCK_END);
        BackPropagateCFT(i, CFT_LOOP_END);

        // If's can be killed because their bodies become empty, break's
        // because they just lead to their following instruction, and next's
        // if they become dead code. However, loops and loop conditionals
        // should not be killed.
        ASSERT(cft.count(CFT_LOOP) == 0);
        ASSERT(cft.count(CFT_LOOP_COND) == 0);
    }
}

void ZAMCompiler::BackPropagateCFT(int inst_num, ControlFlowType cf_type) {
    auto inst = insts1[inst_num];
    auto& cft = inst->aux->cft;
    if ( cft.count(cf_type) == 0 )
        return;

    int j = inst_num;
    while ( --j >= 0 )
        if ( insts1[j]->live )
            break;

    // Initializations of unused variables in functions with no arguments can
    // come at the very beginning of the function, in which case there will
    // be no predecessor to back-propagate to.
    if ( j < 0 )
        return;

    // Make sure the CFT entry is created.
    AddCFT(insts1[j], cf_type);

    auto cft_cnt = cft[cf_type];
    --cft_cnt; // we already did one with the AddCFT just above
    insts1[j]->aux->cft[cf_type] += cft_cnt;
}

void ZAMCompiler::KillInsts(zeek_uint_t i) {
    auto inst = insts1[i];

    ASSERT(inst->num_labels == 0);

    KillInst(i);

    for ( auto j = i + 1; j < insts1.size(); ++j ) {
        auto succ = insts1[j];
        if ( succ->live ) {
            if ( succ->num_labels == 0 )
                KillInst(j);
            else
                // Found viable succeeding code.
                break;
        }
    }
}

} // namespace zeek::detail
