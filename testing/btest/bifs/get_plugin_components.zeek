#
# @TEST-EXEC: zeek -b %INPUT >out
# @TEST-EXEC: btest-diff out

event zeek_init()
{
    local result = get_plugin_components("ANALYZER");

    for (i in result)
    {
        local rec = result[i];
        if ( rec$name == "FTP" )
            print rec;
    }
}
