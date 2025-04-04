// See the file "COPYING" in the main distribution directory for copyright.

#include "zeek/cluster/serializer/binary-serialization-format/Plugin.h"

#include "zeek/cluster/Component.h"
#include "zeek/cluster/serializer/binary-serialization-format/Serializer.h"

using namespace zeek::cluster;

namespace zeek::plugin::Zeek_Binary_Serializer {

Plugin plugin;

zeek::plugin::Configuration Plugin::Configure() {
    AddComponent(new LogSerializerComponent("ZEEK_BIN_V1", []() -> std::unique_ptr<LogSerializer> {
        return std::make_unique<cluster::detail::BinarySerializationFormatLogSerializer>();
    }));

    zeek::plugin::Configuration config;
    config.name = "Zeek::Binary_Serializer";
    config.description = "Serialization using Zeek's custom binary serialization format";
    return config;
}
} // namespace zeek::plugin::Zeek_Binary_Serializer
