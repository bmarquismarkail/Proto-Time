#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <cassert>
#include <cstdint>

int main() {
    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    // Baseline decoded color for palette entry 0
    const auto before = vdp.debugDecodedCramColor(0u);

    // Select CRAM address 0x00 for write (low then control byte selects CRAM write)
    memory.writeIoPort(0xBFu, 0x00u);
    memory.writeIoPort(0xBFu, 0xC0u); // CRAM write, addr 0x00

    // Even write: latch only (should not modify decoded cache yet)
    memory.writeIoPort(0xBEu, 0x12u);
    assert(vdp.debugDecodedCramColor(0u) == before);

    // Odd write: commits latched even byte and odd byte -> cache should update
    memory.writeIoPort(0xBEu, 0x34u);
    // Reconstruct cramWord as implementation does
    const uint8_t even = 0x12u;
    const uint8_t odd = 0x34u;
    const uint16_t cramWord = static_cast<uint16_t>((even & 0x0Fu) |
                                                   ((even & 0xF0u) << 0u) |
                                                   ((odd & 0x0Fu) << 8u));
    const auto red = static_cast<uint8_t>((cramWord & 0x000Fu) * 17u);
    const auto green = static_cast<uint8_t>(((cramWord >> 4u) & 0x000Fu) * 17u);
    const auto blue = static_cast<uint8_t>(((cramWord >> 8u) & 0x000Fu) * 17u);
    const uint32_t expected = 0xFF000000u |
                              (static_cast<uint32_t>(red) << 16u) |
                              (static_cast<uint32_t>(green) << 8u) |
                              static_cast<uint32_t>(blue);
    assert(vdp.debugDecodedCramColor(0u) == expected);

    // Now test SMS (TMS) mode single-byte CRAM updates
    vdp.reset();
    vdp.setSmsMode(true);

    // Select CRAM addr 0x00 and write the SMS color byte 0x3F
    memory.writeIoPort(0xBFu, 0x00u);
    memory.writeIoPort(0xBFu, 0xC0u);
    memory.writeIoPort(0xBEu, 0x3Fu);

    const uint8_t smsColor = static_cast<uint8_t>(0x3Fu & 0x3Fu);
    const auto sred = static_cast<uint8_t>((smsColor & 0x03u) * 85u);
    const auto sgreen = static_cast<uint8_t>(((smsColor >> 2u) & 0x03u) * 85u);
    const auto sblue = static_cast<uint8_t>(((smsColor >> 4u) & 0x03u) * 85u);
    const uint32_t sexpected = 0xFF000000u |
                               (static_cast<uint32_t>(sred) << 16u) |
                               (static_cast<uint32_t>(sgreen) << 8u) |
                               static_cast<uint32_t>(sblue);
    assert(vdp.debugDecodedCramColor(0u) == sexpected);

    return 0;
}
