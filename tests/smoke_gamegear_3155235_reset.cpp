#include "cores/gamegear/mappers/Sega3155235Mapper.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    Sega3155235Mapper mapper;
    GameGearMemoryMap memory;
    memory.setCartridge(&mapper);

    const std::size_t page = 0x4000u;
    std::vector<uint8_t> rom(page * 2u);
    for (std::size_t i = 0; i < page; ++i) rom[i] = 0xAAu;
    for (std::size_t i = 0; i < page; ++i) rom[page + i] = 0xBBu;

    assert(mapper.load(rom.data(), rom.size()));
    memory.reset();

    // After Sega315-5235 reset the documented power-up values are:
    // FFFC=0x00, FFFD=0x00, FFFE=0x01 -> resulting mapping:
    // page0 -> bank0 (0xAA)
    // page1 -> bank0 (0xAA)
    // page2 -> bank1 (0xBB)
    assert(memory.read(0x0000u) == 0xAAu);
    assert(memory.read(0x4000u) == 0xAAu);
    assert(memory.read(0x8000u) == 0xBBu);

    return 0;
}
