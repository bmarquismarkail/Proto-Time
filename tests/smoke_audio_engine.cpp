#include <cassert>
#include <cstdint>
#include <vector>

#include "machine/plugins/AudioEngine.hpp"

int main()
{
    BMMQ::AudioEngine engine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 44100,
        .ringBufferCapacitySamples = 1024,
        .frameChunkSamples = 256,
    });

    std::vector<int16_t> recent(512, 0);
    for (std::size_t i = 0; i < recent.size(); ++i) {
        recent[i] = static_cast<int16_t>(i);
    }

    engine.appendRecentPcm(recent, 1u);
    assert(engine.bufferedSamples() == 256u);
    assert(engine.stats().sourceSamplesPushed == 256u);

    engine.appendRecentPcm(recent, 1u);
    assert(engine.bufferedSamples() == 256u);

    std::vector<int16_t> out(64, 0);
    engine.render(out);
    assert(engine.stats().callbackCount == 1u);
    assert(engine.stats().outputSamplesProduced == 64u);
    assert(engine.stats().sourceSamplesConsumed >= 1u);

    engine.resetStats();
    engine.resetStream();
    assert(engine.bufferedSamples() == 0u);
    assert(engine.stats().callbackCount == 0u);

    engine.appendRecentPcm(recent, 2u);
    assert(engine.bufferedSamples() == 256u);

    BMMQ::AudioEngine unity({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 1024,
        .frameChunkSamples = 256,
    });
    unity.appendRecentPcm(recent, 1u);
    std::vector<int16_t> unityOut(4, 0);
    unity.render(unityOut);
    assert(unityOut[0] == recent[256]);
    assert(unityOut[1] == recent[257]);
    assert(unityOut[2] == recent[258]);
    assert(unityOut[3] == recent[259]);

    return 0;
}
