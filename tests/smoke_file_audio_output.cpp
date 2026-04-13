#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#include "machine/plugins/AudioEngine.hpp"
#include "machine/plugins/audio_output/FileAudioOutput.hpp"

int main()
{
    BMMQ::AudioEngine engine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .ringBufferCapacitySamples = 1024,
        .frameChunkSamples = 256,
    });

    const auto outputPath = std::filesystem::temp_directory_path() / "bmmq_audio_out.raw";
    if (std::filesystem::exists(outputPath)) {
        std::filesystem::remove(outputPath);
    }

    BMMQ::FileAudioOutputBackend backend;
    assert(backend.open(engine, {
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 256,
        .channels = 1,
        .filePath = outputPath,
    }));
    assert(backend.ready());

    std::vector<int16_t> samples(512, 0);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>(i);
    }
    engine.appendRecentPcm(samples, 1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    backend.close();
    assert(!backend.ready());
    assert(std::filesystem::exists(outputPath));
    assert(std::filesystem::file_size(outputPath) > 0u);
    std::filesystem::remove(outputPath);
    return 0;
}
