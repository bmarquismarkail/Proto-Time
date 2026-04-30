#include <cassert>
#include <memory>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/MachineLifecycleCoordinator.hpp"
#include "machine/plugins/video/adapters/HeadlessFrameDumper.hpp"

int main()
{
    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    cartridgeRom[0x0100] = 0x00;
    machine.loadRom(cartridgeRom);

    auto& audio = machine.audioService();
    auto& video = machine.videoService();
    auto& lifecycle = machine.lifecycleCoordinator();

    audio.setLifecycleContractEnforced(true);
    video.setLifecycleContractEnforced(true);

    const auto audioDeniedBefore = audio.lifecycleContractDeniedCalls();
    const auto videoDeniedBefore = video.lifecycleContractDeniedCalls();

    assert(!audio.configureOutputTransport({
        .deviceSampleRate = 48000,
        .channelCount = 1,
        .callbackChunkSamples = 8,
        .readyQueueChunks = 2,
    }));
    assert(!video.configure({
        .frameWidth = 16,
        .frameHeight = 16,
        .mailboxDepthFrames = 2,
    }));
    assert(!video.configurePresenter({
        .windowTitle = "contract-deny",
        .scale = 1,
        .frameWidth = 16,
        .frameHeight = 16,
        .mode = BMMQ::VideoPresenterMode::Software,
        .createHiddenWindowOnOpen = true,
        .showWindowOnPresent = false,
    }));
    assert(!video.attachPresenter(std::make_unique<BMMQ::HeadlessFrameDumper>()));
    assert(audio.lifecycleContractDeniedCalls() > audioDeniedBefore);
    assert(video.lifecycleContractDeniedCalls() > videoDeniedBefore);

    const auto coordinatorAudioDeniedBefore = audio.lifecycleContractDeniedCalls();
    const auto coordinatorVideoDeniedBefore = video.lifecycleContractDeniedCalls();

    const bool transitioned = lifecycle.runTransition(BMMQ::MachineTransitionReason::ConfigReconfigure, [&]() {
        const bool audioOk = audio.configureFixedCallbackCapacity(8u) &&
                             audio.configureOutputTransport({
                                 .deviceSampleRate = 48000,
                                 .channelCount = 1,
                                 .callbackChunkSamples = 8,
                                 .readyQueueChunks = 2,
                             });
        const bool videoOk = video.configure({
            .frameWidth = 16,
            .frameHeight = 16,
            .mailboxDepthFrames = 2,
        }) && video.configurePresenter({
            .windowTitle = "contract-allow",
            .scale = 1,
            .frameWidth = 16,
            .frameHeight = 16,
            .mode = BMMQ::VideoPresenterMode::Software,
            .createHiddenWindowOnOpen = true,
            .showWindowOnPresent = false,
        }) && video.attachPresenter(std::make_unique<BMMQ::HeadlessFrameDumper>());
        return BMMQ::MachineTransitionMutationResult{
            .success = audioOk && videoOk,
            .videoReady = videoOk,
            .audioReady = audioOk,
        };
    });
    assert(transitioned);
    assert(audio.lifecycleContractDeniedCalls() == coordinatorAudioDeniedBefore);
    assert(video.lifecycleContractDeniedCalls() == coordinatorVideoDeniedBefore);

    return 0;
}
