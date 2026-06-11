#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    // H-counter accuracy (pending cycles -> 0..255 mapping)
    {
        GameGearVDP vdp;
        GameGearMemoryMap memory;
        memory.setVdp(&vdp);
        vdp.reset();

        // Turn display on (register 1 := 0x40)
        memory.writeIoPort(0xBFu, 0x40u);
        memory.writeIoPort(0xBFu, 0x81u);

        // Half scanline -> expected H counter = floor(114*256/228) = 128
        vdp.step(114u);
        assert(vdp.readHCounter() == 128u);

        // One more cycle -> increments to 129
        vdp.step(1u);
        assert(vdp.readHCounter() == 129u);

        // Complete the scanline -> back to 0
        vdp.step(113u);
        assert(vdp.readHCounter() == 0u);
    }

    // Game Gear mode keeps fixed 192-line timing even when SMS extended-height bits are set.
    {
        GameGearVDP vdp;
        GameGearMemoryMap memory;
        memory.setVdp(&vdp);
        vdp.reset();

        memory.writeIoPort(0xBFu, 0x06u);
        memory.writeIoPort(0xBFu, 0x80u); // reg0 = 0x06
        memory.writeIoPort(0xBFu, 0x50u);
        memory.writeIoPort(0xBFu, 0x81u); // reg1 = display on + SMS 224-line selector

        vdp.step(228u * 193u);
        assert(vdp.takeVBlankEntered());
        assert(vdp.readVCounter() == 193u);

        vdp.reset();
        memory.setVdp(&vdp);
        memory.writeIoPort(0xBFu, 0x06u);
        memory.writeIoPort(0xBFu, 0x80u); // reg0 = 0x06
        memory.writeIoPort(0xBFu, 0x48u);
        memory.writeIoPort(0xBFu, 0x81u); // reg1 = display on + SMS 240-line selector

        vdp.step(228u * 193u);
        assert(vdp.takeVBlankEntered());
        assert(vdp.readVCounter() == 193u);
    }

    // SMS compatibility mode still honors Mode4 vertical mapping (224 and 240 line modes).
    {
        GameGearVDP vdp;
        GameGearMemoryMap memory;
        memory.setVdp(&vdp);
        vdp.reset();
        vdp.setSmsMode(true);

        // Enable mode4 (reg0 bits 1+2) and set reg1 to select 224-line mode (m1=1,m3=0)
        memory.writeIoPort(0xBFu, 0x06u);
        memory.writeIoPort(0xBFu, 0x80u); // reg0 = 0x06
        memory.writeIoPort(0xBFu, 0x50u);
        memory.writeIoPort(0xBFu, 0x81u); // reg1 = 0x50 (display on + m1)

        // Advance to scanline 235 (0xEB) which is > 0xEA (234) so mapping subtracts 6
        const uint32_t linesToAdvance = 235u;
        for (uint32_t i = 0; i < linesToAdvance; ++i) {
            vdp.step(228u);
        }
        const auto vCounter224 = vdp.readVCounter();
        // For scanline 235 expected vCounter = 235 - 6 = 229
        assert(vCounter224 == 229u);

        // Now test 240-line mode (m1=0, m3=1)
        vdp.reset();
        vdp.setSmsMode(true);
        memory.setVdp(&vdp);
        memory.writeIoPort(0xBFu, 0x06u);
        memory.writeIoPort(0xBFu, 0x80u); // reg0 = 0x06
        memory.writeIoPort(0xBFu, 0x48u);
        memory.writeIoPort(0xBFu, 0x81u); // reg1 = 0x48 (display on + m3)

        // Advance to scanline 250 and verify vCounter equals scanline (mod 256)
        const uint32_t lines240 = 250u;
        for (uint32_t i = 0; i < lines240; ++i) {
            vdp.step(228u);
        }
        const auto vCounter240 = vdp.readVCounter();
        assert(vCounter240 == static_cast<uint8_t>(lines240 & 0xFFu));
    }

    // Sprite collision detection
    {
        GameGearVDP vdp;
        GameGearMemoryMap memory;
        memory.setVdp(&vdp);
        vdp.reset();

        // Turn display on
        memory.writeIoPort(0xBFu, 0x40u);
        memory.writeIoPort(0xBFu, 0x81u);

        // Write a simple filled tile into VRAM at the default sprite pattern base
        const uint16_t tileVramAddr = static_cast<uint16_t>(0x8000u + 0x2000u); // pattern base used by default
        for (uint16_t row = 0; row < 8u; ++row) {
            // plane0..3 bytes per row
            vdp.writeVram(tileVramAddr + row * 4u + 0u, 0xFFu);
            vdp.writeVram(tileVramAddr + row * 4u + 1u, 0xFFu);
            vdp.writeVram(tileVramAddr + row * 4u + 2u, 0xFFu);
            vdp.writeVram(tileVramAddr + row * 4u + 3u, 0xFFu);
        }

        // Configure SAT and sprite generator (use defaults set on reset)
        // Place two sprites at the same X/Y so they overlap and cause collision.
        // Sprite 0: Y=23 (appears on line 24), X=24, tile=0
        vdp.writeOam(0xFE00u + 0u, 23u); // Y
        vdp.writeOam(0xFE00u + 1u, 24u); // X low / tile low
        vdp.writeOam(0xFE00u + 2u, 0u);  // tile index

        // Sprite 1: same coordinates -> collision
        vdp.writeOam(0xFE04u + 0u, 23u);
        vdp.writeOam(0xFE04u + 1u, 24u);
        vdp.writeOam(0xFE04u + 2u, 0u);

        // Advance to the line where sprites appear
        for (uint32_t i = 0; i < 25u; ++i) {
            vdp.step(228u);
        }

        // Read status via control port; sprite collision flag is bit 0x20
        const auto status = memory.readIoPort(0xBFu);
        assert((status & 0x20u) != 0u);
    }

    return 0;
}
