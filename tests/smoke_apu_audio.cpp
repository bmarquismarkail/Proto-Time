#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"

namespace {

struct RecordingAudioPlugin final : BMMQ::IAudioPlugin {
    int audioEventCount = 0;
    std::optional<BMMQ::AudioStateView> lastAudioState;

    std::string_view id() const override {
        return "test.audio.apu";
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

bool hasPositiveAndNegativeSample(const std::vector<int16_t>& samples)
{
    const bool hasPositive = std::any_of(samples.begin(), samples.end(), [](int16_t sample) {
        return sample > 0;
    });
    const bool hasNegative = std::any_of(samples.begin(), samples.end(), [](int16_t sample) {
        return sample < 0;
    });
    return hasPositive && hasNegative;
}

} // namespace

int main()
{
    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    machine.loadRom(cartridgeRom);

    auto audioPlugin = std::make_unique<RecordingAudioPlugin>();
    auto* recorder = audioPlugin.get();
    machine.pluginManager().add(std::move(audioPlugin));
    machine.pluginManager().initialize(machine.view());

    machine.runtimeContext().write8(0xFF26, 0x80u);
    machine.runtimeContext().write8(0xFF24, 0x77u);
    machine.runtimeContext().write8(0xFF25, 0xFFu);

    machine.runtimeContext().write8(0xFF10, 0x16u);
    machine.runtimeContext().write8(0xFF11, 0x80u);
    machine.runtimeContext().write8(0xFF12, 0xF3u);
    machine.runtimeContext().write8(0xFF13, 0x70u);
    machine.runtimeContext().write8(0xFF14, 0x87u);

    machine.runtimeContext().write8(0xFF16, 0x40u);
    machine.runtimeContext().write8(0xFF17, 0xC2u);
    machine.runtimeContext().write8(0xFF18, 0x90u);
    machine.runtimeContext().write8(0xFF19, 0x87u);

    for (uint16_t i = 0; i < 0x10u; ++i) {
        const auto nibble = static_cast<uint8_t>(i & 0x0Fu);
        const auto mirrored = static_cast<uint8_t>(0x0Fu - nibble);
        const auto packed = static_cast<uint8_t>((nibble << 4u) | mirrored);
        machine.runtimeContext().write8(static_cast<uint16_t>(0xFF30u + i), packed);
    }
    machine.runtimeContext().write8(0xFF1A, 0x80u);
    machine.runtimeContext().write8(0xFF1B, 0x20u);
    machine.runtimeContext().write8(0xFF1C, 0x20u);
    machine.runtimeContext().write8(0xFF1D, 0xA0u);
    machine.runtimeContext().write8(0xFF1E, 0x87u);

    machine.runtimeContext().write8(0xFF20, 0x00u);
    machine.runtimeContext().write8(0xFF21, 0xF2u);
    machine.runtimeContext().write8(0xFF22, 0x15u);
    machine.runtimeContext().write8(0xFF23, 0x80u);

    for (int i = 0; i < 30000; ++i) {
        machine.step();
        if (machine.audioFrameCounter() >= 3u) {
            break;
        }
    }

    const auto recentSamples = machine.recentAudioSamples();
    assert(!recentSamples.empty());
    assert(hasNonZeroSample(recentSamples));
    assert(hasPositiveAndNegativeSample(recentSamples));
    assert(machine.audioSampleRate() == 48000u);
    assert(machine.audioFrameCounter() >= 1u);

    assert(recorder->audioEventCount >= 3);
    assert(recorder->lastAudioState.has_value());
    assert(recorder->lastAudioState->soundEnabled());
    assert((recorder->lastAudioState->nr52 & 0x0Fu) != 0u);
    assert(recorder->lastAudioState->waveRam.size() == 0x10u);
    assert(recorder->lastAudioState->sampleRate == 48000u);
    assert(recorder->lastAudioState->frameCounter >= 1u);
    assert(!recorder->lastAudioState->pcmSamples.empty());
    assert(hasNonZeroSample(recorder->lastAudioState->pcmSamples));

    machine.pluginManager().shutdown(machine.view());
    return 0;
}
