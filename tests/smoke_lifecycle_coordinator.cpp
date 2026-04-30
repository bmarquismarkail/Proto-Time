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

    auto& lifecycle = machine.lifecycleCoordinator();
    auto& audio = machine.audioService();
    auto& video = machine.videoService();

    const auto before = lifecycle.stats();
    assert(lifecycle.runTransition(BMMQ::MachineTransitionReason::AudioBackendRestart, [&]() {
        audio.setBackendPausedOrClosed(true);
        const auto configured = audio.configureFixedCallbackCapacity(8u) &&
                                audio.configureOutputTransport({
                                    .deviceSampleRate = 48000,
                                    .channelCount = 1,
                                    .callbackChunkSamples = 8,
                                    .readyQueueChunks = 2,
                                });
        return configured;
    }));

    assert(lifecycle.runTransition(BMMQ::MachineTransitionReason::VideoBackendRestart, [&]() {
        return video.configure({
            .frameWidth = 16,
            .frameHeight = 16,
            .mailboxDepthFrames = 2,
        });
    }));

    const auto after = lifecycle.stats();
    assert(after.transitionCount >= before.transitionCount + 2u);
    assert(after.successCount >= before.successCount + 2u);
    assert(after.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::AudioBackendRestart)] >= 1u);
    assert(after.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::VideoBackendRestart)] >= 1u);
    assert(after.transitionDurationP95Ns >= after.transitionDurationP50Ns);
    return 0;
}
