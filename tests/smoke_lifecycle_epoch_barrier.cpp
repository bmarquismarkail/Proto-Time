#include <cassert>
#include <vector>

#include "machine/AudioService.hpp"
#include "machine/VideoService.hpp"

int main()
{
    BMMQ::AudioService audio;
    assert(audio.configureFixedCallbackCapacity(4u));
    assert(audio.configureOutputTransport({
        .deviceSampleRate = 48000,
        .channelCount = 1,
        .callbackChunkSamples = 4,
        .readyQueueChunks = 2,
    }));
    const auto audioEpochBeforeStart = audio.transportStats().lifecycleEpoch;
    assert(audio.startOutputTransport());
    const auto audioEpochAfterStart = audio.transportStats().lifecycleEpoch;
    assert(audioEpochAfterStart >= audioEpochBeforeStart);
    audio.stopOutputTransport();
    audio.setBackendPausedOrClosed(true);
    const auto audioStats = audio.transportStats();
    assert(audioStats.lifecycleEpoch >= audioEpochAfterStart);
    assert(audioStats.epochBumpCount >= 1u);

    BMMQ::VideoService video(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 8,
        .mailboxDepthFrames = 2,
    });
    BMMQ::VideoFramePacket stale = BMMQ::makeBlankVideoFrame(8, 8, 1u);
    assert(video.submitFrame(stale));
    const auto videoEpochBefore = video.diagnostics().lifecycleEpoch;
    assert(video.configurePresenter({
        .windowTitle = "epoch-barrier",
        .scale = 1,
        .frameWidth = 8,
        .frameHeight = 8,
        .mode = BMMQ::VideoPresenterMode::Auto,
        .createHiddenWindowOnOpen = true,
        .showWindowOnPresent = false,
    }));
    const auto videoEpochAfter = video.diagnostics().lifecycleEpoch;
    assert(videoEpochAfter == videoEpochBefore + 1u);

    assert(video.presentOneFrame());
    assert(video.diagnostics().presentFallbackCount >= 1u);
    assert(video.diagnostics().lifecycleEpochBumpCount >= 1u);
    return 0;
}
