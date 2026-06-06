#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "machine/plugins/AudioEngine.hpp"

int main()
{
    std::vector<int16_t> s512(512, 0);
    for (std::size_t i = 0; i < s512.size(); ++i) {
        s512[i] = static_cast<int16_t>(i);
    }

    BMMQ::AudioEngine engine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 4096,
        .frameChunkSamples = 512,
    });
    engine.appendRecentPcm(s512, 1u);
    assert(engine.bufferedSamples() == 512u);
    assert(engine.stats().sourceSamplesPushed == 512u);
    assert(engine.stats().appendCallCount == 1u);
    assert(engine.stats().appendSamplesRequested == 512u);
    assert(engine.stats().appendSamplesAccepted == 512u);
    assert(engine.stats().appendSamplesRejected == 0u);
    assert(engine.stats().appendSamplesTruncated == 0u);
    assert(engine.stats().appendBufferedSamplesLast == 512u);

    std::vector<int16_t> out512(512, -1);
    engine.render(out512);
    for (std::size_t i = 0; i < s512.size(); ++i) {
        assert(out512[i] == s512[i]);
    }

    engine.appendRecentPcm(s512, 1u);
    assert(engine.bufferedSamples() == 0u);
    assert(engine.stats().appendSamplesRejected == 512u);

    engine.appendRecentPcm(s512, 4u);
    assert(engine.bufferedSamples() == 512u);
    assert(engine.stats().appendCallCount == 3u);
    assert(engine.stats().appendSamplesRequested == 1536u);
    assert(engine.stats().appendSamplesAccepted == 1024u);
    assert(engine.stats().appendSamplesRejected == 512u);
    assert(engine.stats().appendSamplesTruncated == 0u);

    std::vector<int16_t> s1024(1024, 0);
    for (std::size_t i = 0; i < s1024.size(); ++i) {
        s1024[i] = static_cast<int16_t>(1000 + i);
    }
    BMMQ::AudioEngine twoBlockEngine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 4096,
        .frameChunkSamples = 512,
    });
    twoBlockEngine.appendRecentPcm(s1024, 1u);
    assert(twoBlockEngine.bufferedSamples() == 1024u);
    std::vector<int16_t> out1024a(512, -1);
    std::vector<int16_t> out1024b(512, -1);
    twoBlockEngine.render(out1024a);
    twoBlockEngine.render(out1024b);
    for (std::size_t i = 0; i < 512u; ++i) {
        assert(out1024a[i] == s1024[i]);
        assert(out1024b[i] == s1024[512u + i]);
    }

    engine.resetStats();
    engine.resetStream();
    assert(engine.bufferedSamples() == 0u);
    assert(engine.stats().callbackCount == 0u);

    BMMQ::AudioEngine unity({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 1024,
        .frameChunkSamples = 512,
    });
    std::vector<int16_t> multiBlock(2048, 0);
    for (std::size_t i = 0; i < multiBlock.size(); ++i) {
        multiBlock[i] = static_cast<int16_t>(2000 + i);
    }
    unity.appendRecentPcm(multiBlock, 1u);
    const std::size_t usableCapacity = unity.bufferCapacitySamples() - 1u;
    assert(unity.bufferedSamples() == usableCapacity);
    assert(unity.stats().appendSamplesAccepted == usableCapacity);
    assert(unity.stats().appendSamplesTruncated == multiBlock.size() - usableCapacity);
    assert(unity.stats().droppedSamples == 0u);

    std::vector<int16_t> overflowOut(512, -1);
    unity.render(overflowOut);
    const std::size_t expectedStart = multiBlock.size() - usableCapacity;
    for (std::size_t i = 0; i < overflowOut.size(); ++i) {
        assert(overflowOut[i] == multiBlock[expectedStart + i]);
    }

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
    if (rollbackOut[0] != 20 || rollbackOut[1] != 21 || rollbackOut[2] != 22 || rollbackOut[3] != 23) {
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
    if (repeatedRollbackOut[0] != 50 || repeatedRollbackOut[1] != 51
        || repeatedRollbackOut[2] != 52 || repeatedRollbackOut[3] != 53) {
        std::cerr << "latest rollback seed chunk did not win before callback reset" << '\n';
        return 1;
    }

    return 0;
}
