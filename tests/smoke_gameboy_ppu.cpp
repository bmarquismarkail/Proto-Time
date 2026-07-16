#include <cassert>
#include <cstdint>
#include <vector>

#include "cores/gameboy/GameBoyMemoryMap.hpp"
#include "cores/gameboy/GameBoyPPU.hpp"

namespace {

constexpr std::uint32_t kLightest = 0xFFE0F8D0u;
constexpr std::uint32_t kDarkest = 0xFF081820u;

std::vector<std::uint32_t> decode(const BMMQ::RealtimeVideoPacket& frame)
{
    std::vector<std::uint32_t> pixels;
    assert(BMMQ::decodeVideoSurface(frame.surface, frame.width, frame.height, pixels));
    return pixels;
}

void writeSolidTile(GB::GameBoyMemoryMap& memory, std::uint8_t tileIndex, std::uint8_t colorIndex)
{
    const auto low = static_cast<std::uint8_t>((colorIndex & 0x01u) != 0u ? 0xFFu : 0x00u);
    const auto high = static_cast<std::uint8_t>((colorIndex & 0x02u) != 0u ? 0xFFu : 0x00u);
    const auto base = static_cast<std::uint16_t>(0x8000u + static_cast<std::uint16_t>(tileIndex) * 16u);
    for (std::uint8_t row = 0; row < 8u; ++row) {
        memory.write(static_cast<std::uint16_t>(base + row * 2u), low);
        memory.write(static_cast<std::uint16_t>(base + row * 2u + 1u), high);
    }
}

void testCompletedScanlinesKeepTheirOwnScrollState()
{
    GB::GameBoyMemoryMap memory;
    GB::GameBoyPPU ppu;
    ppu.memoryMap = &memory;

    memory.write(0xFF40u, 0x91u); // LCD on, BG on, unsigned tile data, 8x8 BG tilemap at 9800.
    memory.write(0xFF42u, 0x00u);
    memory.write(0xFF43u, 0x00u);
    memory.write(0xFF47u, 0xE4u); // DMG identity palette: color 0 lightest, color 3 darkest.
    writeSolidTile(memory, 0u, 0u);
    writeSolidTile(memory, 1u, 3u);
    memory.write(0x9800u, 0u);
    memory.write(0x9801u, 1u);

    ppu.step(456u);
    assert(ppu.takeScanlineReady());

    memory.write(0xFF43u, 8u);
    ppu.step(456u);
    assert(ppu.takeScanlineReady());

    const auto frame = ppu.buildRealtimeFrame({.frameWidth = 160, .frameHeight = 144});
    const auto pixels = decode(frame.packet);
    assert(pixels[0] == kLightest);
    assert(pixels[160] == kDarkest);
}

void testHBlankScrollWriteAffectsNextScanlineOnly()
{
    GB::GameBoyMemoryMap memory;
    GB::GameBoyPPU ppu;
    ppu.memoryMap = &memory;

    memory.write(0xFF40u, 0x91u); // LCD on, BG on, unsigned tile data, 8x8 BG tilemap at 9800.
    memory.write(0xFF42u, 0x00u);
    memory.write(0xFF43u, 0x00u);
    memory.write(0xFF47u, 0xE4u);
    writeSolidTile(memory, 0u, 0u);
    writeSolidTile(memory, 1u, 3u);
    memory.write(0x9800u, 0u);
    memory.write(0x9801u, 1u);

    ppu.step(252u); // End of fixed-timing mode 2 + minimum mode 3 pixel transfer.
    memory.write(0xFF43u, 8u);
    ppu.step(204u);
    assert(ppu.takeScanlineReady());

    ppu.step(456u);
    assert(ppu.takeScanlineReady());

    const auto frame = ppu.buildRealtimeFrame({.frameWidth = 160, .frameHeight = 144});
    const auto pixels = decode(frame.packet);
    assert(pixels[0] == kLightest);
    assert(pixels[160] == kDarkest);
}

void testOamWriteAfterSpriteSearchAffectsNextScanlineOnly()
{
    GB::GameBoyMemoryMap memory;
    GB::GameBoyPPU ppu;
    ppu.memoryMap = &memory;

    memory.write(0xFF40u, 0x93u); // LCD on, BG on, sprites on.
    memory.write(0xFF47u, 0xE4u);
    memory.write(0xFF48u, 0xE4u);
    writeSolidTile(memory, 0u, 0u);
    writeSolidTile(memory, 1u, 3u);
    memory.write(0x9800u, 0u);

    ppu.step(80u); // End of mode 2 sprite search for scanline 0.
    memory.write(0xFE00u, 16u); // Y=0 on screen.
    memory.write(0xFE01u, 8u);  // X=0 on screen.
    memory.write(0xFE02u, 1u);
    memory.write(0xFE03u, 0u);
    ppu.step(376u);
    assert(ppu.takeScanlineReady());

    ppu.step(456u);
    assert(ppu.takeScanlineReady());

    const auto frame = ppu.buildRealtimeFrame({.frameWidth = 160, .frameHeight = 144});
    const auto pixels = decode(frame.packet);
    assert(pixels[0] == kLightest);
    assert(pixels[160] == kDarkest);
}

void testPpuModeTimingMatchesDmgLinePhases()
{
    GB::GameBoyMemoryMap memory;
    GB::GameBoyPPU ppu;
    ppu.memoryMap = &memory;

    memory.write(0xFF40u, 0x80u);

    ppu.step(79u);
    assert(ppu.currentMode() == 2u);
    ppu.step(1u);
    assert(ppu.currentMode() == 3u);
    ppu.step(171u);
    assert(ppu.currentMode() == 3u);
    ppu.step(1u);
    assert(ppu.currentMode() == 0u);
}

void testLyIncludesLine153BeforeWrapping()
{
    GB::GameBoyMemoryMap memory;
    GB::GameBoyPPU ppu;
    ppu.memoryMap = &memory;

    memory.write(0xFF40u, 0x80u);

    ppu.step(153u * 456u);
    assert(ppu.ly() == 153u);
    assert(ppu.currentMode() == 1u);
    ppu.step(456u);
    assert(ppu.ly() == 0u);
}

} // namespace

int main()
{
    testCompletedScanlinesKeepTheirOwnScrollState();
    testHBlankScrollWriteAffectsNextScanlineOnly();
    testOamWriteAfterSpriteSearchAffectsNextScanlineOnly();
    testPpuModeTimingMatchesDmgLinePhases();
    testLyIncludesLine153BeforeWrapping();
    return 0;
}
