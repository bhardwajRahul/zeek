# @TEST-PORT: BROKER_PORT1
# @TEST-PORT: BROKER_PORT2
# @TEST-PORT: BROKER_PORT3
# @TEST-PORT: BROKER_PORT4
# @TEST-PORT: BROKER_PORT5
#
# @TEST-EXEC: btest-bg-run manager-1 ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=manager-1 zeek -b %INPUT
# @TEST-EXEC: btest-bg-run proxy-1   ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=proxy-1 zeek -b %INPUT
# @TEST-EXEC: btest-bg-run proxy-2   ZEEKPATH=$ZEEKPATH:.. CLUSTER_NODE=proxy-2 zeek -b %INPUT
# @TEST-EXEC: btest-bg-wait 40
# @TEST-EXEC: btest-diff manager-1/.stdout

@load policy/frameworks/cluster/experimental

# @TEST-START-FILE cluster-layout.zeek
redef Cluster::nodes = {
	["manager-1"] = [$node_type=Cluster::MANAGER, $ip=127.0.0.1, $p=to_port(getenv("BROKER_PORT1"))],
	["proxy-1"] = [$node_type=Cluster::PROXY,     $ip=127.0.0.1, $p=to_port(getenv("BROKER_PORT2")), $manager="manager-1"],
	["proxy-2"] = [$node_type=Cluster::PROXY,     $ip=127.0.0.1, $p=to_port(getenv("BROKER_PORT3")), $manager="manager-1"],
};
# @TEST-END-FILE

event go_away()
	{
	terminate();
	}

function print_stuff(heading: string)
	{
	print heading;

	local v: vector of count = vector(0, 1, 2, 3, 13, 37, 42, 101);

	for ( i in v )
		print "hrw", v[i], Cluster::hrw_topic(Cluster::proxy_pool, v[i]);

	local rr_key = "test";

	for ( i in v )
		print "rr", Cluster::rr_topic(Cluster::proxy_pool, rr_key);

	# Just checking the same keys still map to same topic ...
	for ( i in v )
		print "hrw", v[i], Cluster::hrw_topic(Cluster::proxy_pool, v[i]);
	}

event Cluster::Experimental::cluster_started()
	{
	if ( Cluster::node != "manager-1" )
		return;

	print_stuff("1st stuff");
	local e = Broker::make_event(go_away);
	Broker::publish(Cluster::node_topic("proxy-1"), e);
	}

event Cluster::node_down(name: string, id: string)
	{
	if ( Cluster::node != "manager-1" )
		return;

	if ( name == "proxy-1" )
		{
		print_stuff("2nd stuff");
		local e = Broker::make_event(go_away);
		Broker::publish(Cluster::node_topic("proxy-2"), e);
		}

	if ( name == "proxy-2" )
		{
		print_stuff("no stuff");
		terminate();
		}
	}

event Cluster::node_down(name: string, id: string)
	{
	if ( name == "manager-1" )
		terminate();
	}
