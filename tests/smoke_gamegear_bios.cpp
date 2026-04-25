// Smoke test: Game Gear BIOS mapping behavior

#include "cores/gamegear/GameGearMemoryMap.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    GameGearMemoryMap mem;

    std::vector<uint8_t> bios(0x400, 0xA5);
    std::vector<uint8_t> rom(0x4000, 0x42);
    mem.mapRom(rom.data(), rom.size());
    mem.mapBios(bios.data(), bios.size());
    mem.reset();

    // BIOS should be visible at $0000 when present and enabled by default
    if (mem.read(0x0000u) != 0xA5u) {
        std::cerr << "BIOS was not enabled at reset\n";
        return EXIT_FAILURE;
    }

    // Port $3F is I/O control. It must not affect the BIOS mapping bit.
    mem.writeIoPort(0x3Fu, 0xFFu);
    if (mem.read(0x0000u) != 0xA5u) {
        std::cerr << "I/O control write disabled the BIOS\n";
        return EXIT_FAILURE;
    }

    // Port $3E is memory control. D3=1 disables BIOS and exposes the cartridge.
    mem.writeIoPort(0x3Eu, 0xFFu);
    if (mem.read(0x0000u) != 0x42u) {
        std::cerr << "memory control write did not expose cartridge reset vector\n";
        return EXIT_FAILURE;
    }

    // Clearing D3 re-enables the BIOS mapping at $0000-$03FF.
    mem.writeIoPort(0x3Eu, 0xF7u);
    if (mem.read(0x0000u) != 0xA5u) {
        std::cerr << "memory control write did not re-enable BIOS\n";
        return EXIT_FAILURE;
    }

    // Even mirrors in $07-$3F feed memory control too.
    mem.writeIoPort(0x08u, 0xFFu);
    if (mem.read(0x0000u) != 0x42u) {
        std::cerr << "memory control mirror did not expose cartridge reset vector\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
