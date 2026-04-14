#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <memory>
#include <span>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/VideoService.hpp"

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

} // namespace

int main()
{
    GameBoyMachine machine;
    auto& service = machine.videoService();

    assert(service.state() == BMMQ::VideoLifecycleState::Headless);
    assert(service.diagnostics().headlessModeActive);
    assert(service.configure({
        .frameWidth = 32,
        .frameHeight = 24,
        .queueCapacityFrames = 2,
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
        .queueCapacityFrames = 2,
    }));
    assert(!service.addProcessor(std::make_unique<PassthroughVideoProcessor>()));
    service.setBackendActiveForTesting(false);

    BMMQ::VideoFramePacket frame = BMMQ::makeBlankVideoFrame(32, 24, 3u);
    assert(service.submitFrame(frame));
    assert(service.presentOneFrame());
    assert(capturePtr->captureCount == 1u);
    assert(capturePtr->lastGeneration == 3u);
    assert(service.diagnostics().lastPresentedGeneration == 3u);

    assert(!machine.setVideoService(nullptr));
    auto replacement = std::make_unique<BMMQ::VideoService>(BMMQ::VideoEngineConfig{
        .frameWidth = 4,
        .frameHeight = 4,
        .queueCapacityFrames = 1,
    });
    assert(machine.setVideoService(std::move(replacement)));
    assert(machine.videoService().engine().config().frameWidth == 4);

    machine.pluginManager().initialize(machine.mutableView());
    auto* beforeRejectedSwap = &machine.videoService();
    assert(!machine.setVideoService(std::make_unique<BMMQ::VideoService>()));
    assert(&machine.videoService() == beforeRejectedSwap);
    machine.pluginManager().shutdown(machine.mutableView());

    return 0;
}
