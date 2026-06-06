#include "cores/gamegear/GameGearCartridge.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    GameGearCartridge cartridge;
    GameGearMemoryMap memory;
    memory.setCartridge(&cartridge);

    const std::size_t page = 0x4000u;
    std::vector<uint8_t> rom(page * 4u);
    for (std::size_t i = 0; i < page; ++i) rom[i] = 0x10u;
    for (std::size_t i = 0; i < page; ++i) rom[page + i] = 0x20u;
    for (std::size_t i = 0; i < page; ++i) rom[page * 2u + i] = 0x30u;
    for (std::size_t i = 0; i < page; ++i) rom[page * 3u + i] = 0x40u;

    assert(cartridge.load(rom.data(), rom.size()));
    memory.reset();

    assert(memory.read(0x0000u) == 0x10u);
    assert(memory.read(0x4000u) == 0x20u);
    assert(memory.read(0x8000u) == 0x30u);

    memory.write(0xFFFFu, 0x07u); // wraps to bank 3 for page 2
    assert(memory.read(0x8000u) == 0x40u);

    memory.write(0xFFFCu, 0x08u); // SRAM bank 0 enable
    memory.write(0x8000u, 0xA5u);
    memory.write(0xBFFFu, 0x5Au);
    assert(memory.read(0x8000u) == 0xA5u);
    assert(memory.read(0xBFFFu) == 0x5Au);

    memory.write(0xFFFCu, 0x0Cu); // SRAM bank 1 enable
    assert(memory.read(0x8000u) == 0x00u);
    memory.write(0x8000u, 0x3Cu);
    assert(memory.read(0x8000u) == 0x3Cu);

    memory.write(0xFFFCu, 0x08u);
    assert(memory.read(0x8000u) == 0xA5u);

    memory.write(0xFFFCu, 0x00u); // disable SRAM, ROM visible again
    assert(memory.read(0x8000u) == 0x40u);

    memory.write(0x8000u, 0x99u); // ROM window writes ignored when SRAM disabled
    assert(memory.read(0x8000u) == 0x40u);

    return 0;
}
