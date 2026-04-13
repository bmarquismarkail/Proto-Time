#include <cassert>
#include <chrono>
#include <cstddef>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <vector>

#include "machine/plugins/audio_output/FileAudioOutput.hpp"
#include "machine/AudioService.hpp"

int main()
{
    BMMQ::AudioService service;
    auto& engine = service.engine();

    class ZeroProcessor final : public BMMQ::IAudioProcessor {
    public:
        [[nodiscard]] BMMQ::AudioProcessorCapabilities capabilities() const noexcept override
        {
            return {
                .realtimeSafe = true,
                .fixedCapacityOutput = true,
            };
        }

        bool process(BMMQ::AudioBufferView input,
                     std::span<int16_t> output,
                     std::size_t& producedSamples) noexcept override
        {
            if (output.size() < input.samples.size()) {
                producedSamples = 0;
                return false;
            }
            std::fill_n(output.begin(), static_cast<std::ptrdiff_t>(input.samples.size()), 0);
            producedSamples = input.samples.size();
            return true;
        }
    };
    assert(service.addProcessor(std::make_unique<ZeroProcessor>()));

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
        .audioService = &service,
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

    engine.appendRecentPcm(samples, 2u);
    std::vector<int16_t> output(256, 123);
    service.renderForOutput(std::span<int16_t>(output.data(), output.size()));
    for ([[maybe_unused]] auto sample : output) {
        assert(sample == 0);
    }
    assert(std::filesystem::exists(outputPath));
    assert(std::filesystem::file_size(outputPath) > 0u);
    std::filesystem::remove(outputPath);
    return 0;
}
