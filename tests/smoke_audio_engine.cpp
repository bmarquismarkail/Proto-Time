#include <cassert>
#include <cstdint>
#include <iostream>
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

    BMMQ::AudioEngine rollbackEngine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 16,
        .frameChunkSamples = 4,
    });
    std::vector<int16_t> generationA = {10, 11, 12, 13, 14, 15};
    std::vector<int16_t> generationB = {20, 21, 22, 23, 24, 25};
    rollbackEngine.appendRecentPcm(generationA, 5u);
    rollbackEngine.appendRecentPcm(generationB, 4u);
    std::vector<int16_t> rollbackOut(4, -1);
    rollbackEngine.render(rollbackOut);
    if (rollbackOut[0] != 22 || rollbackOut[1] != 23 || rollbackOut[2] != 24 || rollbackOut[3] != 25) {
        std::cerr << "rollback seed chunk was not preserved across reset boundary" << '\n';
        return 1;
    }

    BMMQ::AudioEngine repeatedRollbackEngine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 16,
        .frameChunkSamples = 4,
    });
    std::vector<int16_t> generationC = {30, 31, 32, 33, 34, 35};
    std::vector<int16_t> generationD = {40, 41, 42, 43, 44, 45};
    std::vector<int16_t> generationE = {50, 51, 52, 53, 54, 55};
    repeatedRollbackEngine.appendRecentPcm(generationC, 9u);
    repeatedRollbackEngine.appendRecentPcm(generationD, 7u);
    repeatedRollbackEngine.appendRecentPcm(generationE, 6u);
    std::vector<int16_t> repeatedRollbackOut(4, -1);
    repeatedRollbackEngine.render(repeatedRollbackOut);
    if (repeatedRollbackOut[0] != 52 || repeatedRollbackOut[1] != 53
        || repeatedRollbackOut[2] != 54 || repeatedRollbackOut[3] != 55) {
        std::cerr << "latest rollback seed chunk did not win before callback reset" << '\n';
        return 1;
    }

    return 0;
}
