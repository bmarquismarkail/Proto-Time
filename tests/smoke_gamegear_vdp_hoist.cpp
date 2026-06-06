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

    // Turn display on
    memory.writeIoPort(0xBFu, 0x40u);
    memory.writeIoPort(0xBFu, 0x81u);

    // Write a solid pattern into tile 0 (pattern base = 0x0000 by default)
    // VRAM addr 0x8000 corresponds to pattern 0
    memory.writeIoPort(0xBFu, 0x00u);
    memory.writeIoPort(0xBFu, 0x40u); // VRAM write, address 0x8000
    for (int i = 0; i < 32; ++i) {
        memory.writeIoPort(0xBEu, 0xFFu);
    }

    // Place that tile into the name table at VDP coordinate (48,24) which maps
    // to LCD (0,0) for a 160x144 frame (viewport X=48, Y=24). Tile coords:
    // tileX = 48/8 = 6, tileY = 24/8 = 3
    const std::size_t tileX = 6u;
    const std::size_t tileY = 3u;
    // Default name table base after reset is 0x3800 -> VRAM address = 0x8000 + 0x3800 = 0xB800
    const uint16_t nameBaseAddr = static_cast<uint16_t>(0x8000u + 0x3800u);
    const uint16_t entryOffset = static_cast<uint16_t>((tileY * 32u + tileX) * 2u);
    const uint16_t entryAddr = static_cast<uint16_t>(nameBaseAddr + entryOffset);

    // Set VRAM address to entryAddr for VRAM write
    memory.writeIoPort(0xBFu, static_cast<uint8_t>(entryAddr & 0x00FFu));
    memory.writeIoPort(0xBFu, static_cast<uint8_t>(((entryAddr >> 8u) & 0x3Fu) | 0x40u));
    // Write low then high bytes (tile index = 0)
    memory.writeIoPort(0xBEu, 0x00u);
    memory.writeIoPort(0xBEu, 0x00u);

    // Build the frame model and verify top-left pixel equals expected CRAM color
    const auto model = vdp.buildFrameModel({160, 144});
    if (model.argbPixels.empty()) {
        std::cerr << "Empty frame model" << '\n';
        return 1;
    }

    // The solid pattern produces color code 0x0F (all planes set) -> CRAM index 15 for palette 0
    const uint32_t expected = vdp.debugDecodedCramColor(15u);
    if (model.argbPixels[0] != expected) {
        std::cerr << "Frame mismatch: got " << std::hex << model.argbPixels[0]
                  << " expected " << expected << '\n';
        return 1;
    }

    return 0;
}
