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

}

int main() {
    // 16KiB ROM
    const std::size_t romSize = 0x4000u;
    std::vector<uint8_t> rom(romSize, 0x00u);

    // Main at 0x0000: LD HL,0xC200 ; LD A,0x11 ; EI ; loop
    size_t pc = 0;
    rom[pc++] = 0x21u; rom[pc++] = 0x00u; rom[pc++] = 0xC2u; // LD HL,0xC200
    // Enable VDP display and frame IRQ: LD A,0x60; OUT (0xBF),A; LD A,0x81; OUT (0xBF),A
    rom[pc++] = 0x3Eu; rom[pc++] = 0x60u; // LD A,0x60
    rom[pc++] = 0xD3u; rom[pc++] = 0xBFu; // OUT (0xBF),A
    rom[pc++] = 0x3Eu; rom[pc++] = 0x81u; // LD A,0x81
    rom[pc++] = 0xD3u; rom[pc++] = 0xBFu; // OUT (0xBF),A
    rom[pc++] = 0x3Eu; rom[pc++] = 0x11u;                   // LD A,0x11
    rom[pc++] = 0xFBu;                                      // EI
    rom[pc++] = 0x00u;                                      // NOP
    rom[pc++] = 0x18u; rom[pc++] = 0xFEu;                   // JR -2 (loop)

    // ISR at 0x0038: LD (HL),A ; INC A ; INC HL ; EI ; ED 4D (RETI)
    const size_t isr = 0x0038u;
    rom[isr + 0x00] = 0x77u; // LD (HL),A
    rom[isr + 0x01] = 0x3Cu; // INC A
    rom[isr + 0x02] = 0x23u; // INC HL
    rom[isr + 0x03] = 0xFBu; // EI (deferred)
    rom[isr + 0x04] = 0xEDu; rom[isr + 0x05] = 0x4Du; // RETI

    BMMQ::GameGearMachine gg;
    gg.loadRom(rom);
    // Wait for the ISR to execute once (write 0x11 to 0xC200)
    const bool sawFirst = stepUntil(gg, 200000, [&] {
        return gg.runtimeContext().read8(0xC200u) == 0x11u;
    });
        assert(sawFirst && "ISR did not run at least once");
    // Wait until control returns from the ISR (PC != 0x0038) and IME is set
    const bool returnedAndIme = stepUntil(gg, 200000, [&] {
        return gg.cpuInterruptsEnabled() && gg.readRegisterPair("PC") != 0x0038u;
    });
    assert(returnedAndIme && "IME not re-enabled after EI+RETI sequence or PC did not return from ISR");
    return 0;
}
