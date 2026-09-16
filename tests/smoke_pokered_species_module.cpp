#include "machine/modding/NativeMod.hpp"
#include <cassert>

int main(int argc, char** argv) {
    using namespace BMMQ::Modding;
    assert(argc == 2);
    auto host = std::make_shared<ModHost>();
    LoadedMod metadata{"pokered.title-species", "1", 0, {}, argv[1]};
    for (const auto& [name, size] : {std::pair{"selection", 32u}, {"records", 257u * 29u}, {"current", 2u}})
        metadata.regions.emplace(name, host->createRegion(std::string("pokered.title-species:") + name, size));
    auto table = host->region(metadata.regions.at("selection"));
    auto records = host->region(metadata.regions.at("records"));
    auto current = host->region(metadata.regions.at("current"));
    table[1] = 1; // ID 256, little-endian.
    records[256 * 29] = 0x99;
    records[256 * 29 + 2] = 123;
    auto select = NativeMod::load(metadata, host);
    auto reader = NativeMod::load(metadata, host); // Instances share pool state.
    assert(select->reset());
    TimeModCallV1 call{sizeof(TimeModCallV1), 2, 0, 0, 42};
    assert(reader->invoke(call) && call.result == 0);
    call.hook_id = 1;
    assert(select->invoke(call) && call.result == 256);
    assert(current[0] == 0 && current[1] == 1);
    call.hook_id = 2; call.argument = 2;
    assert(reader->invoke(call) && call.result == 123);
    call.argument = 29;
    assert(!reader->invoke(call) && call.result == 123);
    call.argument = UINT64_MAX;
    assert(!reader->invoke(call));
    call.hook_id = 1; call.argument = 16;
    assert(!select->invoke(call));
    call.argument = 1; // Empty record must not replace the selected ID.
    assert(!select->invoke(call) && current[1] == 1);
    table[2] = 255; table[3] = 255;
    assert(!select->invoke(call) && current[1] == 1);
    call.hook_id = 999;
    assert(!select->invoke(call));
    assert(reader->reset());
    assert(current[0] == 255 && current[1] == 255);
}
