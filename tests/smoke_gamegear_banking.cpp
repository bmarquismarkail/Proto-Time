#include "cores/gamegear/GameGearMemoryMap.hpp"

#include "cores/gamegear/GameGearCartridge.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    GameGearCartridge cartridge;
    GameGearMemoryMap memory;
    memory.setCartridge(&cartridge);

    // Create a ROM with 3 distinct 16KiB banks: 0x00, 0x11, 0x22
    const std::size_t page = 0x4000u;
    std::vector<uint8_t> rom(page * 3u);
    for (std::size_t i = 0; i < page; ++i) rom[i] = 0x00u;
    for (std::size_t i = 0; i < page; ++i) rom[page + i] = 0x11u;
    for (std::size_t i = 0; i < page; ++i) rom[2*page + i] = 0x22u;

    assert(cartridge.load(rom.data(), rom.size()));
    memory.reset();

    // Default banks: 0,1,2
    assert(memory.read(0x0000u) == 0x00u);
    assert(memory.read(0x4000u) == 0x11u);
    assert(memory.read(0x8000u) == 0x22u);

    // Switch bank 0 to bank 2 via write to 0xFFFC
    memory.write(0xFFFCu, 0x02u);
    assert(memory.read(0x0000u) == 0x22u);

    // Switch bank 1 to bank 0 via write to 0xFFFD
    memory.write(0xFFFDu, 0x00u);
    assert(memory.read(0x4000u) == 0x00u);

    // Ensure bank values wrap by number of banks (e.g., 3 -> index 0)
    memory.write(0xFFFCu, 0x03u); // 3 % 3 == 0
    assert(memory.read(0x0000u) == 0x00u);

    memory.write(0xFFFFu, 0x01u); // enable SRAM window at 0x8000-0xBFFF
    memory.write(0x8000u, 0xA5u);
    assert(memory.read(0x8000u) == 0xA5u);

    memory.write(0xFFFFu, 0x03u); // switch to SRAM bank 1
    assert(memory.read(0x8000u) == 0x00u);
    memory.write(0x8000u, 0x5Au);
    assert(memory.read(0x8000u) == 0x5Au);

    memory.write(0xFFFFu, 0x01u);
    assert(memory.read(0x8000u) == 0xA5u);

    return 0;
}
