#include <cassert>
#include <chrono>
#include <cstddef>
#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>

#include "machine/plugins/audio_output/DummyAudioOutput.hpp"
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
    if (!service.addProcessor(std::make_unique<ZeroProcessor>())) {
        std::cerr << "dummy audio output test could not register zero processor" << '\n';
        return 1;
    }

    BMMQ::DummyAudioOutputBackend backend;
    if (!backend.open(engine, {
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 256,
        .channels = 1,
        .filePath = {},
        .audioService = &service,
    })) {
        std::cerr << "dummy audio output backend failed to open: " << backend.lastError() << '\n';
        return 1;
    }
    if (!backend.ready()) {
        std::cerr << "dummy audio output backend did not report ready after open" << '\n';
        return 1;
    }

    std::vector<int16_t> samples(512, 0);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>(i);
    }
    engine.appendRecentPcm(samples, 1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    if (engine.stats().outputSamplesProduced == 0u) {
        std::cerr << "dummy audio output backend did not render any audio while open" << '\n';
        return 1;
    }

    backend.close();
    if (backend.ready()) {
        std::cerr << "dummy audio output backend still reports ready after close" << '\n';
        return 1;
    }

    engine.appendRecentPcm(samples, 2u);
    std::vector<int16_t> output(256, 123);
    service.renderForOutput(std::span<int16_t>(output.data(), output.size()));
    for (auto sample : output) {
        if (sample != 0) {
            std::cerr << "dummy audio output fallback sample was " << sample << '\n';
            return 1;
        }
    }
    return 0;
}
