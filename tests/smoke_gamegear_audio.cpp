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

    // Program the PSG through native Game Gear I/O ports:
    // stereo tone 0 left-only, period 0x031, loudest attenuation.
    rom[0x0000u] = 0x3Eu; rom[0x0001u] = 0x10u; // LD A,$10
    rom[0x0002u] = 0xD3u; rom[0x0003u] = 0x06u; // OUT ($06),A
    rom[0x0004u] = 0x3Eu; rom[0x0005u] = 0x81u; // LD A,$81
    rom[0x0006u] = 0xD3u; rom[0x0007u] = 0x7Eu; // OUT ($7E),A
    rom[0x0008u] = 0x3Eu; rom[0x0009u] = 0x03u; // LD A,$03
    rom[0x000Au] = 0xD3u; rom[0x000Bu] = 0x7Fu; // OUT ($7F),A
    rom[0x000Cu] = 0x3Eu; rom[0x000Du] = 0x90u; // LD A,$90
    rom[0x000Eu] = 0xD3u; rom[0x000Fu] = 0x40u; // OUT ($40),A
    rom[0x0010u] = 0xC3u; rom[0x0011u] = 0x10u; rom[0x0012u] = 0x00u; // JP $0010
    machine.loadRom(rom);

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
    assert((recentSamples.size() % 2u) == 0u);
    for (std::size_t i = 1; i < recentSamples.size(); i += 2u) {
        assert(recentSamples[i] == 0);
    }
    assert(machine.audioSampleRate() == 48000u);
    assert(machine.audioFrameCounter() > frameCounterAfterWarmup);

    assert(recorder->audioEventCount >= 2);
    assert(recorder->lastAudioState.has_value());
    assert(recorder->lastAudioState->soundEnabled());
    assert((recorder->lastAudioState->nr52 & 0x0Fu) != 0u);
    assert(recorder->lastAudioState->waveRam.size() == 0x10u);
    assert(recorder->lastAudioState->sampleRate == 48000u);
    assert(recorder->lastAudioState->channelCount == 2u);
    assert(recorder->lastAudioState->frameCounter >= 1u);
    assert(!recorder->lastAudioState->pcmSamples.empty());
    assert(hasNonZeroSample(recorder->lastAudioState->pcmSamples));

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}
