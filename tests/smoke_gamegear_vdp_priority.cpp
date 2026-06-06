#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <array>
#include <cstdint>
#include <iostream>

int main() {
    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    // Enable display
    memory.writeIoPort(0xBFu, 0x40u);
    memory.writeIoPort(0xBFu, 0x81u);

    const auto writeVramBytes = [&](uint16_t addr, const uint8_t* data, std::size_t len) {
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(addr & 0x00FFu));
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(((addr >> 8u) & 0x3Fu) | 0x40u));
        for (std::size_t i = 0; i < len; ++i) memory.writeIoPort(0xBEu, data[i]);
    };

    const auto writeBgPattern = [&](std::size_t tileIndex, const std::array<uint8_t, 32>& data) {
        const uint16_t addr = static_cast<uint16_t>(0x8000u + tileIndex * 32u);
        writeVramBytes(addr, data.data(), data.size());
    };

    const auto writeNameEntry = [&](std::size_t tileX, std::size_t tileY, uint16_t entry) {
        const uint16_t nameBaseAddr = static_cast<uint16_t>(0x8000u + 0x3800u);
        const uint16_t entryOffset = static_cast<uint16_t>((tileY * 32u + tileX) * 2u);
        const uint16_t entryAddr = static_cast<uint16_t>(nameBaseAddr + entryOffset);
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(entryAddr & 0x00FFu));
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(((entryAddr >> 8u) & 0x3Fu) | 0x40u));
        memory.writeIoPort(0xBEu, static_cast<uint8_t>(entry & 0x00FFu));
        memory.writeIoPort(0xBEu, static_cast<uint8_t>((entry >> 8u) & 0x00FFu));
    };

    const auto writeSpritePattern = [&](std::size_t spriteTileIndex, const std::array<uint8_t, 32>& data) {
        const uint16_t patternBase = static_cast<uint16_t>(0x8000u + 0x2000u + spriteTileIndex * 32u);
        writeVramBytes(patternBase, data.data(), data.size());
    };

    const auto writeSatY = [&](std::size_t satBase, std::size_t spriteIndex, uint8_t y) {
        const uint16_t addr = static_cast<uint16_t>(0x8000u + satBase + spriteIndex);
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(addr & 0x00FFu));
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(((addr >> 8u) & 0x3Fu) | 0x40u));
        memory.writeIoPort(0xBEu, y);
    };

    const auto writeSatXTile = [&](std::size_t satBase, std::size_t spriteIndex, uint8_t x, uint8_t tile) {
        const uint16_t addr = static_cast<uint16_t>(0x8000u + satBase + 0x80u + spriteIndex * 2u);
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(addr & 0x00FFu));
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(((addr >> 8u) & 0x3Fu) | 0x40u));
        memory.writeIoPort(0xBEu, x);
        memory.writeIoPort(0xBEu, tile);
    };

    // Test A: Non-transparent background with priority should override sprite
    {
        // background tile: left-most pixel colorCode=1
        std::array<uint8_t, 32> bgTile{};
        bgTile[0] = static_cast<uint8_t>(1u << 7u);
        writeBgPattern(20u, bgTile);
        // set name entry with priority bit
        writeNameEntry(6u, 3u, static_cast<uint16_t>(20u | 0x1000u));

        // sprite pattern: left-most pixel colorCode=1
        std::array<uint8_t, 32> sTile{};
        sTile[0] = static_cast<uint8_t>(1u << 7u);
        writeSpritePattern(5u, sTile);

        // place sprite0 at top-left (screen 0,0): rawY = viewportY - 1 = 23; X = viewportX = 48
        writeSatY(0x3F00u, 0u, 23u);
        writeSatXTile(0x3F00u, 0u, 48u, 5u);

        const auto model = vdp.buildFrameModel({160, 144});
        const auto got = model.argbPixels[0u];
        const auto bgColor = vdp.debugDecodedCramColor(1u);
        const auto spriteColor = vdp.debugDecodedCramColor(16u + 1u);
        if (got != bgColor) {
            std::cerr << "FAIL: background priority did not override sprite; got 0x" << std::hex << got
                      << " bg=0x" << bgColor << " sprite=0x" << spriteColor << std::dec << std::endl;
            return 1;
        }
    }

    // Test B: Transparent background color (0) with priority should NOT hide sprite
    {
        std::array<uint8_t, 32> tileZero{}; // all zeros -> colorCode==0
        writeBgPattern(21u, tileZero);
        writeNameEntry(6u, 3u, static_cast<uint16_t>(21u | 0x1000u));

        const auto model = vdp.buildFrameModel({160, 144});
        const auto got = model.argbPixels[0u];
        const auto spriteColor = vdp.debugDecodedCramColor(16u + 1u);
        if (got != spriteColor) {
            std::cerr << "FAIL: transparent background with priority hid sprite; got 0x" << std::hex << got
                      << " expected sprite 0x" << spriteColor << std::dec << std::endl;
            return 1;
        }
    }

    return 0;
}
