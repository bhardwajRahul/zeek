// See the file "COPYING" in the main distribution directory for copyright.

#include "StorageDummy.h"

#include "zeek/Func.h"
#include "zeek/Val.h"
#include "zeek/storage/ReturnCode.h"

using namespace zeek;
using namespace zeek::storage;

namespace btest::storage::backend {

BackendPtr StorageDummy::Instantiate() { return make_intrusive<StorageDummy>(); }

/**
 * Called by the manager system to open the backend.
 *
 * Derived classes must implement this method. If successful, the
 * implementation must call \a Opened(); if not, it must call Error()
 * with a corresponding message.
 */
OperationResult StorageDummy::DoOpen(OpenResultCallback* cb, RecordValPtr options) {
    RecordValPtr dummy_options = options->GetField<RecordVal>("dummy");
    bool open_fail = dummy_options->GetField<BoolVal>("open_fail")->Get();
    if ( open_fail )
        return {ReturnCode::OPERATION_FAILED, "open_fail was set to true, returning error"};

    open = true;

    return {ReturnCode::SUCCESS};
}

/**
 * Finalizes the backend when it's being closed.
 */
OperationResult StorageDummy::DoClose(ResultCallback* cb) {
    open = false;
    return {ReturnCode::SUCCESS};
}

/**
 * The workhorse method for Put(). This must be implemented by plugins.
 */
OperationResult StorageDummy::DoPut(ResultCallback* cb, ValPtr key, ValPtr value, bool overwrite,
                                    double expiration_time) {
    RecordValPtr dummy_options = backend_options->GetField<RecordVal>("dummy");
    bool timeout_put = dummy_options->GetField<BoolVal>("timeout_put")->Get();
    if ( timeout_put )
        return {ReturnCode::TIMEOUT};

    auto json_key = key->ToJSON()->ToStdString();
    auto json_value = value->ToJSON()->ToStdString();
    data[json_key] = json_value;
    return {ReturnCode::SUCCESS};
}

/**
 * The workhorse method for Get(). This must be implemented for plugins.
 */
OperationResult StorageDummy::DoGet(ResultCallback* cb, ValPtr key) {
    auto json_key = key->ToJSON();
    auto it = data.find(json_key->ToStdString());
    if ( it == data.end() )
        return {ReturnCode::KEY_NOT_FOUND};

    auto val = zeek::detail::ValFromJSON(it->second.c_str(), val_type, Func::nil);
    if ( std::holds_alternative<ValPtr>(val) ) {
        ValPtr val_v = std::get<ValPtr>(val);
        return {ReturnCode::SUCCESS, "", val_v};
    }

    return {ReturnCode::OPERATION_FAILED, std::get<std::string>(val)};
}

/**
 * The workhorse method for Erase(). This must be implemented for plugins.
 */
OperationResult StorageDummy::DoErase(ResultCallback* cb, ValPtr key) {
    auto json_key = key->ToJSON();
    auto it = data.find(json_key->ToStdString());
    if ( it == data.end() )
        return {ReturnCode::KEY_NOT_FOUND};

    data.erase(it);
    return {ReturnCode::SUCCESS};
}

} // namespace btest::storage::backend
