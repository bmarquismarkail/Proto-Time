#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/VideoService.hpp"
#include "tests/visual_test_helpers.hpp"

namespace {

class PassthroughVideoProcessor final : public BMMQ::IVideoFrameProcessorPlugin {
public:
    [[nodiscard]] BMMQ::VideoPluginCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = true,
            .frameSizePreserving = true,
            .snapshotAware = true,
            .deterministic = true,
            .headlessSafe = true,
        };
    }

    bool process(const BMMQ::VideoFramePacket& input,
                 BMMQ::VideoFramePacket& output) noexcept override
    {
        output = input;
        ++processCount;
        return true;
    }

    std::size_t processCount = 0;
};

class NonRealtimeVideoProcessor final : public BMMQ::IVideoFrameProcessorPlugin {
public:
    [[nodiscard]] BMMQ::VideoPluginCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = false,
            .frameSizePreserving = true,
            .deterministic = true,
            .headlessSafe = true,
            .nonRealtimeOnly = true,
        };
    }

    bool process(const BMMQ::VideoFramePacket& input,
                 BMMQ::VideoFramePacket& output) noexcept override
    {
        output = input;
        return true;
    }
};

class RecordingCapture final : public BMMQ::IVideoCapturePlugin {
public:
    [[nodiscard]] BMMQ::VideoPluginCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = true,
            .frameSizePreserving = true,
            .snapshotAware = true,
            .deterministic = true,
            .headlessSafe = true,
        };
    }

    bool capture(const BMMQ::VideoFramePacket& frame) noexcept override
    {
        lastGeneration = frame.generation;
        ++captureCount;
        return true;
    }

    std::size_t captureCount = 0;
    std::uint64_t lastGeneration = 0;
};

BMMQ::VideoStateView makeSplitScrollState(uint8_t ly, uint8_t scx)
{
    BMMQ::VideoStateView state;
    state.vram.resize(0x2000u, 0);
    state.oam.resize(0x00A0u, 0);
    state.lcdc = 0x91u;
    state.ly = ly;
    state.scx = scx;
    state.bgp = 0xE4u;

    for (std::size_t row = 0; row < 8u; ++row) {
        const auto tile0 = row * 2u;
        state.vram[tile0] = 0xFFu;
        state.vram[tile0 + 1u] = 0x00u;

        const auto tile1 = 0x10u + row * 2u;
        state.vram[tile1] = 0xFFu;
        state.vram[tile1 + 1u] = 0xFFu;
    }
    state.vram[0x1800u] = 0x00u;
    state.vram[0x1801u] = 0x01u;
    return state;
}

} // namespace

int main()
{
    namespace Visual = BMMQ::Tests::Visual;

    GameBoyMachine machine;
    auto& service = machine.videoService();

    assert(service.state() == BMMQ::VideoLifecycleState::Headless);
    assert(service.diagnostics().headlessModeActive);
    assert(service.visualDebugAdapter() == machine.visualDebugAdapter());
    assert(service.configure({
        .frameWidth = 32,
        .frameHeight = 24,
        .mailboxDepthFrames = 2,
    }));

    assert(service.addProcessor(std::make_unique<PassthroughVideoProcessor>()));
    assert(!service.addProcessor(std::make_unique<NonRealtimeVideoProcessor>()));

    auto capture = std::make_unique<RecordingCapture>();
    auto* capturePtr = capture.get();
    assert(service.addCapture(std::move(capture)));

    service.setBackendActiveForTesting(true);
    assert(service.state() == BMMQ::VideoLifecycleState::Active);
    assert(!service.configure({
        .frameWidth = 16,
        .frameHeight = 16,
        .mailboxDepthFrames = 2,
    }));
    assert(!service.addProcessor(std::make_unique<PassthroughVideoProcessor>()));
    service.setBackendActiveForTesting(false);

    BMMQ::VideoFramePacket frame = BMMQ::makeBlankVideoFrame(32, 24, 3u);
    assert(service.submitFrame(frame));
    assert(service.diagnostics().presentCount == 0u);
    assert(service.presentOneFrame());
    assert(capturePtr->captureCount == 1u);
    assert(capturePtr->lastGeneration == 3u);
    assert(service.diagnostics().lastPresentedGeneration == 3u);
    assert(service.diagnostics().presentCount == 1u);
    assert(service.diagnostics().lastPublishedGeneration == 3u);

    BMMQ::VideoService scanlineService(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 2,
        .mailboxDepthFrames = 1,
    });
    auto line0 = Visual::makeSemanticModelFromState(makeSplitScrollState(0u, 0u), 8, 2);
    auto line1 = Visual::makeSemanticModelFromState(makeSplitScrollState(1u, 8u), 8, 2);
    auto vblank = Visual::makeSemanticModelFromState(makeSplitScrollState(144u, 8u), 8, 2);

    assert(!scanlineService.submitVideoDebugModel(BMMQ::MachineEvent{
        .type = BMMQ::MachineEventType::VideoScanlineReady,
        .category = BMMQ::PluginCategory::Video,
        .address = 0xFF44u,
        .value = 0u,
    }, line0));
    assert(scanlineService.engine().mailboxFrameCount() == 0u);
    assert(!scanlineService.submitVideoDebugModel(BMMQ::MachineEvent{
        .type = BMMQ::MachineEventType::VideoScanlineReady,
        .category = BMMQ::PluginCategory::Video,
        .address = 0xFF44u,
        .value = 1u,
    }, line1));
    assert(scanlineService.engine().mailboxFrameCount() == 0u);
    assert(scanlineService.submitVideoDebugModel(BMMQ::MachineEvent{
        .type = BMMQ::MachineEventType::VBlank,
        .category = BMMQ::PluginCategory::Video,
        .address = 0xFF44u,
        .value = 144u,
    }, vblank));

    auto splitFrame = scanlineService.engine().tryConsumeLatestFrame();
    assert(splitFrame.has_value());
    assert(splitFrame->width == 8);
    assert(splitFrame->height == 2);
    const auto hasVisiblePixel = std::any_of(
        splitFrame->pixels.begin(),
        splitFrame->pixels.end(),
        [](std::uint32_t pixel) { return pixel != 0u; });
    assert(hasVisiblePixel);
    assert(splitFrame->pixelCount() == 16u);

    assert(!machine.setVideoService(nullptr));
    auto replacement = std::make_unique<BMMQ::VideoService>(BMMQ::VideoEngineConfig{
        .frameWidth = 4,
        .frameHeight = 4,
        .mailboxDepthFrames = 1,
    });
    assert(machine.setVideoService(std::move(replacement)));
    assert(machine.videoService().engine().config().frameWidth == 4);
    assert(machine.videoService().visualDebugAdapter() == machine.visualDebugAdapter());

    machine.pluginManager().initialize(machine.mutableView());
    auto* beforeRejectedSwap = &machine.videoService();
    assert(!machine.setVideoService(std::make_unique<BMMQ::VideoService>()));
    assert(&machine.videoService() == beforeRejectedSwap);
    machine.pluginManager().shutdown(machine.mutableView());

    return 0;
}
