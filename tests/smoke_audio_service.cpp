#include <cassert>
#include <cstdint>
#include <memory>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/AudioService.hpp"

int main()
{
    GameBoyMachine machine;

    auto* defaultService = &machine.audioService();
    const auto defaultConfig = defaultService->engine().config();
    assert(defaultConfig.sourceSampleRate == 48000);
    assert(defaultConfig.deviceSampleRate == 48000);
    assert(defaultConfig.ringBufferCapacitySamples == 2048);
    assert(defaultConfig.frameChunkSamples == 256);

    auto view = machine.view();
    assert(view.audioService().canPerformReset());
    view.audioService().resetStats();
    view.audioService().resetStream();
    view.audioService().setBackendPausedOrClosed(false);
    assert(!view.audioService().canPerformReset());
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

    machine.pluginManager().initialize(machine.view());
    auto* beforeFailedSwap = &machine.audioService();
    auto rejected = machine.setAudioService(std::make_unique<BMMQ::AudioService>());
    assert(!rejected);
    assert(&machine.audioService() == beforeFailedSwap);

    machine.pluginManager().shutdown(machine.view());
    return 0;
}
