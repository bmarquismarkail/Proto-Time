#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

#include "machine/plugins/AudioEngine.hpp"
#include "machine/plugins/audio_output/DummyAudioOutput.hpp"

int main()
{
    BMMQ::AudioEngine engine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 1024,
        .frameChunkSamples = 256,
    });

    BMMQ::DummyAudioOutputBackend backend;
    assert(backend.open(engine, {
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 256,
        .channels = 1,
    }));
    assert(backend.ready());

    std::vector<int16_t> samples(512, 0);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>(i);
    }
    engine.appendRecentPcm(samples, 1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(engine.stats().outputSamplesProduced > 0u);

    backend.close();
    assert(!backend.ready());
    return 0;
}
