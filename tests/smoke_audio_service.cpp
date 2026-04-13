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

    auto view = machine.view();
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
    view.audioService().engine().appendRecentPcm(fallbackSamples, 1u);
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

    machine.pluginManager().initialize(machine.view());
    auto* beforeFailedSwap = &machine.audioService();
    const bool rejected = machine.setAudioService(std::make_unique<BMMQ::AudioService>());
    assert(!rejected);
    assert(&machine.audioService() == beforeFailedSwap);
    (void)beforeFailedSwap;
    (void)rejected;

    machine.pluginManager().shutdown(machine.view());
    return 0;
}
