#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <vector>

#include "machine/VisualOverrideService.hpp"
#include "machine/plugins/AudioEngine.hpp"
#include "machine/plugins/video/VideoEngine.hpp"
#include "tests/visual_test_helpers.hpp"

namespace {

[[nodiscard]] long perfTestTimeoutMs() noexcept
{
    constexpr long kDefaultPerfTestTimeoutMs = 1000;
    if (const char* value = std::getenv("PERF_TEST_TIMEOUT_MS"); value != nullptr) {
        char* end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        if (end != value && parsed > 0) {
            return parsed;
        }
    }
    return kDefaultPerfTestTimeoutMs;
}

BMMQ::VideoStateView makeDebugVideoState()
{
    BMMQ::VideoStateView state;
    state.vram.resize(0x2000u, 0);
    state.oam.resize(0x00A0u, 0);
    state.lcdc = 0x93u;
    state.bgp = 0xE4u;
    state.obp0 = 0xE4u;
    state.obp1 = 0xE4u;

    state.vram[0x0000] = 0xFFu;
    state.vram[0x0001] = 0x00u;
    for (std::size_t row = 1; row < 8; ++row) {
        state.vram[row * 2u] = 0xFFu;
        state.vram[row * 2u + 1u] = 0x00u;
    }
    state.vram[0x1800] = 0x00u;

    state.vram[0x0010] = 0xFFu;
    state.vram[0x0011] = 0xFFu;
    for (std::size_t row = 1; row < 8; ++row) {
        state.vram[0x0010u + row * 2u] = 0xFFu;
        state.vram[0x0010u + row * 2u + 1u] = 0xFFu;
    }

    state.oam[0] = 32u;
    state.oam[1] = 16u;
    state.oam[2] = 0x01u;
    state.oam[3] = 0x00u;
    return state;
}

BMMQ::VideoStateView makeDenseVideoState()
{
    BMMQ::VideoStateView state;
    state.vram.resize(0x2000u, 0);
    state.oam.resize(0x00A0u, 0);
    state.lcdc = 0xF3u;
    state.bgp = 0xE4u;
    state.obp0 = 0xE4u;
    state.obp1 = 0x1Bu;

    for (std::size_t tile = 0; tile < 384u; ++tile) {
        for (std::size_t row = 0; row < 8u; ++row) {
            const auto base = tile * 16u + row * 2u;
            state.vram[base] = static_cast<uint8_t>(0xA5u ^ static_cast<uint8_t>(tile + row));
            state.vram[base + 1u] = static_cast<uint8_t>(0x5Au ^ static_cast<uint8_t>(tile * 3u + row));
        }
    }
    for (std::size_t i = 0; i < 0x800u; ++i) {
        state.vram[0x1800u + i] = static_cast<uint8_t>(i & 0xFFu);
    }
    for (std::size_t sprite = 0; sprite < 40u; ++sprite) {
        const auto base = sprite * 4u;
        state.oam[base] = static_cast<uint8_t>(16u + (sprite * 4u) % 144u);
        state.oam[base + 1u] = static_cast<uint8_t>(8u + (sprite * 7u) % 160u);
        state.oam[base + 2u] = static_cast<uint8_t>(sprite & 0xFFu);
        state.oam[base + 3u] = static_cast<uint8_t>((sprite & 1u) != 0u ? 0x20u : 0x00u);
    }
    return state;
}

BMMQ::VideoDebugFrameModel makeGenericDebugModel()
{
    BMMQ::VideoDebugFrameModel model;
    model.width = 4;
    model.height = 2;
    model.displayEnabled = true;
    model.argbPixels = {
        0xFF112233u, 0xFF445566u, 0xFF778899u, 0xFFAABBCCu,
        0xFF010203u, 0xFF040506u, 0xFF070809u, 0xFF0A0B0Cu,
    };
    model.semantics.resize(model.argbPixels.size());
    return model;
}

} // namespace

