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
        return BMMQ::MachineTransitionMutationResult{
            .success = configured,
            .videoReady = true,
            .audioReady = configured,
        };
    }));

    assert(lifecycle.runTransition(BMMQ::MachineTransitionReason::VideoBackendRestart, [&]() {
        const auto ok = video.configure({
            .frameWidth = 16,
            .frameHeight = 16,
            .mailboxDepthFrames = 2,
        });
        return BMMQ::MachineTransitionMutationResult{
            .success = ok,
            .videoReady = ok,
            .audioReady = true,
        };
    }));

    assert(lifecycle.runTransition(BMMQ::MachineTransitionReason::ConfigReconfigure, [&]() {
        return BMMQ::MachineTransitionMutationResult{
            .success = false,
            .videoReady = false,
            .audioReady = true,
        };
    }));
    const auto degradedResult = lifecycle.lastTransitionResult();
    assert(degradedResult.outcome == BMMQ::MachineTransitionOutcome::Degraded);
    assert(degradedResult.failureStage == BMMQ::MachineTransitionFailureStage::Mutation);
    assert(lifecycle.degradedHeadlessVideoActive());

    assert(!lifecycle.runTransition(BMMQ::MachineTransitionReason::HardReset, [&]() {
        return BMMQ::MachineTransitionMutationResult{
            .success = false,
            .videoReady = false,
            .audioReady = false,
        };
    }));
    const auto failedResult = lifecycle.lastTransitionResult();
    assert(failedResult.outcome == BMMQ::MachineTransitionOutcome::Failed);
    assert(failedResult.failureStage == BMMQ::MachineTransitionFailureStage::Mutation);

    const auto reentryBefore = lifecycle.stats();
    bool nestedRejected = false;
    assert(lifecycle.runTransition(BMMQ::MachineTransitionReason::ConfigReconfigure, [&]() {
        nestedRejected = !lifecycle.runTransition(BMMQ::MachineTransitionReason::VideoBackendRestart, [&]() {
            return BMMQ::MachineTransitionMutationResult{
                .success = true,
                .videoReady = true,
                .audioReady = true,
            };
        });
        return BMMQ::MachineTransitionMutationResult{
            .success = true,
            .videoReady = true,
            .audioReady = true,
        };
    }));
    assert(nestedRejected);

    const auto after = lifecycle.stats();
    assert(after.transitionCount >= before.transitionCount + 4u);
    assert(after.successCount >= before.successCount + 2u);
    assert(after.degradedCount >= 1u);
    assert(after.failureCount >= before.failureCount + 1u);
    assert(after.transitionReentryAttemptCount >= reentryBefore.transitionReentryAttemptCount + 1u);
    assert(after.nestedTransitionRejectCount >= reentryBefore.nestedTransitionRejectCount + 1u);
    assert(after.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::AudioBackendRestart)] >= 1u);
    assert(after.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::VideoBackendRestart)] >= 1u);
    assert(after.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::ConfigReconfigure)] >= 1u);
    assert(after.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::HardReset)] >= 1u);
    assert(after.transitionDurationP95Ns >= after.transitionDurationP50Ns);
    return 0;
}
