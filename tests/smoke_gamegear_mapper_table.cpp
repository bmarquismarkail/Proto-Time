#include "cores/gamegear/GameGearMapperFactory.hpp"
#include "cores/gamegear/mappers/Sega3155208Mapper.hpp"
#include "cores/gamegear/mappers/Sega3155235Mapper.hpp"
#include "cores/gamegear/mappers/Sega3155365Mapper.hpp"

#include <cassert>
#include <vector>
#include <optional>
#include <filesystem>
#include <string>

int main() {
    // Header-based mapping: include the title in the ROM header area
    std::vector<uint8_t> romHeader(0x1000, 0u);
    const std::string golden = "GOLDEN AXE";
    std::copy(golden.begin(), golden.end(), romHeader.begin() + 0x10);
    auto m1 = createMapperFromRom(romHeader.data(), romHeader.size(), std::nullopt);
    assert(m1 && "mapper should not be null for header-based ROM");
    assert(dynamic_cast<Sega3155235Mapper*>(m1.get()) && "expected Sega3155235Mapper for GOLDEN AXE header");

    // Filename-based mapping: Sonic -> 315-5208
    std::vector<uint8_t> romName(0x8000, 0u);
    auto m2 = createMapperFromRom(romName.data(), romName.size(), std::optional<std::filesystem::path>{"/home/user/Sonic The Hedgehog (J).sms"});
    assert(m2 && "mapper should not be null for filename-based ROM");
    assert(dynamic_cast<Sega3155208Mapper*>(m2.get()) && "expected Sega3155208Mapper for Sonic filename");

    // Size-based fallbacks
    std::vector<uint8_t> smallRom(131072u, 0u); // <=131072 => 5208
    auto m3 = createMapperFromRom(smallRom.data(), smallRom.size(), std::nullopt);
    assert(m3 && dynamic_cast<Sega3155208Mapper*>(m3.get()));

    std::vector<uint8_t> midRom(200000u, 0u); // <=524288 => 5235
    auto m4 = createMapperFromRom(midRom.data(), midRom.size(), std::nullopt);
    assert(m4 && dynamic_cast<Sega3155235Mapper*>(m4.get()));

    std::vector<uint8_t> largeRom(600000u, 0u); // >524288 => 5365
    auto m5 = createMapperFromRom(largeRom.data(), largeRom.size(), std::nullopt);
    assert(m5 && dynamic_cast<Sega3155365Mapper*>(m5.get()));

    return 0;
}
