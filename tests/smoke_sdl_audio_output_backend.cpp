#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

#include "machine/AudioService.hpp"
#include "machine/plugins/AudioOutput.hpp"
#include "machine/plugins/sdl_frontend/SdlAudioOutput.hpp"

int main()
{
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif

    BMMQ::AudioService service;
    auto& engine = service.engine();
    BMMQ::SdlAudioOutputBackend output;

    assert(!output.name().empty());
    assert(output.open(engine, {
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 256,
        .channels = 1,
        .filePath = {},
        .audioService = &service,
    }));
    assert(output.ready());
    assert(output.deviceInfo().sampleRate > 0);
    assert(output.deviceInfo().callbackChunkSamples > 0u);
    assert(output.deviceInfo().channels == 1);

    std::vector<int16_t> recent(512, 0);
    for (std::size_t i = 0; i < recent.size(); ++i) {
        recent[i] = static_cast<int16_t>(i);
    }
    engine.appendRecentPcm(recent, 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(engine.stats().callbackCount >= 1u);
    assert(engine.stats().outputSamplesProduced >= 1u);

    output.close();
    assert(!output.ready());

    BMMQ::AudioService resampledService;
    auto& resampledEngine = resampledService.engine();
    (void)resampledEngine;
    assert(output.open(resampledEngine, {
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 256,
        .channels = 1,
        .testForcedDeviceSampleRate = 44100,
        .filePath = {},
        .audioService = &resampledService,
    }));
    assert(resampledEngine.config().deviceSampleRate == 44100);
    assert(resampledEngine.stats().resamplingActive);
    output.close();

    return 0;
}
