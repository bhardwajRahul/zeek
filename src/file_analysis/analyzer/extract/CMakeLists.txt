zeek_add_plugin(
    Zeek FileExtract
    SOURCES Extract.cc Plugin.cc
    BIFS events.bif functions.bif)
