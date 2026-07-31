#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
using GameBoyMachine = GB::GameBoyMachine;

namespace {

struct RecordingAudioPlugin final : BMMQ::IAudioPlugin {
    int audioEventCount = 0;
    int audioFrameReadyEventCount = 0;
    std::optional<BMMQ::AudioStateView> lastAudioState;
    std::vector<std::size_t> realtimePacketSampleSizes;
    std::vector<uint8_t> realtimePacketChannelCounts;
    std::vector<std::size_t> realtimePacketVoiceCounts;
    std::vector<std::size_t> realtimePacketStemSizes;
    std::size_t realtimeEventCount = 0u;
    bool stemsReconstructMix = true;

    std::string_view id() const override {
        return "test.audio.apu";
    }

    void onAudioEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override {
        ++audioEventCount;
        lastAudioState = view.audioState();
        if (event.type == BMMQ::MachineEventType::AudioFrameReady) {
            ++audioFrameReadyEventCount;
        }
        const auto packet = event.type == BMMQ::MachineEventType::AudioFrameReady
            ? view.realtimeAudioPacket()
            : std::optional<BMMQ::RealtimeAudioPacket>{};
        if (packet.has_value()) {
            realtimePacketSampleSizes.push_back(packet->pcmSamples.size());
            realtimePacketChannelCounts.push_back(packet->channelCount);
            realtimePacketVoiceCounts.push_back(packet->voices.size());
            realtimePacketStemSizes.push_back(packet->voiceStems.size());
            realtimeEventCount += packet->events.size();
            const auto voiceStride = packet->pcmSamples.size();
            if (packet->voiceStems.size() != voiceStride * 4u) {
                stemsReconstructMix = false;
            } else {
                for (std::size_t sample = 0u; sample < voiceStride; ++sample) {
                    int sum = 0;
                    for (std::size_t voice = 0u; voice < 4u; ++voice) {
                        sum += packet->voiceStems[voice * voiceStride + sample];
                    }
                    stemsReconstructMix = stemsReconstructMix &&
                        packet->pcmSamples[sample] == std::clamp(sum, -32768, 32767);
                }
            }
        }
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

std::size_t countSignTransitions(const std::vector<int16_t>& samples)
{
    std::size_t transitions = 0u;
    int lastSign = 0;
    for (const auto sample : samples) {
        if (sample == 0) {
            continue;
        }
        const int sign = sample > 0 ? 1 : -1;
        if (lastSign != 0 && sign != lastSign) {
            ++transitions;
        }
        lastSign = sign;
    }
    return transitions;
}

std::vector<int16_t> collectPulseToneSamples(uint16_t frequency)
{
    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    machine.loadRom(cartridgeRom);

    machine.runtimeContext().write8(0xFF26, 0x80u);
    machine.runtimeContext().write8(0xFF24, 0x77u);
    machine.runtimeContext().write8(0xFF25, 0x11u);
    machine.runtimeContext().write8(0xFF10, 0x00u);
    machine.runtimeContext().write8(0xFF11, 0x80u);
    machine.runtimeContext().write8(0xFF12, 0xF0u);
    machine.runtimeContext().write8(0xFF13, static_cast<uint8_t>(frequency & 0x00FFu));
    machine.runtimeContext().write8(
        0xFF14,
        static_cast<uint8_t>(0x80u | ((frequency >> 8u) & 0x07u)));

    for (int i = 0; i < 250000; ++i) {
        machine.step();
        if (machine.audioFrameCounter() >= 6u && machine.recentAudioSamples().size() >= 1024u) {
            break;
        }
    }

    auto samples = machine.recentAudioSamples();
    if (samples.size() > 1024u) {
        samples.erase(samples.begin(), samples.end() - static_cast<std::ptrdiff_t>(1024u));
    }
    return samples;
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
    machine.pluginManager().initialize(machine.mutableView());

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

    const auto frameCounterAfterWarmup = machine.audioFrameCounter();
    for (int i = 0; i < 12000; ++i) {
        machine.step();
    }

    const auto recentSamples = machine.recentAudioSamples();
    assert(!recentSamples.empty());
    assert(hasNonZeroSample(recentSamples));
    assert(hasPositiveAndNegativeSample(recentSamples));
    assert(machine.audioSampleRate() == 48000u);
    assert(machine.audioChannelCount() == 1u);
    assert(machine.audioFrameCounter() > frameCounterAfterWarmup);

    assert(recorder->audioEventCount >= 3);
    assert(recorder->audioFrameReadyEventCount >= 3);
    assert(recorder->lastAudioState.has_value());
    assert(recorder->lastAudioState->soundEnabled());
    assert((recorder->lastAudioState->nr52 & 0x0Fu) != 0u);
    assert(recorder->lastAudioState->waveRam.size() == 0x10u);
    assert(recorder->lastAudioState->sampleRate == 48000u);
    assert(recorder->lastAudioState->frameCounter >= 1u);
    assert(!recorder->lastAudioState->pcmSamples.empty());
    assert(hasNonZeroSample(recorder->lastAudioState->pcmSamples));
    assert(!recorder->realtimePacketSampleSizes.empty());
    assert(std::all_of(recorder->realtimePacketChannelCounts.begin(),
                       recorder->realtimePacketChannelCounts.end(),
                       [](uint8_t channelCount) {
                           return channelCount == 1u;
                       }));
    assert(std::any_of(recorder->realtimePacketSampleSizes.begin(),
                       recorder->realtimePacketSampleSizes.end(),
                       [](std::size_t sampleCount) {
                           return sampleCount > 0u;
                       }));
    assert(std::all_of(recorder->realtimePacketSampleSizes.begin(),
                       recorder->realtimePacketSampleSizes.end(),
                       [](std::size_t sampleCount) {
                           return sampleCount != 0u && (sampleCount % 256u) == 0u;
                       }));
    assert(std::all_of(recorder->realtimePacketVoiceCounts.begin(),
                       recorder->realtimePacketVoiceCounts.end(),
                       [](std::size_t count) { return count == 4u; }));
    assert(recorder->realtimePacketStemSizes.size() == recorder->realtimePacketSampleSizes.size());
    for (std::size_t i = 0u; i < recorder->realtimePacketStemSizes.size(); ++i) {
        assert(recorder->realtimePacketStemSizes[i] == recorder->realtimePacketSampleSizes[i] * 4u);
    }
    assert(recorder->realtimeEventCount > 0u);
    assert(recorder->stemsReconstructMix);

    const auto lowToneSamples = collectPulseToneSamples(0x0300u);
    const auto highToneSamples = collectPulseToneSamples(0x0700u);
    assert(!lowToneSamples.empty());
    assert(!highToneSamples.empty());
    const auto lowToneTransitions = countSignTransitions(lowToneSamples);
    const auto highToneTransitions = countSignTransitions(highToneSamples);
    assert(lowToneTransitions > 0u);
    assert(highToneTransitions > (lowToneTransitions * 2u));

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}
