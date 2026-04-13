#include <cassert>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/AudioService.hpp"

namespace {

class PassthroughProcessor final : public BMMQ::IAudioProcessor {
public:
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
    view.audioService().setBackendPausedOrClosed(false);
    assert(!view.audioService().canPerformReset());
    assert(!view.audioService().addProcessor(std::make_unique<PassthroughProcessor>()));
    assert(!view.audioService().clearProcessors());
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
