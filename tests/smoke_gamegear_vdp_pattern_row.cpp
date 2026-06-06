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

    // Build a tile where the top row (row 0) has color codes 1..8 across pixels 0..7
    std::array<uint8_t, 32> tile{};
    const std::array<uint8_t, 8> codes = {1,2,3,4,5,6,7,8};
    uint8_t plane0 = 0;
    uint8_t plane1 = 0;
    uint8_t plane2 = 0;
    uint8_t plane3 = 0;
    for (std::size_t px = 0; px < 8u; ++px) {
        const auto c = codes[px];
        const auto bit = static_cast<uint8_t>(7u - static_cast<uint8_t>(px));
        if (c & 0x01u) plane0 |= static_cast<uint8_t>(1u << bit);
        if (c & 0x02u) plane1 |= static_cast<uint8_t>(1u << bit);
        if (c & 0x04u) plane2 |= static_cast<uint8_t>(1u << bit);
        if (c & 0x08u) plane3 |= static_cast<uint8_t>(1u << bit);
    }
    // row 0 planes
    tile[0] = plane0;
    tile[1] = plane1;
    tile[2] = plane2;
    tile[3] = plane3;
    // leave other rows zero

    writePattern(7u, tile);
    writeNameEntry(6u, 3u, static_cast<uint16_t>(7u));

    const auto model = vdp.buildFrameModel({160, 144});
    if (model.argbPixels.empty()) {
        std::cerr << "Empty frame model" << std::endl;
        return 1;
    }

    // Verify top-left 8 pixels map to CRAM entries for palette 0 and codes 1..8
    for (std::size_t px = 0; px < 8u; ++px) {
        const auto expected = vdp.debugDecodedCramColor(static_cast<std::size_t>(codes[px]));
        const auto got = model.argbPixels[px];
        if (got != expected) {
            std::cerr << "Pattern row mismatch at px=" << px << ": got " << std::hex << got
                      << " expected " << expected << std::dec << std::endl;
            return 1;
        }
    }

    // Horizontal flip: set H flip in name entry, expect reversed order
    writeNameEntry(6u, 3u, static_cast<uint16_t>(7u | 0x0200u));
    const auto modelH = vdp.buildFrameModel({160, 144});
    for (std::size_t px = 0; px < 8u; ++px) {
        const auto expected = vdp.debugDecodedCramColor(static_cast<std::size_t>(codes[7u - px]));
        const auto got = modelH.argbPixels[px];
        if (got != expected) {
            std::cerr << "Hflip mismatch at px=" << px << ": got " << std::hex << got
                      << " expected " << expected << std::dec << std::endl;
            return 1;
        }
    }

    // Vertical flip: write different data into row 7 and toggle V flip
    std::array<uint8_t, 32> tile2{};
    // set row7 planes to codes reversed (8..1)
    uint8_t p0 = 0, p1 = 0, p2 = 0, p3 = 0;
    const std::array<uint8_t, 8> codes7 = {8,7,6,5,4,3,2,1};
    for (std::size_t px = 0; px < 8u; ++px) {
        const auto c = codes7[px];
        const auto bit = static_cast<uint8_t>(7u - static_cast<uint8_t>(px));
        if (c & 0x01u) p0 |= static_cast<uint8_t>(1u << bit);
        if (c & 0x02u) p1 |= static_cast<uint8_t>(1u << bit);
        if (c & 0x04u) p2 |= static_cast<uint8_t>(1u << bit);
        if (c & 0x08u) p3 |= static_cast<uint8_t>(1u << bit);
    }
    // place into row 7
    tile2[7*4 + 0] = p0;
    tile2[7*4 + 1] = p1;
    tile2[7*4 + 2] = p2;
    tile2[7*4 + 3] = p3;
    writePattern(8u, tile2);
    writeNameEntry(6u, 3u, static_cast<uint16_t>(8u | 0x0400u)); // V flip set
    const auto modelV = vdp.buildFrameModel({160, 144});
    for (std::size_t px = 0; px < 8u; ++px) {
        const auto expected = vdp.debugDecodedCramColor(static_cast<std::size_t>(codes7[px]));
        const auto got = modelV.argbPixels[px];
        if (got != expected) {
            std::cerr << "Vflip mismatch at px=" << px << ": got " << std::hex << got
                      << " expected " << expected << std::dec << std::endl;
            return 1;
        }
    }

    return 0;
}