int main()
{
    namespace Visual = BMMQ::Tests::Visual;

    BMMQ::VideoEngine genericEngine({
        .frameWidth = 4,
        .frameHeight = 2,
        .mailboxDepthFrames = 1,
    });
    const auto genericModel = makeGenericDebugModel();
    const auto genericFrame = genericEngine.buildDebugFrame(genericModel, 1u);
    assert(genericFrame.pixels == genericModel.argbPixels);

    BMMQ::VideoEngine engine({
        .frameWidth = 32,
        .frameHeight = 24,
        .mailboxDepthFrames = 2,
    });

    const auto state = makeDebugVideoState();
    const auto model = Visual::makeSemanticModelFromState(state, 32, 24);
    auto frame = engine.buildDebugFrame(model, 7u);
    assert(frame.width == 32);
    assert(frame.height == 24);
    assert(frame.pixelCount() == 32u * 24u);
    assert(frame.generation == 7u);
    assert(frame.format == BMMQ::VideoFrameFormat::Argb8888);

    const auto shade1 = 0xFF88C070u;
    const auto shade3 = 0xFF081820u;
    assert(frame.pixels[0] == shade1);
    assert(frame.pixels[1] == shade1);
    assert(frame.pixels[7] == shade1);
    assert(frame.pixels[8] == shade1);
    assert(frame.pixels[16 * 32 + 8] == shade3);
    assert(frame.pixels[16 * 32 + 15] == shade3);

    auto frameA = frame;
    auto frameB = frame;
    frameB.generation = 8u;
    auto frameC = frame;
    frameC.generation = 9u;

    const auto submitA = engine.submitFrame(frameA);
    assert(submitA.accepted);
    assert(!submitA.overwroteOldFrame);
    const auto submitB = engine.submitFrame(frameB);
    assert(submitB.accepted);
    assert(!submitB.overwroteOldFrame);
    const auto submitC = engine.submitFrame(frameC);
    assert(submitC.accepted);
    assert(submitC.overwroteOldFrame);
    assert(engine.stats().overwriteCount == 1u);
    assert(engine.stats().mailboxHighWaterMark == 2u);
    assert(engine.stats().publishedFrameCount == 3u);
    assert(engine.stats().lastPublishedGeneration == 9u);

    auto consumed = engine.tryConsumeLatestFrame();
    assert(consumed.has_value());
    assert(consumed->generation == 9u);
    assert(engine.stats().consumedFrameCount == 1u);
    assert(engine.stats().staleFrameDropCount == 1u);
    assert(engine.stats().lastConsumedGeneration == 9u);
    assert(!engine.tryConsumeLatestFrame().has_value());
    assert(engine.mailboxFrameCount() == 0u);

    engine.advanceGeneration();
    assert(engine.currentGeneration() == 1u);
    assert(engine.lastValidFrame().has_value());
    assert(engine.fallbackFrame().generation == 9u);

    BMMQ::VideoEngine blankEngine({
        .frameWidth = 4,
        .frameHeight = 3,
        .mailboxDepthFrames = 1,
    });
    const auto blank = blankEngine.fallbackFrame();
    assert(blank.width == 4);
    assert(blank.height == 3);
    assert(blank.pixelCount() == 12u);
    assert(blank.pixels[0] == 0xFF000000u);

    BMMQ::VideoEngine fullFrameEngine({
        .frameWidth = 160,
        .frameHeight = 144,
        .mailboxDepthFrames = 2,
    });
    BMMQ::AudioEngine audioEngine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 4096,
        .frameChunkSamples = 256,
    });
    const auto denseState = makeDenseVideoState();
    const auto denseModel = Visual::makeSemanticModelFromState(denseState, 160, 144);
    std::vector<int16_t> audioChunk(256u, 1200);
    std::vector<int16_t> audioOutput(256u, 0);

    constexpr int kFramesToBuild = 60;
    const auto maxPerfTime = std::chrono::milliseconds(perfTestTimeoutMs());
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kFramesToBuild; ++i) {
        const auto denseFrame = fullFrameEngine.buildDebugFrame(denseModel, static_cast<uint64_t>(i + 1));
        assert(denseFrame.pixelCount() == 160u * 144u);
        audioEngine.appendRecentPcm(audioChunk, static_cast<uint64_t>(i + 1));
        audioEngine.render(audioOutput);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed) < maxPerfTime);
    assert(audioEngine.stats().underrunCount == 0u);
    assert(audioEngine.stats().samplesDelivered >= static_cast<std::size_t>(kFramesToBuild) * audioOutput.size());

    BMMQ::VisualOverrideService inactiveVisualService;
    BMMQ::VideoEngine inactiveVisualEngine({
        .frameWidth = 160,
        .frameHeight = 144,
        .mailboxDepthFrames = 1,
    });
    inactiveVisualEngine.setVisualOverrideService(&inactiveVisualService);
    const auto inactiveStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kFramesToBuild; ++i) {
        const auto denseFrame = inactiveVisualEngine.buildDebugFrame(denseModel, static_cast<uint64_t>(i + 1));
        assert(denseFrame.pixelCount() == 160u * 144u);
    }
    const auto inactiveElapsed = std::chrono::steady_clock::now() - inactiveStart;
    assert(std::chrono::duration_cast<std::chrono::milliseconds>(inactiveElapsed) < maxPerfTime);

    const auto captureDir = std::filesystem::temp_directory_path() / "proto_time_video_engine_capture_perf";
    std::filesystem::remove_all(captureDir);
    BMMQ::VisualOverrideService captureVisualService;
    assert(captureVisualService.beginCapture(captureDir, "gameboy"));
    BMMQ::VideoEngine captureVisualEngine({
        .frameWidth = 160,
        .frameHeight = 144,
        .mailboxDepthFrames = 1,
    });
    captureVisualEngine.setVisualOverrideService(&captureVisualService);
    const auto captureStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i) {
        const auto denseFrame = captureVisualEngine.buildDebugFrame(denseModel, static_cast<uint64_t>(i + 1));
        assert(denseFrame.pixelCount() == 160u * 144u);
    }
    const auto captureElapsed = std::chrono::steady_clock::now() - captureStart;
    assert(captureVisualService.captureStats().uniqueResourcesDumped != 0u);
    assert(std::chrono::duration_cast<std::chrono::milliseconds>(captureElapsed) < maxPerfTime);
    captureVisualService.endCapture();
    std::filesystem::remove_all(captureDir);

    return 0;
}
