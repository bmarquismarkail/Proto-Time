#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void populateBackground(GameGearVDP& vdp)
{
    // Populate pattern table with pseudo-random data to exercise decoding
    for (std::size_t tile = 0; tile < 384u; ++tile) {
        for (std::size_t row = 0; row < 8u; ++row) {
            const auto base = static_cast<uint16_t>(0x8000u + tile * 16u + row * 2u);
            vdp.writeVram(base + 0u, static_cast<uint8_t>((tile + row) & 0xFFu));
            vdp.writeVram(base + 1u, static_cast<uint8_t>(((tile * 3u) + row) & 0xFFu));
            vdp.writeVram(base + 2u, static_cast<uint8_t>((tile * 5u + row) & 0xFFu));
            vdp.writeVram(base + 3u, static_cast<uint8_t>((tile * 7u + row) & 0xFFu));
        }
    }

    // Populate name table with a simple pattern
    const uint16_t nameBaseAddr = static_cast<uint16_t>(0x8000u + 0x3800u);
    for (std::size_t y = 0; y < 24u; ++y) {
        for (std::size_t x = 0; x < 32u; ++x) {
            const uint16_t entryAddr = static_cast<uint16_t>(nameBaseAddr + (y * 32u + x) * 2u);
            const uint16_t tileIndex = static_cast<uint16_t>((x + y) & 0xFFu);
            vdp.writeVram(entryAddr + 0u, static_cast<uint8_t>(tileIndex & 0xFFu));
            vdp.writeVram(entryAddr + 1u, static_cast<uint8_t>((tileIndex >> 8u) & 0xFFu));
        }
    }
}

void populateVisibleSprites(GameGearVDP& vdp)
{
    constexpr uint16_t satBase = 0xBF00u;
    constexpr uint16_t xyBase = 0xBF80u;

    for (std::size_t sprite = 0u; sprite < 40u; ++sprite) {
        const auto y = static_cast<uint8_t>(23u + (sprite % 18u) * 8u);
        const auto x = static_cast<uint8_t>(48u + (sprite % 20u) * 8u);
        vdp.writeVram(static_cast<uint16_t>(satBase + sprite), y);
        vdp.writeVram(static_cast<uint16_t>(xyBase + sprite * 2u), x);
        vdp.writeVram(static_cast<uint16_t>(xyBase + sprite * 2u + 1u), static_cast<uint8_t>(sprite & 0xFFu));
    }
}

void runFrames(GameGearVDP& vdp, int iterations)
{
    for (int i = 0; i < iterations; ++i) {
        const auto frame = vdp.buildFrameModel({160, 144});
        if (frame.argbPixels.empty()) {
            throw std::runtime_error("empty frame");
        }
    }
}

} // namespace

int main()
{
    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    constexpr int kIterations = 1000;

    // Measure the frame-clear path before enabling display and populating
    // any visible content.
    runFrames(vdp, kIterations);

    // Turn display on
    memory.writeIoPort(0xBFu, 0x40u);
    memory.writeIoPort(0xBFu, 0x81u);

    populateBackground(vdp);

    try {
        runFrames(vdp, kIterations);
        populateVisibleSprites(vdp);
        runFrames(vdp, kIterations);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }

    std::cout << "bench-done\n";
    return 0;
}
