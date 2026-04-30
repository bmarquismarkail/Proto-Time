#include <cassert>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/AudioService.hpp"

namespace {

class PassthroughProcessor final : public BMMQ::IAudioProcessor {
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
        std::copy_n(input.samples.begin(), static_cast<std::ptrdiff_t>(input.samples.size()), output.begin());
        producedSamples = input.samples.size();
        return true;
    }
};

class NonRealtimeProcessor final : public BMMQ::IAudioProcessor {
public:
    [[nodiscard]] BMMQ::AudioProcessorCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = false,
            .fixedCapacityOutput = true,
        };
    }

    bool process(BMMQ::AudioBufferView,
                 std::span<int16_t>,
                 std::size_t& producedSamples) noexcept override
    {
        producedSamples = 0;
        return true;
    }
};

class VariableOutputProcessor final : public BMMQ::IAudioProcessor {
public:
    [[nodiscard]] BMMQ::AudioProcessorCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = true,
            .fixedCapacityOutput = false,
        };
    }

    bool process(BMMQ::AudioBufferView,
                 std::span<int16_t>,
                 std::size_t& producedSamples) noexcept override
    {
        producedSamples = 0;
        return true;
    }
};

class AddTenProcessor final : public BMMQ::IAudioProcessor {
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
        for (std::size_t i = 0; i < input.samples.size(); ++i) {
            output[i] = static_cast<int16_t>(input.samples[i] + 10);
        }
        producedSamples = input.samples.size();
        return true;
    }
};

} // namespace

