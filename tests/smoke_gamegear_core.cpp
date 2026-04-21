#include "cores/gamegear/GameGearMachine.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/Z80Interpreter.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

int main() {
    BMMQ::GameGearMachine gg;
    const std::vector<uint8_t> rom = {0x00u, 0x00u, 0x00u, 0x00u};
    gg.loadRom(rom);
    // Step a few cycles to ensure no crash
    for (int i = 0; i < 10; ++i) gg.step();

    GameGearMemoryMap memory;
    const std::vector<uint8_t> mappedRom(0x8000, 0xAAu);
    memory.mapRom(mappedRom.data(), mappedRom.size());
    assert(memory.read(0x00DC) == 0xFFu);
    assert(memory.read(0x7F00) == 0x00u);

    Z80Interpreter cpu;
    assert(cpu.AF == 0u);
    assert(cpu.BC == 0u);
    assert(cpu.DE == 0u);
    assert(cpu.HL == 0u);
    assert(cpu.IX == 0u);
    assert(cpu.IY == 0u);
    assert(cpu.SP == 0u);
    assert(cpu.PC == 0u);
    assert(cpu.AF_ == 0u);
    assert(cpu.BC_ == 0u);
    assert(cpu.DE_ == 0u);
    assert(cpu.HL_ == 0u);
    assert(cpu.I == 0u);
    assert(cpu.R == 0u);
    assert(!cpu.IFF1);
    assert(!cpu.IFF2);
    assert(!cpu.IME);

    bool threwMissingMemoryInterface = false;
    try {
        cpu.step();
    } catch (const std::runtime_error& ex) {
        threwMissingMemoryInterface = std::string_view(ex.what()) == "Z80 memory interface not set";
    }
    assert(threwMissingMemoryInterface);

    puts("Game Gear core basic smoke test passed.");
    return 0;
}
