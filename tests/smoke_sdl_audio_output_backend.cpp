#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "machine/AudioService.hpp"
#include "machine/plugins/AudioOutputPluginLoader.hpp"

int main(int argc, char** argv)
{
    assert(argc == 2);
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif

    BMMQ::AudioService service;
    auto& engine = service.engine();
    auto output = BMMQ::loadAudioOutputPlugin(argv[1], "sdl");

    assert(!output->name().empty());
    const bool opened = output->open(engine, {
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 256,
        .channels = 1,
        .filePath = {},
        .audioService = &service,
    });
    if (!opened) {
#if defined(__unix__) || defined(__APPLE__)
        std::cerr << "smoke_sdl_audio_output_backend: dummy audio open failed: "
                  << output->lastError() << '\n';
        return 1;
#else
        const auto code = output->lastErrorCode();
        assert(code == BMMQ::AudioOutputErrorCode::BackendUnavailable
               || code == BMMQ::AudioOutputErrorCode::DeviceOpenFailed
               || code == BMMQ::AudioOutputErrorCode::UnsupportedConfig);
        return 0;
#endif
    }
    assert(output->ready());
    assert(output->deviceInfo().sampleRate > 0);
    assert(output->deviceInfo().callbackChunkSamples > 0u);
    assert(output->deviceInfo().channels == 1);

    std::vector<int16_t> recent(512, 0);
    for (std::size_t i = 0; i < recent.size(); ++i) {
        recent[i] = static_cast<int16_t>(i);
    }
    service.appendRecentPcm(recent, 1u);
    for (int attempt = 0; attempt < 40 && !service.primedForDrain(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(service.primedForDrain());
    assert(service.transportStats().readyQueueDepth >= service.transportStats().prefillTargetChunks);
    output->service();
    for (int attempt = 0; attempt < 100
         && service.transportStats().drainCallbackCount == 0u; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(engine.stats().callbackCount >= 1u);
    assert(engine.stats().outputSamplesProduced >= 1u);
    assert(service.transportStats().drainCallbackCount >= 1u);
    assert(service.transportStats().workerProducedBlocks >= 1u);

    output->close();
    assert(!output->ready());

    BMMQ::AudioService resampledService;
    auto& resampledEngine = resampledService.engine();
    const bool openedResampled = output->open(resampledEngine, {
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 256,
        .channels = 1,
        .testForcedDeviceSampleRate = 44100,
        .filePath = {},
        .audioService = &resampledService,
    });
    if (!openedResampled) {
        std::cerr << "smoke_sdl_audio_output_backend: resampled open failed: "
                  << output->lastError() << '\n';
        return 1;
    }
    assert(resampledEngine.config().deviceSampleRate == 44100);
    assert(resampledEngine.stats().resamplingActive);
    output->close();

    BMMQ::AudioService stereoService(BMMQ::AudioEngineConfig{
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .channelCount = 2,
        .ringBufferCapacitySamples = 4096,
        .frameChunkSamples = 512,
    });
    auto& stereoEngine = stereoService.engine();
    const bool openedStereo = output->open(stereoEngine, {
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 512,
        .channels = 2,
        .filePath = {},
        .audioService = &stereoService,
    });
    if (!openedStereo) {
        std::cerr << "smoke_sdl_audio_output_backend: stereo open failed: "
                  << output->lastError() << '\n';
        return 1;
    }
    assert(output->deviceInfo().channels == 2);
    std::vector<int16_t> stereoRecent(1024, 0);
    for (std::size_t i = 0; i < stereoRecent.size(); i += 2u) {
        stereoRecent[i] = 1000;
        stereoRecent[i + 1u] = -1000;
    }
    stereoService.appendRecentPcm(stereoRecent, 1u);
    for (int attempt = 0; attempt < 40 && !stereoService.primedForDrain(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(stereoService.primedForDrain());
    output->service();
    for (int attempt = 0; attempt < 100
         && stereoService.transportStats().drainCallbackCount == 0u; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(stereoEngine.stats().outputSamplesProduced >= 2u);
    assert(stereoService.transportStats().drainCallbackCount >= 1u);
    output->close();

    return 0;
}
