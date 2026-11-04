#include "machine/AudioService.hpp"

#include <algorithm>
#include <array>
#include <iostream>

int main()
{
    BMMQ::AudioService audio(BMMQ::AudioEngineConfig{
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .channelCount = 2,
        .ringBufferCapacitySamples = 4096,
        .frameChunkSamples = 512,
    });
    if (!audio.configureOutputTransport({48000, 2, 512, 3})) return 1;
    std::array<int16_t, 1536> pcm{};
    pcm.fill(1234);
    audio.appendRecentPcm(pcm, 1);
    if (!audio.produceReadyOutputBlock() || !audio.produceReadyOutputBlock()) return 1;
    // A live PulseAudio device can request three blocks immediately at start.
    // Two prepared blocks must not admit that initial consumer burst.
    if (audio.primedForDrain()) {
        std::cerr << "startup admitted a three-callback device burst with only two ready blocks\n";
        return 1;
    }
    if (!audio.produceReadyOutputBlock() || !audio.primedForDrain()) return 1;
    std::array<int16_t, 512> output{};
    for (unsigned callback = 0; callback < 3; ++callback) {
        audio.drainReadyOutput(output);
        if (!std::all_of(output.begin(), output.end(), [](auto sample) { return sample == 1234; })) {
            std::cerr << "initial device burst drained unprepared audio\n";
            return 1;
        }
    }
    if (audio.transportStats().underrunCount != 0) return 1;
    audio.drainReadyOutput(output);
    if (audio.transportStats().underrunCount != 1 ||
        audio.transportStats().silenceSamplesFilled != output.size()) {
        std::cerr << "genuine starvation after the prepared burst was not counted\n";
        return 1;
    }
    return 0;
}
