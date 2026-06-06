#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

double runScenarioMedianNsPerFrame(GameGearVDP& vdp, int iterations, int repeats)
{
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(std::max(repeats, 1)));
    for (int run = 0; run < repeats; ++run) {
        const auto started = std::chrono::steady_clock::now();
        runFrames(vdp, iterations);
        const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto perFrame = static_cast<double>(elapsedNs) / static_cast<double>(std::max(iterations, 1));
        samples.push_back(perFrame);
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2u];
}

int parsePositiveInt(const char* value, const char* flag)
{
    if (value == nullptr) {
        throw std::invalid_argument(std::string(flag) + " requires a value");
    }
    const int parsed = std::stoi(value);
    if (parsed <= 0) {
        throw std::invalid_argument(std::string(flag) + " must be > 0");
    }
    return parsed;
}

} // namespace

int main(int argc, char** argv)
{
    int iterations = 1000;
    int repeats = 5;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--iterations") {
            if (i + 1 >= argc) {
                std::cerr << "--iterations requires a value\n";
                return 2;
            }
            iterations = parsePositiveInt(argv[++i], "--iterations");
        } else if (arg == "--repeats") {
            if (i + 1 >= argc) {
                std::cerr << "--repeats requires a value\n";
                return 2;
            }
            repeats = parsePositiveInt(argv[++i], "--repeats");
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            return 2;
        }
    }

    GameGearVDP vdp;
    GameGearMemoryMap memory;
    memory.setVdp(&vdp);
    vdp.reset();

    // Measure the frame-clear path before enabling display and populating
    // any visible content.
    const auto clearMedian = runScenarioMedianNsPerFrame(vdp, iterations, repeats);

    // Turn display on
    memory.writeIoPort(0xBFu, 0x40u);
    memory.writeIoPort(0xBFu, 0x81u);

    populateBackground(vdp);

    try {
        const auto backgroundMedian = runScenarioMedianNsPerFrame(vdp, iterations, repeats);
        populateVisibleSprites(vdp);
        const auto spriteMedian = runScenarioMedianNsPerFrame(vdp, iterations, repeats);
        std::cout << "bench iterations=" << iterations
                  << " repeats=" << repeats
                  << " clear_ns_per_frame_med=" << clearMedian
                  << " background_ns_per_frame_med=" << backgroundMedian
                  << " sprite_ns_per_frame_med=" << spriteMedian
                  << '\n';
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }

    std::cout << "bench-done\n";
    return 0;
}
