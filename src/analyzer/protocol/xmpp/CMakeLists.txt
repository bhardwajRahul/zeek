zeek_add_plugin(
    Zeek XMPP
    SOURCES Plugin.cc XMPP.cc
    BIFS events.bif
    PAC xmpp.pac xmpp-analyzer.pac xmpp-protocol.pac)
