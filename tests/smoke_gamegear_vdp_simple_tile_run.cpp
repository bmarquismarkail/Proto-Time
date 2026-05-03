#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

void writeRegister(GameGearMemoryMap& memory, uint8_t index, uint8_t value)
{
    memory.writeIoPort(0xBFu, value);
    memory.writeIoPort(0xBFu, static_cast<uint8_t>(0x80u | index));
}

void writeNameEntry(GameGearVDP& vdp, std::size_t tileX, std::size_t tileY, uint16_t entry)
{
    const auto entryAddress = static_cast<uint16_t>(0x8000u + 0x3800u + (tileY * 32u + tileX) * 2u);
    vdp.writeVram(entryAddress, static_cast<uint8_t>(entry & 0x00FFu));
    vdp.writeVram(static_cast<uint16_t>(entryAddress + 1u), static_cast<uint8_t>((entry >> 8u) & 0x00FFu));
}

uint8_t tileColorCode(std::size_t tileIndex, std::size_t pixelX, std::size_t pixelY)
{
    return static_cast<uint8_t>(((tileIndex + pixelX + pixelY) % 3u) + 1u);
}

std::size_t tileIndexFor(std::size_t tileX, std::size_t tileY)
{
    return ((tileY * 32u + tileX) % 96u) + 1u;
}

void writePatternTile(GameGearVDP& vdp, std::size_t tileIndex)
{
    const auto baseAddress = static_cast<uint16_t>(0x8000u + tileIndex * 32u);
    for (std::size_t row = 0; row < 8u; ++row) {
        uint8_t plane0 = 0u;
        uint8_t plane1 = 0u;
        uint8_t plane2 = 0u;
        uint8_t plane3 = 0u;
        for (std::size_t pixel = 0u; pixel < 8u; ++pixel) {
            const auto colorCode = tileColorCode(tileIndex, pixel, row);
            const auto bit = static_cast<uint8_t>(7u - static_cast<uint8_t>(pixel));
            if ((colorCode & 0x01u) != 0u) plane0 |= static_cast<uint8_t>(1u << bit);
            if ((colorCode & 0x02u) != 0u) plane1 |= static_cast<uint8_t>(1u << bit);
            if ((colorCode & 0x04u) != 0u) plane2 |= static_cast<uint8_t>(1u << bit);
            if ((colorCode & 0x08u) != 0u) plane3 |= static_cast<uint8_t>(1u << bit);
        }
        const auto rowAddress = static_cast<uint16_t>(baseAddress + row * 4u);
        vdp.writeVram(rowAddress, plane0);
        vdp.writeVram(static_cast<uint16_t>(rowAddress + 1u), plane1);
        vdp.writeVram(static_cast<uint16_t>(rowAddress + 2u), plane2);
        vdp.writeVram(static_cast<uint16_t>(rowAddress + 3u), plane3);
    }
}

void fillSimpleTileMap(GameGearVDP& vdp)
{
    std::array<bool, 128u> writtenPatterns{};
    for (std::size_t tileY = 0u; tileY < 32u; ++tileY) {
        for (std::size_t tileX = 0u; tileX < 32u; ++tileX) {
            const auto tileIndex = tileIndexFor(tileX, tileY);
            if (!writtenPatterns[tileIndex]) {
                writePatternTile(vdp, tileIndex);
                writtenPatterns[tileIndex] = true;
            }
            writeNameEntry(vdp, tileX, tileY, static_cast<uint16_t>(tileIndex));
        }
    }
}

uint32_t expectedSimplePixel(const GameGearVDP& vdp,
                             int screenX,
                             int screenY,
                             uint8_t scrollX,
                             uint8_t scrollY,
                             bool palette1 = false)
{
    const auto vdpX = static_cast<std::size_t>(screenX + 48);
    const auto vdpY = static_cast<std::size_t>(screenY + 24);
    const auto startingColumn = static_cast<std::size_t>((32u - (scrollX >> 3u)) & 0x1Fu);
    const auto scrolledX = vdpX & 0xFFu;
    const auto tileX = (startingColumn + (scrolledX / 8u)) & 0x1Fu;
    const auto pixelX = scrolledX & 0x07u;
    const auto scrolledY = (vdpY + scrollY) & 0xFFu;
    const auto tileY = scrolledY / 8u;
    const auto pixelY = scrolledY & 0x07u;
    const auto tileIndex = tileIndexFor(tileX, tileY % 32u);
    const auto colorCode = tileColorCode(tileIndex, pixelX, pixelY);
    return vdp.debugDecodedCramColor((palette1 ? 16u : 0u) + colorCode);
}

void writeScrollY(GameGearVDP& vdp, GameGearMemoryMap& memory, uint8_t value)
{
    const auto currentLine = vdp.currentScanline();
    if (currentLine <= 192u) {
        vdp.step(228u * static_cast<uint32_t>(193u - currentLine));
    }
    writeRegister(memory, 9u, value);
}

} // namespace

