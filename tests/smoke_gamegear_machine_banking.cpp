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
    // Prepare a 3-bank ROM (3 * 16KiB)
    const std::size_t page = 0x4000u;
    std::vector<uint8_t> rom(page * 3u, 0x00u);

    // Bank 0 (offset 0): JP 0x4000
    rom[0x0000] = 0xC3u; rom[0x0001] = 0x00u; rom[0x0002] = 0x40u; // JP 0x4000

    // Bank 0 continuation at 0x0010: write bank register 1 <- 2, then JP 0x4000
    const std::size_t p0_addr = 0x0010u;
    rom[p0_addr + 0x00] = 0x21u; rom[p0_addr + 0x01] = 0xFEu; rom[p0_addr + 0x02] = 0xFFu; // LD HL,0xFFFE
    rom[p0_addr + 0x03] = 0x3Eu; rom[p0_addr + 0x04] = 0x02u;                               // LD A,0x02
    rom[p0_addr + 0x05] = 0x77u;                                                         // LD (HL),A
    rom[p0_addr + 0x06] = 0xC3u; rom[p0_addr + 0x07] = 0x00u; rom[p0_addr + 0x08] = 0x40u; // JP 0x4000

    // Bank 1 (second 16KiB) at base page*1 + 0x0000: will write 0x11 to 0xC001 and return to 0x0010
    const std::size_t bank1_base = page * 1u;
    rom[bank1_base + 0x0000] = 0x21u; rom[bank1_base + 0x0001] = 0x01u; rom[bank1_base + 0x0002] = 0xC0u; // LD HL,0xC001
    rom[bank1_base + 0x0003] = 0x3Eu; rom[bank1_base + 0x0004] = 0x11u;                                 // LD A,0x11
    rom[bank1_base + 0x0005] = 0x77u;                                                                     // LD (HL),A
    rom[bank1_base + 0x0006] = 0xC3u; rom[bank1_base + 0x0007] = 0x10u; rom[bank1_base + 0x0008] = 0x00u; // JP 0x0010

    // Bank 2 (third 16KiB) at base page*2 + 0x0000: write 0x22 to 0xC002 then jump back to 0x0010
    const std::size_t bank2_base = page * 2u;
    rom[bank2_base + 0x0000] = 0x21u; rom[bank2_base + 0x0001] = 0x02u; rom[bank2_base + 0x0002] = 0xC0u; // LD HL,0xC002
    rom[bank2_base + 0x0003] = 0x3Eu; rom[bank2_base + 0x0004] = 0x22u;                                 // LD A,0x22
    rom[bank2_base + 0x0005] = 0x77u;                                                                     // LD (HL),A
    rom[bank2_base + 0x0006] = 0xC3u; rom[bank2_base + 0x0007] = 0x10u; rom[bank2_base + 0x0008] = 0x00u; // JP 0x0010

    // Final check area in bank0 at 0x0025: NOPs (will not be executed as part of this test beyond the JP targets)

    BMMQ::GameGearMachine gg;
    gg.loadRom(rom);

    // Quick sanity checks: ROM content should be visible at CPU addresses
    assert(gg.runtimeContext().read8(0x0000u) == 0xC3u);
    assert(gg.runtimeContext().read8(0x4000u) == 0x21u);

    // Ensure CPU actually steps from PC=0x0000 to PC=0x4000 after one instruction
    assert(gg.readRegisterPair("PC") == 0x0000u);
    gg.step();
    assert(gg.readRegisterPair("PC") == 0x4000u);

    // Step until bank1's write completes (0xC001 == 0x11)
    const bool seen1 = stepUntil(gg, 10000, [&] {
        return gg.runtimeContext().read8(0xC001u) == 0x11u;
    });
    assert(seen1 && "Bank1 code did not run as expected");

    // Step until bank2's write completes (0xC002 == 0x22)
    const bool seen2 = stepUntil(gg, 10000, [&] {
        return gg.runtimeContext().read8(0xC002u) == 0x22u;
    });
    assert(seen2 && "Bank2 code did not run after bank register write");

    return 0;
}
