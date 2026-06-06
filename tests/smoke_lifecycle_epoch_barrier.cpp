#include <cassert>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/MachineLifecycleCoordinator.hpp"

int main()
{
    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    cartridgeRom[0x0100] = 0x00;
    machine.loadRom(cartridgeRom);

    auto& audio = machine.audioService();
    auto& video = machine.videoService();
    auto& lifecycle = machine.lifecycleCoordinator();

    assert(audio.configureFixedCallbackCapacity(4u));
    assert(audio.configureOutputTransport({
        .deviceSampleRate = 48000,
        .channelCount = 1,
        .callbackChunkSamples = 4,
        .readyQueueChunks = 2,
    }));
    const auto audioEpochBefore = audio.transportStats().lifecycleEpoch;
    const auto videoEpochBefore = video.diagnostics().lifecycleEpoch;

    assert(lifecycle.runTransition(BMMQ::MachineTransitionReason::ConfigReconfigure, [&]() {
        const bool ok = audio.startOutputTransport() && video.configurePresenter({
            .windowTitle = "epoch-barrier",
            .scale = 1,
            .frameWidth = 8,
            .frameHeight = 8,
            .mode = BMMQ::VideoPresenterMode::Auto,
            .createHiddenWindowOnOpen = true,
            .showWindowOnPresent = false,
        });
        return BMMQ::MachineTransitionMutationResult{
            .success = ok,
            .videoReady = ok,
            .audioReady = ok,
        };
    }));
    assert(audio.transportStats().lifecycleEpoch > audioEpochBefore);
    assert(video.diagnostics().lifecycleEpoch > videoEpochBefore);
    const auto coordinatorStats = lifecycle.stats();
    assert(coordinatorStats.transitionCount >= 1u);
    assert(coordinatorStats.successCount >= 1u);
    return 0;
}
