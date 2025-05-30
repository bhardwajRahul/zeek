# @TEST-PORT: BROKER_MANAGER_PORT
# @TEST-PORT: BROKER_WORKER1_PORT
# @TEST-PORT: BROKER_WORKER2_PORT
#
# @TEST-EXEC: cp $FILES/broker/cluster-layout.zeek .
#
# @TEST-EXEC: btest-bg-run manager   ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=manager zeek -b %INPUT
# @TEST-EXEC: btest-bg-run worker-1  ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=worker-1 zeek -b %INPUT
# @TEST-EXEC: btest-bg-run worker-2  ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=worker-2 zeek -b %INPUT
# @TEST-EXEC: btest-bg-wait 30

# @TEST-EXEC: btest-diff manager/.stdout
#

@load policy/frameworks/cluster/experimental
@load base/frameworks/sumstats

redef Log::default_rotation_interval = 0secs;

global n = 0;

event zeek_init() &priority=5
	{
	local r1 = SumStats::Reducer($stream="test", $apply=set(SumStats::SUM, SumStats::MIN, SumStats::MAX, SumStats::AVERAGE, SumStats::STD_DEV, SumStats::VARIANCE, SumStats::UNIQUE));
	SumStats::create([$name="test sumstat",
	                  $epoch=1hr,
	                  $reducers=set(r1)]);
	}

event Broker::peer_lost(endpoint: Broker::EndpointInfo, msg: string)
	{
	terminate();
	}

event on_demand()
	{
	local host = 7.2.1.5;
	when [host] ( local result = SumStats::request_key("test sumstat", [$host=host]) )
		{
		print "SumStat key request";
		if ( "test" in result )
			print fmt("    Host: %s -> %.0f", host, result["test"]$sum);

		if ( Cluster::node == "manager" )
		  terminate();
		}
	}

global ready_count = 0;
event ready_to_demand()
	{
	++ready_count;

	if ( ready_count == 2 )
		event on_demand();
	}

event Cluster::Experimental::cluster_started()
	{
	if ( Cluster::node == "worker-1" )
		{
		SumStats::observe("test", [$host=1.2.3.4], [$num=34]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=30]);
		SumStats::observe("test", [$host=6.5.4.3], [$num=1]);
		SumStats::observe("test", [$host=7.2.1.5], [$num=54]);
		}
	if ( Cluster::node == "worker-2" )
		{
		SumStats::observe("test", [$host=1.2.3.4], [$num=75]);
		SumStats::observe("test", [$host=1.2.3.4], [$num=30]);
		SumStats::observe("test", [$host=7.2.1.5], [$num=91]);
		SumStats::observe("test", [$host=10.10.10.10], [$num=5]);
		}

	Broker::publish(Cluster::manager_topic, ready_to_demand);
	}