int main()
{
    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    writeRegister(memory, 1u, 0x40u); // display on
    fillSimpleTileMap(vdp);

    if (!vdp.debugMode4SimpleBackgroundPathEligible()) {
        return fail("simple tile-run path should be eligible for native aligned scrolling");
    }

    const auto baseModel = vdp.buildFrameModel({160, 144});
    if (baseModel.argbPixels.empty()) {
        return fail("simple tile-run base frame unexpectedly empty");
    }
    if (baseModel.argbPixels[0] != expectedSimplePixel(vdp, 0, 0, 0u, 0u)) {
        return fail("simple tile-run top-left pixel did not match expected tile sample");
    }
    if (baseModel.argbPixels[7] != expectedSimplePixel(vdp, 7, 0, 0u, 0u)) {
        return fail("simple tile-run partial first tile did not preserve pixel order");
    }
    if (baseModel.argbPixels[8] != expectedSimplePixel(vdp, 8, 0, 0u, 0u)) {
        return fail("simple tile-run tile boundary did not advance to next tile");
    }
    if (baseModel.argbPixels[8u * 160u] != expectedSimplePixel(vdp, 0, 8, 0u, 0u)) {
        return fail("simple tile-run viewport Y offset did not sample expected tile row");
    }

    writeRegister(memory, 8u, 0x08u);
    if (!vdp.debugMode4SimpleBackgroundPathEligible()) {
        return fail("simple tile-run path should remain eligible for aligned scrollX");
    }
    const auto scrollXModel = vdp.buildFrameModel({160, 144});
    if (scrollXModel.argbPixels[0] != expectedSimplePixel(vdp, 0, 0, 0x08u, 0u)) {
        return fail("simple tile-run aligned scrollX did not sample expected tile");
    }

    writeRegister(memory, 8u, 0x00u);
    writeScrollY(vdp, memory, 0x01u);
    if (!vdp.debugMode4SimpleBackgroundPathEligible()) {
        return fail("simple tile-run path should remain eligible after scrollY latch update");
    }
    const auto scrollYModel = vdp.buildFrameModel({160, 144});
    if (scrollYModel.argbPixels[0] != expectedSimplePixel(vdp, 0, 0, 0u, 0x01u)) {
        return fail("simple tile-run scrollY did not sample expected pattern row");
    }

    writeScrollY(vdp, memory, 0x00u);
    const auto paletteTile = tileIndexFor(6u, 3u);
    writeNameEntry(vdp, 6u, 3u, static_cast<uint16_t>(paletteTile | 0x0800u));
    const auto paletteModel = vdp.buildFrameModel({160, 144});
    if (paletteModel.argbPixels[0] != expectedSimplePixel(vdp, 0, 0, 0u, 0u, true)) {
        return fail("simple tile-run palette select did not use sprite/background palette bank");
    }
    writeNameEntry(vdp, 6u, 3u, static_cast<uint16_t>(paletteTile));

    std::array<uint8_t, 32u> spritePattern{};
    spritePattern[0] = 0x80u;
    for (std::size_t i = 0u; i < spritePattern.size(); ++i) {
        vdp.writeVram(static_cast<uint16_t>(0xA000u + 5u * 32u + i), spritePattern[i]);
    }
    vdp.writeVram(0xBF00u, 23u);
    vdp.writeVram(0xBF80u, 48u);
    vdp.writeVram(0xBF81u, 5u);

    std::array<uint8_t, 32u> priorityPattern{};
    priorityPattern[0] = 0x80u;
    for (std::size_t i = 0u; i < priorityPattern.size(); ++i) {
        vdp.writeVram(static_cast<uint16_t>(0x8000u + 120u * 32u + i), priorityPattern[i]);
    }
    writeNameEntry(vdp, 6u, 3u, static_cast<uint16_t>(120u | 0x1000u));
    const auto priorityModel = vdp.buildFrameModel({160, 144});
    if (priorityModel.argbPixels[0] != vdp.debugDecodedCramColor(1u)) {
        return fail("simple tile-run background priority did not mask sprite pixel");
    }
    std::array<uint8_t, 32u> transparentPriority{};
    for (std::size_t i = 0u; i < transparentPriority.size(); ++i) {
        vdp.writeVram(static_cast<uint16_t>(0x8000u + 121u * 32u + i), transparentPriority[i]);
    }
    writeNameEntry(vdp, 6u, 3u, static_cast<uint16_t>(121u | 0x1000u));
    const auto transparentPriorityModel = vdp.buildFrameModel({160, 144});
    if (transparentPriorityModel.argbPixels[0] != vdp.debugDecodedCramColor(17u)) {
        return fail("simple tile-run transparent priority pixel incorrectly masked sprite");
    }
    writeNameEntry(vdp, 6u, 3u, static_cast<uint16_t>(paletteTile));
    vdp.writeVram(0xBF00u, 0xD0u);

    const auto modelFrame = vdp.buildFrameModel({160, 144});
    const auto realtimeFrame = vdp.buildRealtimeFrame({160, 144});
    if (modelFrame.argbPixels != realtimeFrame.argbPixels) {
        return fail("simple tile-run buildFrameModel and buildRealtimeFrame pixels differ");
    }

    writeRegister(memory, 8u, 0x01u);
    if (vdp.debugMode4SimpleBackgroundPathEligible()) {
        return fail("fine scrollX should force general background path");
    }
    writeRegister(memory, 8u, 0x00u);
    vdp.setSmsMode(true);
    if (vdp.debugMode4SimpleBackgroundPathEligible()) {
        return fail("SMS mode should force general background path");
    }
    writeRegister(memory, 0u, 0x40u);
    if (vdp.debugMode4SimpleBackgroundPathEligible()) {
        return fail("SMS fixed top-row mode should remain in general background path");
    }
    writeRegister(memory, 0u, 0x80u);
    if (vdp.debugMode4SimpleBackgroundPathEligible()) {
        return fail("SMS fixed right-column mode should remain in general background path");
    }
    vdp.setSmsMode(false);
    writeRegister(memory, 0u, 0x80u);
    if (vdp.debugMode4SimpleBackgroundPathEligible()) {
        return fail("native fixed-right-column bit should force general background path");
    }

    return 0;
}
