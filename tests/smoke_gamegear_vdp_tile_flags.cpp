#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <array>
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

    auto writePattern = [&](std::size_t tileIndex, const std::array<uint8_t, 32>& data) {
        const uint16_t addr = static_cast<uint16_t>(0x8000u + tileIndex * 32u);
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(addr & 0x00FFu));
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(((addr >> 8u) & 0x3Fu) | 0x40u));
        for (int i = 0; i < 32; ++i) {
            memory.writeIoPort(0xBEu, data[static_cast<std::size_t>(i)]);
        }
    };

    auto writeNameEntry = [&](std::size_t tileX, std::size_t tileY, uint16_t entry) {
        const uint16_t nameBaseAddr = static_cast<uint16_t>(0x8000u + 0x3800u);
        const uint16_t entryOffset = static_cast<uint16_t>((tileY * 32u + tileX) * 2u);
        const uint16_t entryAddr = static_cast<uint16_t>(nameBaseAddr + entryOffset);
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(entryAddr & 0x00FFu));
        memory.writeIoPort(0xBFu, static_cast<uint8_t>(((entryAddr >> 8u) & 0x3Fu) | 0x40u));
        memory.writeIoPort(0xBEu, static_cast<uint8_t>(entry & 0x00FFu));
        memory.writeIoPort(0xBEu, static_cast<uint8_t>((entry >> 8u) & 0x00FFu));
    };

    // Test: Normal solid tile (all pixels set)
    {
        std::array<uint8_t, 32> solid{};
        solid.fill(0xFFu);
        writePattern(2u, solid);
        writeNameEntry(6u, 3u, static_cast<uint16_t>(2u));
        const auto model = vdp.buildFrameModel({160, 144});
        const uint32_t expected = vdp.debugDecodedCramColor(15u); // colorCode 0x0F -> CRAM index 15
        if (model.argbPixels.empty() || model.argbPixels[0] != expected) {
            std::cerr << "Solid tile mismatch: got " << std::hex << (model.argbPixels.empty() ? 0u : model.argbPixels[0])
                      << " expected " << expected << '\n';
            return 1;
        }
    }

    // Test: Horizontal flip
    {
        // Pattern with only the left column set (bit 7 in each row's plane0)
        std::array<uint8_t, 32> leftCol{};
        for (int row = 0; row < 8; ++row) {
            const std::size_t base = static_cast<std::size_t>(row * 4);
            leftCol[base + 0] = 0x80u; // plane0: bit 7 set => pixelX==0
            leftCol[base + 1] = 0x00u;
            leftCol[base + 2] = 0x00u;
            leftCol[base + 3] = 0x00u;
        }
        writePattern(3u, leftCol);
        // No flip: top-left should show the single-column pixel
        writeNameEntry(6u, 3u, static_cast<uint16_t>(3u));
        auto model = vdp.buildFrameModel({160, 144});
        const uint32_t expectedOn = vdp.debugDecodedCramColor(1u);
        if (model.argbPixels.empty() || model.argbPixels[0] != expectedOn) {
            std::cerr << "Hflip baseline mismatch: got " << std::hex << (model.argbPixels.empty() ? 0u : model.argbPixels[0])
                      << " expected " << expectedOn << '\n';
            return 1;
        }

        // With horizontal flip set, the highlighted column moves to the far right -> top-left should be background (CRAM idx 0)
        writeNameEntry(6u, 3u, static_cast<uint16_t>(3u | 0x0200u));
        model = vdp.buildFrameModel({160, 144});
        const uint32_t expectedOff = vdp.debugDecodedCramColor(0u);
        if (model.argbPixels.empty() || model.argbPixels[0] != expectedOff) {
            std::cerr << "Hflip applied mismatch: got " << std::hex << (model.argbPixels.empty() ? 0u : model.argbPixels[0])
                      << " expected " << expectedOff << '\n';
            return 1;
        }
    }

    // Test: Vertical flip
    {
        // Pattern with only the top row set (row 0 bytes non-zero)
        std::array<uint8_t, 32> topRow{};
        // Row 0: set all planes so colorCode != 0
        topRow[0] = 0xFFu;
        topRow[1] = 0xFFu;
        topRow[2] = 0xFFu;
        topRow[3] = 0xFFu;
        // Remaining rows are zero
        writePattern(4u, topRow);
        // No flip: top-left should show the top-row pixel
        writeNameEntry(6u, 3u, static_cast<uint16_t>(4u));
        auto model = vdp.buildFrameModel({160, 144});
        const uint32_t expectedOn = vdp.debugDecodedCramColor(15u & 0x0Fu); // row uses full planes -> colorCode 0x0F
        if (model.argbPixels.empty() || model.argbPixels[0] != expectedOn) {
            std::cerr << "Vflip baseline mismatch: got " << std::hex << (model.argbPixels.empty() ? 0u : model.argbPixels[0])
                      << " expected " << expectedOn << '\n';
            return 1;
        }

        // With vertical flip set, the top row moves to bottom -> top-left should be background (CRAM idx 0)
        writeNameEntry(6u, 3u, static_cast<uint16_t>(4u | 0x0400u));
        model = vdp.buildFrameModel({160, 144});
        const uint32_t expectedOff = vdp.debugDecodedCramColor(0u);
        if (model.argbPixels.empty() || model.argbPixels[0] != expectedOff) {
            std::cerr << "Vflip applied mismatch: got " << std::hex << (model.argbPixels.empty() ? 0u : model.argbPixels[0])
                      << " expected " << expectedOff << '\n';
            return 1;
        }
    }

    // Test: Palette select (palette1 toggles CRAM bank 0/1 -> indices 0..15 vs 16..31)
    {
        std::array<uint8_t, 32> solid{};
        solid.fill(0xFFu);
        writePattern(5u, solid);
        // Palette 0
        writeNameEntry(6u, 3u, static_cast<uint16_t>(5u));
        auto model = vdp.buildFrameModel({160, 144});
        const uint32_t expectedPal0 = vdp.debugDecodedCramColor(15u);
        if (model.argbPixels.empty() || model.argbPixels[0] != expectedPal0) {
            std::cerr << "Palette0 mismatch: got " << std::hex << (model.argbPixels.empty() ? 0u : model.argbPixels[0])
                      << " expected " << expectedPal0 << '\n';
            return 1;
        }
        // Palette 1
        writeNameEntry(6u, 3u, static_cast<uint16_t>(5u | 0x0800u));
        model = vdp.buildFrameModel({160, 144});
        const uint32_t expectedPal1 = vdp.debugDecodedCramColor(31u);
        if (model.argbPixels.empty() || model.argbPixels[0] != expectedPal1) {
            std::cerr << "Palette1 mismatch: got " << std::hex << (model.argbPixels.empty() ? 0u : model.argbPixels[0])
                      << " expected " << expectedPal1 << '\n';
            return 1;
        }
    }

    return 0;
}