int main()
{
    GameBoyMachine machine;

    auto* defaultService = &machine.audioService();
    const auto defaultConfig = defaultService->engine().config();
    assert(defaultConfig.sourceSampleRate == 48000);
    assert(defaultConfig.deviceSampleRate == 48000);
    assert(defaultConfig.ringBufferCapacitySamples == 2048);
    assert(defaultConfig.frameChunkSamples == 256);
    (void)defaultConfig;

    auto view = machine.mutableView();
    assert(view.audioService().canPerformReset());
    assert(view.audioService().resetStats());
    assert(view.audioService().resetStream());
    assert(view.audioService().addProcessor(std::make_unique<PassthroughProcessor>()));
    assert(!view.audioService().addProcessor(std::make_unique<NonRealtimeProcessor>()));
    assert(!view.audioService().addProcessor(std::make_unique<VariableOutputProcessor>()));
    assert(view.audioService().clearProcessors());
    view.audioService().setBackendPausedOrClosed(false);
    assert(!view.audioService().canPerformReset());
    assert(!view.audioService().addProcessor(std::make_unique<PassthroughProcessor>()));
    assert(!view.audioService().clearProcessors());
    view.audioService().setBackendPausedOrClosed(true);
    assert(view.audioService().canPerformReset());

    if (!view.audioService().configureFixedCallbackCapacity(4u)) {
        std::cerr << "audio service did not accept fixed callback capacity reconfiguration" << '\n';
        return 1;
    }
    if (!view.audioService().addProcessor(std::make_unique<PassthroughProcessor>())) {
        std::cerr << "audio service did not accept passthrough processor for fallback test" << '\n';
        return 1;
    }
    std::vector<int16_t> fallbackSamples = {101, 102, 103, 104, 105, 106};
    view.audioService().appendRecentPcm(fallbackSamples, 1u);
    std::vector<int16_t> fallbackOutput(6, 0);
    view.audioService().renderForOutput(std::span<int16_t>(fallbackOutput.data(), fallbackOutput.size()));
    if (fallbackOutput[0] != 101 || fallbackOutput[1] != 102 || fallbackOutput[2] != 103
        || fallbackOutput[3] != 104 || fallbackOutput[4] != 105 || fallbackOutput[5] != 106) {
        std::cerr << "audio service did not preserve dry output on callback-capacity fallback" << '\n';
        return 1;
    }
    if (view.audioService().engine().stats().pipelineCapacitySkipCount != 1u) {
        std::cerr << "audio service did not record callback-capacity fallback; count="
                  << view.audioService().engine().stats().pipelineCapacitySkipCount << '\n';
        return 1;
    }
    if (!view.audioService().clearProcessors()) {
        std::cerr << "audio service did not clear processors after fallback test" << '\n';
        return 1;
    }

    if (!view.audioService().configureOutputTransport({
            .deviceSampleRate = 48000,
            .channelCount = 1,
            .callbackChunkSamples = 4,
            .readyQueueChunks = 2,
        })) {
        std::cerr << "audio service did not configure output transport" << '\n';
        return 1;
    }
    std::vector<int16_t> emptyDrain(4, 123);
    view.audioService().drainReadyOutput(std::span<int16_t>(emptyDrain.data(), emptyDrain.size()));
    if (!std::all_of(emptyDrain.begin(), emptyDrain.end(), [](int16_t sample) { return sample == 0; })) {
        std::cerr << "empty ready output did not zero-fill" << '\n';
        return 1;
    }
    auto transportStats = view.audioService().transportStats();
    if (transportStats.underrunCount != 1u || transportStats.silenceSamplesFilled != 4u) {
        std::cerr << "empty ready output did not record underrun stats" << '\n';
        return 1;
    }

    std::vector<int16_t> transportSamples = {201, 202, 203, 204, 205, 206};
    view.audioService().appendRecentPcm(transportSamples, 2u);
    if (!view.audioService().produceReadyOutputBlock()) {
        std::cerr << "audio service did not produce ready output block" << '\n';
        return 1;
    }
    std::vector<int16_t> transportOutput(4, 0);
    view.audioService().drainReadyOutput(std::span<int16_t>(transportOutput.data(), transportOutput.size()));
    if (transportOutput[0] != 201 || transportOutput[1] != 202 ||
        transportOutput[2] != 203 || transportOutput[3] != 204) {
        std::cerr << "ready output block did not preserve source samples" << '\n';
        return 1;
    }
    transportStats = view.audioService().transportStats();
    if (transportStats.workerProducedBlocks != 1u || transportStats.drainCallbackCount != 2u) {
        std::cerr << "ready output transport stats were not updated" << '\n';
        return 1;
    }

    if (!view.audioService().addProcessor(std::make_unique<AddTenProcessor>())) {
        std::cerr << "audio service did not accept add-ten processor for transport test" << '\n';
        return 1;
    }
    view.audioService().appendRecentPcm(transportSamples, 3u);
    if (!view.audioService().produceReadyOutputBlock()) {
        std::cerr << "audio service did not produce processed ready output block" << '\n';
        return 1;
    }
    if (!view.audioService().clearProcessors()) {
        std::cerr << "audio service did not clear processors before drain" << '\n';
        return 1;
    }
    std::fill(transportOutput.begin(), transportOutput.end(), 0);
    view.audioService().drainReadyOutput(std::span<int16_t>(transportOutput.data(), transportOutput.size()));
    if (transportOutput[0] != 215 || transportOutput[1] != 216 ||
        transportOutput[2] != 211 || transportOutput[3] != 212) {
        std::cerr << "pipeline output was not captured at ready-block production time" << '\n';
        return 1;
    }

    if (!view.audioService().startOutputTransport()) {
        std::cerr << "audio service did not start output transport" << '\n';
        return 1;
    }
    assert(!view.audioService().canPerformReset());
    assert(!view.audioService().configureFixedCallbackCapacity(4u));
    assert(!view.audioService().addProcessor(std::make_unique<PassthroughProcessor>()));
    view.audioService().stopOutputTransport();
    view.audioService().setBackendPausedOrClosed(true);
    assert(view.audioService().canPerformReset());

    assert(!machine.setAudioService(nullptr));
    assert(&machine.audioService() == defaultService);

    auto replacement = std::make_unique<BMMQ::AudioService>(BMMQ::AudioEngineConfig{
        .sourceSampleRate = 48000,
        .deviceSampleRate = 44100,
        .ringBufferCapacitySamples = 1024,
        .frameChunkSamples = 256,
    });
    assert(machine.setAudioService(std::move(replacement)));
    auto* swappedService = &machine.audioService();
    assert(swappedService != defaultService);
    assert(machine.view().audioService().engine().config().deviceSampleRate == 44100);
    (void)swappedService;

    machine.pluginManager().initialize(machine.mutableView());
    auto* beforeFailedSwap = &machine.audioService();
    const bool rejected = machine.setAudioService(std::make_unique<BMMQ::AudioService>());
    assert(!rejected);
    assert(&machine.audioService() == beforeFailedSwap);
    (void)beforeFailedSwap;
    (void)rejected;

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}
