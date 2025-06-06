# @TEST-PORT: BROKER_MANAGER_PORT
# @TEST-PORT: BROKER_WORKER1_PORT
# @TEST-PORT: BROKER_WORKER2_PORT
#
# @TEST-EXEC: cp $FILES/broker/cluster-layout.zeek .
#
# @TEST-EXEC: btest-bg-run manager   ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=manager zeek -b %INPUT
# @TEST-EXEC: btest-bg-run worker-1  ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=worker-1 zeek -b %INPUT
# @TEST-EXEC: btest-bg-run worker-2  ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=worker-2 zeek -b %INPUT
# This timeout needs to be large to accommodate ZAM compilation delays.
# @TEST-EXEC: btest-bg-wait 45
# @TEST-EXEC: TEST_DIFF_CANONIFIER=$SCRIPTS/diff-sort btest-diff manager/.stdout

@load base/frameworks/sumstats
@load policy/frameworks/cluster/experimental

redef Log::default_rotation_interval = 0secs;
global did_data = F;

event zeek_init() &priority=5
	{
	local r1: SumStats::Reducer = [$stream="test", $apply=set(SumStats::SAMPLE), $num_samples=5];
	SumStats::create([$name="test",
	                  $epoch=5secs,
	                  $reducers=set(r1),
	                  $epoch_result(ts: time, key: SumStats::Key, result: SumStats::Result) =
	                  	{
	                  	if ( ! did_data ) return;
	                  	local r = result["test"];
	                  	print fmt("Host: %s  Sampled observations: %d", key$host, r$sample_elements);
	                  	local sample_nums: vector of count = vector();
	                  	for ( sample in r$samples ) 
	                  		sample_nums += r$samples[sample]$num;

	                  	print fmt("    %s", sort(sample_nums));
	                  	},
                      $epoch_finished(ts: time) =
                      	{
                      	if ( did_data )
	                  		terminate();
	                  	}]);
	}

event Broker::peer_lost(endpoint: Broker::EndpointInfo, msg: string)
	{
	terminate();
	}

event Cluster::Experimental::cluster_started()
	{
	if ( Cluster::node == "worker-1" )
		{
		SumStats::observe("test", [$host=1.2.3.4], [$num=5]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=22]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=94]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=50]);
		# I checked the random numbers. seems legit.
		SumStats::observe("test", [$host=1.2.3.4], [$num=51]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=61]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=61]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=71]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=81]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=91]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=101]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=111]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=121]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=131]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=141]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=151]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=161]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=171]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=181]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=191]);

		SumStats::observe("test", [$host=6.5.4.3], [$num=2]);
		SumStats::observe("test", [$host=7.2.1.5], [$num=1]);
		}
	if ( Cluster::node == "worker-2" )
		{
		SumStats::observe("test", [$host=1.2.3.4], [$num=75]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=30]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=3]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=57]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=52]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=61]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=95]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=95]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=95]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=95]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=95]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=95]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=95]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=95]);
		SumStats::observe("test", [$host=6.5.4.3], [$num=5]);
		SumStats::observe("test", [$host=7.2.1.5], [$num=91]);
		SumStats::observe("test", [$host=10.10.10.10], [$num=5]);
		}

	did_data = T;
	}
