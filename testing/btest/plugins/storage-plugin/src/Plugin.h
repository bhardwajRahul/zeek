
#pragma once

#include <plugin/Plugin.h>

namespace btest::plugin::Testing_StorageDummy {

class Plugin : public zeek::plugin::Plugin {
protected:
    // Overridden from plugin::Plugin.
    virtual zeek::plugin::Configuration Configure();
};

extern Plugin plugin;

} // namespace btest::plugin::Testing_StorageDummy
