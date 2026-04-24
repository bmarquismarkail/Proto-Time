#include "cores/gamegear/GameGearMachine.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

bool stepUntil(BMMQ::GameGearMachine& machine, int maxSteps, auto&& predicate) {
    for (int i = 0; i < maxSteps; ++i) {
        machine.step();
        if (predicate()) return true;
    }
    return false;
}

} // namespace

int main() {
    // Build a small ROM with an interrupt handler at 0x0038 that writes 0x99
    std::vector<uint8_t> rom(0x4000u, 0x00u);

    // Startup: enable VDP display via register write sequence (OUT (0xBF) twice)
    // LD A,0x60; OUT (0xBF),A; LD A,0x81; OUT (0xBF),A
    size_t pc = 0;
    rom[pc++] = 0x3Eu; rom[pc++] = 0x60u; // LD A,0x60: display + frame IRQ enable
    rom[pc++] = 0xD3u; rom[pc++] = 0xBFu; // OUT (0xBF),A
    rom[pc++] = 0x3Eu; rom[pc++] = 0x81u; // LD A,0x81
    rom[pc++] = 0xD3u; rom[pc++] = 0xBFu; // OUT (0xBF),A

    // Enable interrupts
    rom[pc++] = 0xFBu; // EI

    // Tight loop to generate CPU cycles while waiting for VBlank
    rom[pc++] = 0x00u; // NOP
    rom[pc++] = 0x18u; rom[pc++] = 0xFEu; // JR -2 (loop to the NOP)

    // Interrupt service routine at 0x0038: LD HL,0xC100; LD (HL),0x99
    const size_t isr = 0x0038u;
    rom[isr + 0x00] = 0x21u; rom[isr + 0x01] = 0x00u; rom[isr + 0x02] = 0xC1u; // LD HL,0xC100
    rom[isr + 0x03] = 0x36u; rom[isr + 0x04] = 0x99u;                             // LD (HL),0x99
    // Hang at ISR
    rom[isr + 0x05] = 0xC3u; rom[isr + 0x06] = 0x38u; rom[isr + 0x07] = 0x00u;     // JP 0x0038

    BMMQ::GameGearMachine gg;
    gg.loadRom(rom);

    // Step until ISR writes sentinel to 0xC100
    const bool sawIsr = stepUntil(gg, 30000, [&] {
        return gg.runtimeContext().read8(0xC100u) == 0x99u;
    });
    assert(sawIsr && "VBlank IRQ handler did not run");

    return 0;
}
