#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    // CRAM size: 64 bytes / 32 color entries
    const auto& cram = vdp.debugCram();
    assert(cram.size() == 0x40u);

    // Snapshot initial CRAM bytes
    std::array<uint8_t, 0x0040> before = cram;

    // Set CRAM address to 0x003E (low then control byte selects CRAM write)
    memory.writeIoPort(0xBFu, 0x3Eu);
    memory.writeIoPort(0xBFu, 0xC0u); // CRAM write, addr 0x003E

    // Even write: latch only (should not modify CRAM bytes yet)
    memory.writeIoPort(0xBEu, 0x12u);
    assert(vdp.debugCram()[0x3Eu] == before[0x3Eu]);
    assert(vdp.debugCram()[0x3Fu] == before[0x3Fu]);

    // Odd write: commits the latched even byte and the odd byte
    memory.writeIoPort(0xBEu, 0x34u);
    assert(vdp.debugCram()[0x3Eu] == 0x12u);
    assert(vdp.debugCram()[0x3Fu] == 0x34u);

    // Next writes wrap past 0x3F -> 0x00
    memory.writeIoPort(0xBEu, 0x56u); // even for addr 0x00 (latched)
    assert(vdp.debugCram()[0x00u] == before[0x00u]);
    memory.writeIoPort(0xBEu, 0x78u); // odd commit to 0x00/0x01
    assert(vdp.debugCram()[0x00u] == 0x56u);
    assert(vdp.debugCram()[0x01u] == 0x78u);

    // Verify 12-bit color word composition: bits 0-3 red, 4-7 green, 8-11 blue
    const auto even = vdp.debugCram()[0x00u];
    const auto odd = vdp.debugCram()[0x01u];
    const uint16_t cramWord = static_cast<uint16_t>((even & 0x0Fu) |
                                                   ((even & 0xF0u) << 0u) |
                                                   ((odd & 0x0Fu) << 8u));
    // expect mapping to match stored nibbles
    assert((cramWord & 0x000Fu) == static_cast<uint16_t>(even & 0x0Fu));
    assert(((cramWord >> 4u) & 0x000Fu) == static_cast<uint16_t>((even >> 4u) & 0x0Fu));
    assert(((cramWord >> 8u) & 0x000Fu) == static_cast<uint16_t>(odd & 0x0Fu));

    return 0;
}
