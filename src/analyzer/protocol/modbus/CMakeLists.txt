zeek_add_plugin(
    Zeek Modbus
    SOURCES Modbus.cc Plugin.cc
    BIFS events.bif
    PAC modbus.pac modbus-analyzer.pac modbus-protocol.pac)
