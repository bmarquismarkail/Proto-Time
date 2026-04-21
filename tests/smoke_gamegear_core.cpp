#include "cores/gamegear/GameGearMachine.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    BMMQ::GameGearMachine gg;
    const std::vector<uint8_t> rom = {0x00u, 0x00u, 0x00u, 0x00u};
    gg.loadRom(rom);
    // Step a few cycles to ensure no crash
    for (int i = 0; i < 10; ++i) gg.step();
    puts("Game Gear core basic smoke test passed.");
    return 0;
}
