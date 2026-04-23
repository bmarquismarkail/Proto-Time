#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"

namespace {

struct RecordingAudioPlugin final : BMMQ::IAudioPlugin {
    int audioEventCount = 0;
    std::optional<BMMQ::AudioStateView> lastAudioState;

    std::string_view id() const override {
        return "test.audio.gamegear";
    }

    void onAudioEvent(const BMMQ::MachineEvent&, const BMMQ::MachineView& view) override {
        ++audioEventCount;
        lastAudioState = view.audioState();
    }
};

bool hasNonZeroSample(const std::vector<int16_t>& samples)
{
    return std::any_of(samples.begin(), samples.end(), [](int16_t sample) {
        return sample != 0;
    });
}

} // namespace

int main()
{
    BMMQ::GameGearMachine machine;
    std::vector<uint8_t> rom(0x4000u, 0x00u);
    machine.loadRom(rom);

    auto audioPlugin = std::make_unique<RecordingAudioPlugin>();
    auto* recorder = audioPlugin.get();
    machine.pluginManager().add(std::move(audioPlugin));
    machine.pluginManager().initialize(machine.mutableView());

    machine.runtimeContext().write8(0xFF26u, 0x80u);
    machine.runtimeContext().write8(0xFF24u, 0x77u);
    machine.runtimeContext().write8(0xFF25u, 0x11u);
    machine.runtimeContext().write8(0xFF12u, 0xF0u);
    machine.runtimeContext().write8(0xFF13u, 0x01u);
    machine.runtimeContext().write8(0xFF14u, 0x80u);

    for (uint16_t i = 0; i < 0x10u; ++i) {
        machine.runtimeContext().write8(static_cast<uint16_t>(0xFF30u + i), static_cast<uint8_t>(i));
    }

    for (int i = 0; i < 50000; ++i) {
        machine.step();
        if (machine.audioFrameCounter() >= 2u) {
            break;
        }
    }

    const auto frameCounterAfterWarmup = machine.audioFrameCounter();
    for (int i = 0; i < 12000; ++i) {
        machine.step();
    }

    const auto recentSamples = machine.recentAudioSamples();
    assert(!recentSamples.empty());
    assert(hasNonZeroSample(recentSamples));
    assert(machine.audioSampleRate() == 48000u);
    assert(machine.audioFrameCounter() > frameCounterAfterWarmup);

    assert(recorder->audioEventCount >= 2);
    assert(recorder->lastAudioState.has_value());
    assert(recorder->lastAudioState->soundEnabled());
    assert((recorder->lastAudioState->nr52 & 0x0Fu) != 0u);
    assert(recorder->lastAudioState->waveRam.size() == 0x10u);
    assert(recorder->lastAudioState->sampleRate == 48000u);
    assert(recorder->lastAudioState->frameCounter >= 1u);
    assert(!recorder->lastAudioState->pcmSamples.empty());
    assert(hasNonZeroSample(recorder->lastAudioState->pcmSamples));

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}
