zeek_add_plugin(
    Zeek NTP
    SOURCES NTP.cc Plugin.cc
    BIFS types.bif events.bif
    PAC ntp.pac ntp-analyzer.pac ntp-mode7.pac ntp-protocol.pac)
