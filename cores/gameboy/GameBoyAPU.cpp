#include "GameBoyAPU.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace GB {

// Duty cycle patterns (Pan Docs)
static constexpr std::array<std::array<uint8_t, 8>, 4> kDutyPatterns{{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // 12.5%
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF}, // 25%
    {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF}, // 50%
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00}, // 75%
}};

GameBoyAPU::GameBoyAPU() {
    apu_ = GameBoyAPUState{};
}

void GameBoyAPU::reset() {
    apu_ = GameBoyAPUState{};
    apu_.masterEnabled = true;
    recentVoiceStems_ = {};
    pendingEvents_.clear();
    pendingEventDropCount_ = 0u;
    eventSequence_ = 0u;
    observedGateStates_.fill(false);
    for (std::uint8_t voice = 0u; voice < 4u; ++voice) {
        recordVoiceEvent(voice, BMMQ::PsgEventKind::StateSnapshot);
    }
}

uint16_t GameBoyAPU::pulseTimerPeriod(uint16_t frequency) const noexcept
{
    const auto sanitized = static_cast<uint16_t>(frequency & 0x07FFu);
    return static_cast<uint16_t>(std::max<uint32_t>(4u, (2048u - sanitized) * 4u));
}

uint16_t GameBoyAPU::waveTimerPeriod(uint16_t frequency) const noexcept
{
    const auto sanitized = static_cast<uint16_t>(frequency & 0x07FFu);
    return static_cast<uint16_t>(std::max<uint32_t>(2u, (2048u - sanitized) * 2u));
}

uint16_t GameBoyAPU::noiseTimerPeriod() const noexcept
{
    static constexpr std::array<uint16_t, 8> kDivisors{{8u, 16u, 32u, 48u, 64u, 80u, 96u, 112u}};
    const auto divisor = kDivisors[apu_.noise.divisorCode & 0x07u];
    const auto period = static_cast<uint32_t>(divisor) << apu_.noise.clockShift;
    return static_cast<uint16_t>(std::min<uint32_t>(std::max<uint32_t>(period, 8u), 0xFFFFu));
}

void GameBoyAPU::step(uint32_t cpuCycles) {
    for (uint32_t cycle = 0; cycle < cpuCycles; ++cycle) {
        if (apu_.masterEnabled) {
            ++apu_.frameSequencerCounter;
            if (apu_.frameSequencerCounter >= kCyclesPerFrameStep) {
                apu_.frameSequencerCounter = 0u;
                stepFrameSequencer();
            }

            const auto tickPulseTimer = [this](PulseChannel& channel) {
                if (channel.timer > 0u) {
                    --channel.timer;
                }
                if (channel.timer == 0u) {
                    channel.timer = pulseTimerPeriod(channel.frequency);
                    channel.dutyStep = static_cast<uint8_t>((channel.dutyStep + 1u) & 0x07u);
                }
            };

            tickPulseTimer(apu_.pulse1);
            tickPulseTimer(apu_.pulse2);

            if (apu_.wave.timer > 0u) {
                --apu_.wave.timer;
            }
            if (apu_.wave.timer == 0u) {
                apu_.wave.timer = waveTimerPeriod(apu_.wave.frequency);
                apu_.wave.sampleIndex = static_cast<uint8_t>((apu_.wave.sampleIndex + 1u) & 0x1Fu);
            }

            if (apu_.noise.timer > 0u) {
                --apu_.noise.timer;
            }
            if (apu_.noise.timer == 0u) {
                apu_.noise.timer = noiseTimerPeriod();
                const uint16_t feedback = static_cast<uint16_t>((apu_.noise.lfsr ^ (apu_.noise.lfsr >> 1u)) & 0x01u);
                apu_.noise.lfsr = static_cast<uint16_t>((apu_.noise.lfsr >> 1u) | (feedback << 14u));
                if (apu_.noise.widthMode7) {
                    apu_.noise.lfsr = static_cast<uint16_t>((apu_.noise.lfsr & ~0x0040u) | (feedback << 6u));
                }
                apu_.noise.lfsr = static_cast<uint16_t>(apu_.noise.lfsr & 0x7FFFu);
            }
        }

        apu_.sampleAccumulator += kSampleRate;
        while (apu_.sampleAccumulator >= kMasterClockHz) {
            apu_.sampleAccumulator -= kMasterClockHz;
            const auto stems = currentVoiceContributions();
            int mixed = 0;
            for (const auto stem : stems) mixed += stem;
            pushSample(static_cast<int16_t>(std::clamp(mixed, -32768, 32767)), stems);
        }
    }
}

void GameBoyAPU::stepFrameSequencer() {
    switch (apu_.frameSequencerStep) {
    case 0:
    case 2:
    case 4:
    case 6:
        tickLengthCounters();
        if (apu_.frameSequencerStep == 2u || apu_.frameSequencerStep == 6u) {
            tickSweep();
        }
        break;
    case 7:
        tickEnvelope(apu_.pulse1, 0u);
        tickEnvelope(apu_.pulse2, 1u);
        tickEnvelope(apu_.noise, 3u);
        break;
    default:
        break;
    }

    apu_.frameSequencerStep = static_cast<uint8_t>((apu_.frameSequencerStep + 1u) & 0x07u);
}

void GameBoyAPU::tickLengthCounters() {
    // Pulse 1
    if (apu_.pulse1.lengthEnabled && apu_.pulse1.lengthCounter > 0) {
        apu_.pulse1.lengthCounter--;
        if (apu_.pulse1.lengthCounter == 0) apu_.pulse1.enabled = false;
    }
    // Pulse 2
    if (apu_.pulse2.lengthEnabled && apu_.pulse2.lengthCounter > 0) {
        apu_.pulse2.lengthCounter--;
        if (apu_.pulse2.lengthCounter == 0) apu_.pulse2.enabled = false;
    }
    // Wave
    if (apu_.wave.lengthEnabled && apu_.wave.lengthCounter > 0) {
        apu_.wave.lengthCounter--;
        if (apu_.wave.lengthCounter == 0) apu_.wave.enabled = false;
    }
    // Noise
    if (apu_.noise.lengthEnabled && apu_.noise.lengthCounter > 0) {
        apu_.noise.lengthCounter--;
        if (apu_.noise.lengthCounter == 0) apu_.noise.enabled = false;
    }
}

void GameBoyAPU::tickSweep() {
    if (!apu_.pulse1.sweepEnabled || apu_.pulse1.sweepPeriod == 0 || apu_.pulse1.sweepShift == 0) {
        return;
    }

    apu_.pulse1.sweepTimer--;
    if (apu_.pulse1.sweepTimer > 0) return;
    apu_.pulse1.sweepTimer = apu_.pulse1.sweepPeriod;

    const uint16_t delta = static_cast<uint16_t>(apu_.pulse1.shadowFrequency >> apu_.pulse1.sweepShift);
    const uint16_t newFreq = apu_.pulse1.sweepNegate
        ? static_cast<uint16_t>(apu_.pulse1.shadowFrequency - delta)
        : static_cast<uint16_t>(apu_.pulse1.shadowFrequency + delta);
    if (newFreq > 0x07FFu) {
        apu_.pulse1.enabled = false; // Overflow
        return;
    }

    apu_.pulse1.shadowFrequency = newFreq;
    apu_.pulse1.frequency = newFreq;
}

void GameBoyAPU::tickEnvelope(PulseChannel& channel, std::uint8_t voice) {
    if (channel.envelopePeriod == 0) return;
    channel.envelopeTimer--;
    if (channel.envelopeTimer > 0) return;
    channel.envelopeTimer = channel.envelopePeriod;

    const auto previousVolume = channel.volume;
    if (channel.envelopeIncrease) {
        if (channel.volume < 15) {
            channel.volume++;
        }
    } else {
        if (channel.volume > 0) {
            channel.volume--;
        }
    }
    if (channel.volume != previousVolume) {
        recordVoiceEvent(voice, BMMQ::PsgEventKind::LevelChange);
    }
}

void GameBoyAPU::tickEnvelope(NoiseChannel& channel, std::uint8_t voice) {
    if (channel.envelopePeriod == 0) return;
    channel.envelopeTimer--;
    if (channel.envelopeTimer > 0) return;
    channel.envelopeTimer = channel.envelopePeriod;

    const auto previousVolume = channel.volume;
    if (channel.envelopeIncrease) {
        if (channel.volume < 15) {
            channel.volume++;
        }
    } else {
        if (channel.volume > 0) {
            channel.volume--;
        }
    }
    if (channel.volume != previousVolume) {
        recordVoiceEvent(voice, BMMQ::PsgEventKind::LevelChange);
    }
}

int GameBoyAPU::currentPulseSample(const PulseChannel& channel) const noexcept
{
    if (!apu_.masterEnabled || !channel.enabled || !channel.dacEnabled || channel.volume == 0u) {
        return 0;
    }

    const auto polarity = kDutyPatterns[channel.duty & 0x03u][channel.dutyStep & 0x07u] != 0u ? 1 : -1;
    return polarity * static_cast<int>(channel.volume);
}

int GameBoyAPU::currentWaveSample() const noexcept
{
    if (!apu_.masterEnabled || !apu_.wave.enabled || !apu_.wave.dacEnabled) {
        return 0;
    }

    const auto outputLevel = static_cast<uint8_t>(apu_.wave.outputLevel & 0x03u);
    if (outputLevel == 0u) {
        return 0;
    }

    const auto sampleIndex = static_cast<uint8_t>(apu_.wave.sampleIndex & 0x1Fu);
    const auto packed = apu_.waveRam[sampleIndex / 2u];
    uint8_t sample = (sampleIndex & 0x01u) == 0u
        ? static_cast<uint8_t>((packed >> 4u) & 0x0Fu)
        : static_cast<uint8_t>(packed & 0x0Fu);
    if (outputLevel == 2u) {
        sample >>= 1u;
    } else if (outputLevel == 3u) {
        sample >>= 2u;
    }

    return (static_cast<int>(sample) - 8) * 2;
}

int GameBoyAPU::currentNoiseSample() const noexcept
{
    if (!apu_.masterEnabled || !apu_.noise.enabled || !apu_.noise.dacEnabled || apu_.noise.volume == 0u) {
        return 0;
    }

    const auto polarity = (apu_.noise.lfsr & 0x01u) == 0u ? 1 : -1;
    return polarity * static_cast<int>(apu_.noise.volume);
}

std::array<int16_t, 4u> GameBoyAPU::currentVoiceContributions() const noexcept
{
    const std::array<int, 4u> raw{{currentPulseSample(apu_.pulse1), currentPulseSample(apu_.pulse2),
                                   currentWaveSample(), currentNoiseSample()}};
    const int leftVolume = static_cast<int>((apu_.nr50 >> 4u) & 0x07u) + 1;
    const int rightVolume = static_cast<int>(apu_.nr50 & 0x07u) + 1;
    std::array<int16_t, 4u> result{};
    for (std::size_t voice = 0u; voice < result.size(); ++voice) {
        const int left = (apu_.nr51 & static_cast<std::uint8_t>(0x10u << voice)) != 0u ? raw[voice] : 0;
        const int right = (apu_.nr51 & static_cast<std::uint8_t>(0x01u << voice)) != 0u ? raw[voice] : 0;
        result[voice] = static_cast<int16_t>(std::clamp((left * leftVolume + right * rightVolume) * 32,
                                                       -32768, 32767));
    }
    return result;
}

void GameBoyAPU::pushSample(int16_t sample, const std::array<int16_t, 4u>& stems) {
    std::size_t cursor = apu_.recentWriteCursor;
    apu_.recentSamples[cursor] = sample;
    for (std::size_t voice = 0u; voice < stems.size(); ++voice) recentVoiceStems_[voice][cursor] = stems[voice];
    apu_.recentWriteCursor = (cursor + 1) % kHistorySamples;
    if (apu_.recentSampleCount < kHistorySamples) {
        apu_.recentSampleCount++;
    }
    ++apu_.sampleCounter;
    if ((apu_.sampleCounter % kFrameChunkSamples) == 0u) {
        ++apu_.frameCounter;
    }
    if (apu_.pendingSampleCount < kHistorySamples) {
        apu_.pendingSampleCount++;
    } else {
        apu_.pendingReadCursor = (apu_.pendingReadCursor + 1u) % kHistorySamples;
    }
}

void GameBoyAPU::writeRegister(uint16_t address, uint8_t value) {
    switch (address) {
    case 0xFF10: // NR10
        if (!apu_.masterEnabled) break;
        apu_.pulse1.sweepPeriod = (value >> 4) & 0x07u;
        apu_.pulse1.sweepNegate = (value & 0x08u) != 0;
        apu_.pulse1.sweepShift = value & 0x07u;
        apu_.pulse1.sweepEnabled = apu_.pulse1.sweepPeriod != 0 || apu_.pulse1.sweepShift != 0;
        break;
    case 0xFF11: // NR11
        if (!apu_.masterEnabled) break;
        apu_.pulse1.duty = (value >> 6u) & 0x03u;
        apu_.pulse1.lengthCounter = 64 - (value & 0x3Fu);
        break;
    case 0xFF12: // NR12
        if (!apu_.masterEnabled) break;
        apu_.pulse1.dacEnabled = (value & 0xF8u) != 0;
        if (!apu_.pulse1.dacEnabled) {
            apu_.pulse1.enabled = false;
        }
        apu_.pulse1.envelopeIncrease = (value & 0x08u) != 0;
        apu_.pulse1.envelopePeriod = value & 0x07u;
        apu_.pulse1.initialVolume = (value >> 4) & 0x0Fu;
        apu_.pulse1.volume = apu_.pulse1.initialVolume;
        apu_.pulse1.envelopeTimer = apu_.pulse1.envelopePeriod;
        break;
    case 0xFF13: // NR13
        if (!apu_.masterEnabled) break;
        apu_.pulse1.frequency = (apu_.pulse1.frequency & 0x0700u) | value;
        break;
    case 0xFF14: // NR14
        if (!apu_.masterEnabled) break;
        apu_.pulse1.frequency = (value & 0x07u) << 8 | (apu_.pulse1.frequency & 0x00FFu);
        apu_.pulse1.lengthEnabled = (value & 0x40u) != 0;
        if ((value & 0x80u) != 0) {
            if (apu_.pulse1.lengthCounter == 0) {
                apu_.pulse1.lengthCounter = 64;
            }
            apu_.pulse1.volume = apu_.pulse1.initialVolume;
            apu_.pulse1.envelopeTimer = apu_.pulse1.envelopePeriod;
            apu_.pulse1.dutyStep = 0;
            apu_.pulse1.timer = pulseTimerPeriod(apu_.pulse1.frequency);
            apu_.pulse1.enabled = apu_.pulse1.dacEnabled;
        }
        apu_.pulse1.hasSweep = apu_.pulse1.sweepPeriod != 0 || apu_.pulse1.sweepShift != 0;
        apu_.pulse1.sweepEnabled = apu_.pulse1.hasSweep;
        apu_.pulse1.shadowFrequency = apu_.pulse1.frequency;
        apu_.pulse1.sweepTimer = apu_.pulse1.sweepPeriod;
        break;
    case 0xFF16: // NR21
        if (!apu_.masterEnabled) break;
        apu_.pulse2.duty = (value >> 6u) & 0x03u;
        apu_.pulse2.lengthCounter = 64 - (value & 0x3Fu);
        break;
    case 0xFF17: // NR22
        if (!apu_.masterEnabled) break;
        apu_.pulse2.dacEnabled = (value & 0xF8u) != 0;
        if (!apu_.pulse2.dacEnabled) {
            apu_.pulse2.enabled = false;
        }
        apu_.pulse2.envelopeIncrease = (value & 0x08u) != 0;
        apu_.pulse2.envelopePeriod = value & 0x07u;
        apu_.pulse2.initialVolume = (value >> 4) & 0x0Fu;
        apu_.pulse2.volume = apu_.pulse2.initialVolume;
        apu_.pulse2.envelopeTimer = apu_.pulse2.envelopePeriod;
        break;
    case 0xFF18: // NR23
        if (!apu_.masterEnabled) break;
        apu_.pulse2.frequency = (apu_.pulse2.frequency & 0x0700u) | value;
        break;
    case 0xFF19: // NR24
        if (!apu_.masterEnabled) break;
        apu_.pulse2.frequency = (value & 0x07u) << 8 | (apu_.pulse2.frequency & 0x00FFu);
        apu_.pulse2.lengthEnabled = (value & 0x40u) != 0;
        if ((value & 0x80u) != 0) {
            if (apu_.pulse2.lengthCounter == 0) {
                apu_.pulse2.lengthCounter = 64;
            }
            apu_.pulse2.volume = apu_.pulse2.initialVolume;
            apu_.pulse2.envelopeTimer = apu_.pulse2.envelopePeriod;
            apu_.pulse2.dutyStep = 0;
            apu_.pulse2.timer = pulseTimerPeriod(apu_.pulse2.frequency);
            apu_.pulse2.enabled = apu_.pulse2.dacEnabled;
        }
        break;
    case 0xFF1A: // NR30
        if (!apu_.masterEnabled) break;
        apu_.wave.dacEnabled = (value & 0x80u) != 0;
        if (!apu_.wave.dacEnabled) apu_.wave.enabled = false;
        break;
    case 0xFF1B: // NR31
        if (!apu_.masterEnabled) break;
        apu_.wave.lengthCounter = 256 - value;
        break;
    case 0xFF1C: // NR32
        if (!apu_.masterEnabled) break;
        apu_.wave.outputLevel = static_cast<uint8_t>((value >> 5u) & 0x03u);
        break;
    case 0xFF1D: // NR33
        if (!apu_.masterEnabled) break;
        apu_.wave.frequency = (apu_.wave.frequency & 0x0700u) | value;
        break;
    case 0xFF1E: // NR34
        if (!apu_.masterEnabled) break;
        apu_.wave.frequency = (value & 0x07u) << 8 | (apu_.wave.frequency & 0x00FFu);
        apu_.wave.lengthEnabled = (value & 0x40u) != 0;
        if ((value & 0x80u) != 0 && apu_.wave.lengthCounter == 0) {
            apu_.wave.lengthCounter = 256;
        }
        if ((value & 0x80u) != 0) {
            apu_.wave.enabled = apu_.wave.dacEnabled;
            apu_.wave.timer = waveTimerPeriod(apu_.wave.frequency);
            apu_.wave.sampleIndex = 0;
        }
        break;
    case 0xFF20: // NR41
        if (!apu_.masterEnabled) break;
        apu_.noise.lengthCounter = 64 - (value & 0x3Fu);
        break;
    case 0xFF21: // NR42
        if (!apu_.masterEnabled) break;
        apu_.noise.dacEnabled = (value & 0xF8u) != 0;
        if (!apu_.noise.dacEnabled) {
            apu_.noise.enabled = false;
        }
        apu_.noise.envelopeIncrease = (value & 0x08u) != 0;
        apu_.noise.envelopePeriod = value & 0x07u;
        apu_.noise.initialVolume = (value >> 4) & 0x0Fu;
        apu_.noise.volume = apu_.noise.initialVolume;
        apu_.noise.envelopeTimer = apu_.noise.envelopePeriod;
        break;
    case 0xFF22: // NR43
        if (!apu_.masterEnabled) break;
        apu_.noise.clockShift = (value >> 4);
        apu_.noise.divisorCode = value & 0x07u;
        apu_.noise.widthMode7 = (value & 0x08u) != 0;
        break;
    case 0xFF23: // NR44
        if (!apu_.masterEnabled) break;
        apu_.noise.lengthEnabled = (value & 0x40u) != 0;
        if ((value & 0x80u) != 0) {
            if (apu_.noise.lengthCounter == 0) {
                apu_.noise.lengthCounter = 64;
            }
            apu_.noise.enabled = apu_.noise.dacEnabled;
            apu_.noise.volume = apu_.noise.initialVolume;
            apu_.noise.envelopeTimer = apu_.noise.envelopePeriod;
            apu_.noise.lfsr = 0x7FFFu;
            apu_.noise.timer = noiseTimerPeriod();
        }
        break;
    case 0xFF24: // NR50 - volume / mixing
        if (!apu_.masterEnabled) break;
        apu_.nr50 = value;
        break;
    case 0xFF25: // NR51 - PWR NR
        if (!apu_.masterEnabled) break;
        apu_.nr51 = value;
        break;
    case 0xFF26: // NR52 - APU power control
        apu_.masterEnabled = (value & 0x80u) != 0;
        apu_.nr52 = value;
        if (!apu_.masterEnabled) {
            apu_.pulse1 = PulseChannel{};
            apu_.pulse2 = PulseChannel{};
            apu_.wave = WaveChannel{};
            apu_.noise = NoiseChannel{};
            apu_.nr50 = 0;
            apu_.nr51 = 0;
        }
        break;
    default:
        if (address >= 0xFF30u && address <= 0xFF3Fu) {
            apu_.waveRam[address - 0xFF30u] = value;
        }
        break;
    }
    recordWriteEvent(address, value);
}

uint8_t GameBoyAPU::readRegister(uint16_t address) const {
    switch (address) {
    case 0xFF24:
        return apu_.nr50;
    case 0xFF25:
        return apu_.nr51;
    case 0xFF26: // NR52
        return static_cast<uint8_t>((0x70u | (apu_.masterEnabled ? 0x80u : 0u)) |
               (((apu_.pulse1.enabled ? 0x01u : 0u) |
                 (apu_.pulse2.enabled ? 0x02u : 0u) |
                 (apu_.wave.enabled ? 0x04u : 0u) |
                 (apu_.noise.enabled ? 0x08u : 0u)) & 0x0Fu));
    default:
        if (address >= 0xFF30u && address <= 0xFF3Fu) {
            return apu_.waveRam[address - 0xFF30u];
        }
        return 0xFFu;
    }
}

std::vector<int16_t> GameBoyAPU::copyRecentSamples() const {
    std::vector<int16_t> samples;
    if (apu_.recentSampleCount == 0) return samples;

    std::size_t start = (apu_.recentWriteCursor - apu_.recentSampleCount + kHistorySamples) % kHistorySamples;
    samples.reserve(apu_.recentSampleCount);
    for (std::size_t i = 0; i < apu_.recentSampleCount; ++i) {
        samples.push_back(apu_.recentSamples[(start + i) % kHistorySamples]);
    }
    return samples;
}

std::vector<int16_t> GameBoyAPU::takePendingSamples() const {
    std::vector<int16_t> samples;
    if (apu_.pendingSampleCount == 0) return samples;
    samples.reserve(apu_.pendingSampleCount);
    for (std::size_t i = 0; i < apu_.pendingSampleCount; ++i) {
        samples.push_back(apu_.recentSamples[apu_.pendingReadCursor]);
        apu_.pendingReadCursor = (apu_.pendingReadCursor + 1) % kHistorySamples;
    }
    apu_.pendingSampleCount = 0;
    return samples;
}

std::vector<int16_t> GameBoyAPU::copyPendingVoiceStems() const
{
    std::vector<int16_t> samples;
    samples.reserve(apu_.pendingSampleCount * 4u);
    for (std::size_t voice = 0u; voice < 4u; ++voice) {
        auto cursor = apu_.pendingReadCursor;
        for (std::size_t i = 0u; i < apu_.pendingSampleCount; ++i) {
            samples.push_back(recentVoiceStems_[voice][cursor]);
            cursor = (cursor + 1u) % kHistorySamples;
        }
    }
    return samples;
}

std::vector<BMMQ::PsgAudioEvent> GameBoyAPU::takePendingEvents(std::uint64_t firstSampleFrame) const
{
    std::vector<BMMQ::PsgAudioEvent> events;
    events.reserve(pendingEvents_.size());
    while (!pendingEvents_.empty()) {
        auto pending = std::move(pendingEvents_.front());
        pendingEvents_.pop_front();
        pending.event.sampleFrameOffset = static_cast<std::uint32_t>(
            pending.absoluteSampleFrame > firstSampleFrame
                ? pending.absoluteSampleFrame - firstSampleFrame : 0u);
        events.push_back(std::move(pending.event));
    }
    return events;
}

std::uint64_t GameBoyAPU::takePendingEventDropCount() const noexcept
{
    const auto count = pendingEventDropCount_;
    pendingEventDropCount_ = 0u;
    return count;
}

void GameBoyAPU::recordWriteEvent(std::uint16_t address, std::uint8_t value)
{
    if (address == 0xFF24u) {
        for (std::uint8_t voice = 0u; voice < 4u; ++voice) {
            recordVoiceEvent(voice, BMMQ::PsgEventKind::LevelChange, address, value, true);
        }
        recordRawWriteEvent(0u, address, value);
        return;
    }
    if (address == 0xFF25u) {
        for (std::uint8_t voice = 0u; voice < 4u; ++voice) {
            recordVoiceEvent(voice, BMMQ::PsgEventKind::RoutingChange, address, value, true);
        }
        recordRawWriteEvent(0u, address, value);
        return;
    }

    std::uint8_t voice = 0u;
    if (address >= 0xFF16u && address <= 0xFF19u) voice = 1u;
    else if ((address >= 0xFF1Au && address <= 0xFF1Eu) ||
             (address >= 0xFF30u && address <= 0xFF3Fu)) voice = 2u;
    else if (address >= 0xFF20u && address <= 0xFF23u) voice = 3u;

    const bool trigger = (address == 0xFF14u || address == 0xFF19u ||
                          address == 0xFF1Eu || address == 0xFF23u) && (value & 0x80u) != 0u;
    BMMQ::PsgEventKind kind = trigger ? BMMQ::PsgEventKind::Retrigger : BMMQ::PsgEventKind::TimbreChange;
    if (address == 0xFF13u || address == 0xFF18u || address == 0xFF1Du ||
        address == 0xFF14u || address == 0xFF19u || address == 0xFF1Eu || address == 0xFF22u) {
        kind = trigger ? BMMQ::PsgEventKind::Retrigger : BMMQ::PsgEventKind::PitchChange;
    } else if (address == 0xFF12u || address == 0xFF17u || address == 0xFF1Cu || address == 0xFF21u) {
        kind = BMMQ::PsgEventKind::LevelChange;
    }
    const std::array<bool, 4u> gates{{apu_.pulse1.enabled, apu_.pulse2.enabled,
                                      apu_.wave.enabled, apu_.noise.enabled}};
    if (!trigger && gates[voice] != observedGateStates_[voice]) {
        kind = gates[voice] ? BMMQ::PsgEventKind::GateOn : BMMQ::PsgEventKind::GateOff;
    }
    observedGateStates_[voice] = gates[voice];
    recordVoiceEvent(voice, kind, address, value, true);
    recordRawWriteEvent(voice, address, value);
}

void GameBoyAPU::recordVoiceEvent(std::uint8_t voice, BMMQ::PsgEventKind kind,
                                  std::uint16_t rawAddress, std::uint8_t rawValue,
                                  bool hasRawWrite)
{
    const auto frequency = voice == 0u ? apu_.pulse1.frequency
        : voice == 1u ? apu_.pulse2.frequency : voice == 2u ? apu_.wave.frequency : 0u;
    const std::array<bool, 4u> gates{{apu_.pulse1.enabled, apu_.pulse2.enabled,
                                      apu_.wave.enabled, apu_.noise.enabled}};

    BMMQ::PsgAudioEvent event;
    event.sequence = ++eventSequence_;
    event.voiceId = voice;
    event.voiceKind = voice < 2u ? BMMQ::PsgVoiceKind::Pulse
        : voice == 2u ? BMMQ::PsgVoiceKind::Wave : BMMQ::PsgVoiceKind::Noise;
    event.kind = kind;
    if (voice < 3u) {
        const auto denominator = std::max<std::uint32_t>(2048u - (frequency & 0x07FFu), 1u);
        const auto numerator = voice == 2u ? 65'536'000ull : 131'072'000ull;
        event.frequencyMilliHz = static_cast<std::uint32_t>(numerator / denominator);
    }
    event.levelQ15 = effectiveVoiceLevelQ15(voice);
    event.routingMask = voiceRoutingMask(voice);
    event.timbre = voice == 0u ? apu_.pulse1.duty : voice == 1u ? apu_.pulse2.duty
        : voice == 2u ? apu_.wave.outputLevel
        : static_cast<std::uint8_t>((apu_.noise.clockShift << 4u) | apu_.noise.divisorCode);
    event.rawAddress = rawAddress;
    event.rawValue = rawValue;
    event.gate = gates[voice];
    event.hasRawWrite = hasRawWrite;
    if (pendingEvents_.size() == kHistorySamples) {
        pendingEvents_.pop_front();
        ++pendingEventDropCount_;
    }
    pendingEvents_.push_back({std::move(event), apu_.sampleCounter});
}

void GameBoyAPU::recordRawWriteEvent(std::uint8_t voice, std::uint16_t address,
                                     std::uint8_t value)
{
    recordVoiceEvent(voice, BMMQ::PsgEventKind::RawWrite, address, value, true);
}

std::uint8_t GameBoyAPU::voiceRoutingMask(std::uint8_t voice) const noexcept
{
    return static_cast<std::uint8_t>(((apu_.nr51 & (0x10u << voice)) != 0u ? 1u : 0u) |
                                     ((apu_.nr51 & (0x01u << voice)) != 0u ? 2u : 0u));
}

std::uint16_t GameBoyAPU::effectiveVoiceLevelQ15(std::uint8_t voice) const noexcept
{
    std::uint32_t channelLevelQ15 = 0u;
    switch (voice) {
    case 0u:
        channelLevelQ15 = (static_cast<std::uint32_t>(apu_.pulse1.volume) * 32767u) / 15u;
        break;
    case 1u:
        channelLevelQ15 = (static_cast<std::uint32_t>(apu_.pulse2.volume) * 32767u) / 15u;
        break;
    case 2u:
        channelLevelQ15 = apu_.wave.outputLevel == 1u ? 32767u
            : apu_.wave.outputLevel == 2u ? 16384u
            : apu_.wave.outputLevel == 3u ? 8192u : 0u;
        break;
    case 3u:
        channelLevelQ15 = (static_cast<std::uint32_t>(apu_.noise.volume) * 32767u) / 15u;
        break;
    default:
        return 0u;
    }

    const auto routing = voiceRoutingMask(voice);
    if (routing == 0u || channelLevelQ15 == 0u) return 0u;

    const auto leftGain = static_cast<std::uint32_t>(((apu_.nr50 >> 4u) & 0x07u) + 1u);
    const auto rightGain = static_cast<std::uint32_t>((apu_.nr50 & 0x07u) + 1u);
    std::uint32_t gainSum = 0u;
    std::uint32_t routedOutputs = 0u;
    if ((routing & 0x01u) != 0u) {
        gainSum += leftGain;
        ++routedOutputs;
    }
    if ((routing & 0x02u) != 0u) {
        gainSum += rightGain;
        ++routedOutputs;
    }
    const auto masterLevelQ15 = (gainSum * 32767u + 4u * routedOutputs) /
        (8u * routedOutputs);
    return static_cast<std::uint16_t>(
        (channelLevelQ15 * masterLevelQ15 + 16383u) / 32767u);
}

// Save state export/import.
GameBoyAPUState GameBoyAPU::exportState() const {
    GameBoyAPUState state;
    state.masterEnabled = apu_.masterEnabled;
    state.frameSequencerCounter = apu_.frameSequencerCounter;
    state.frameSequencerStep = apu_.frameSequencerStep;
    state.sampleAccumulator = apu_.sampleAccumulator;
    state.sampleCounter = apu_.sampleCounter;
    state.frameCounter = apu_.frameCounter;
    state.recentSamples = apu_.recentSamples;
    state.recentWriteCursor = apu_.recentWriteCursor;
    state.recentSampleCount = apu_.recentSampleCount;
    state.pendingReadCursor = apu_.pendingReadCursor;
    state.pendingSampleCount = apu_.pendingSampleCount;
    state.waveRam = apu_.waveRam;
    state.nr50 = apu_.nr50;
    state.nr51 = apu_.nr51;
    state.nr52 = apu_.nr52;
    state.pulse1 = apu_.pulse1;
    state.pulse2 = apu_.pulse2;
    state.wave = apu_.wave;
    state.noise = apu_.noise;
    return state;
}

void GameBoyAPU::importState(const GameBoyAPUState& state) {
    if (state.frameSequencerStep > 7u ||
        state.recentWriteCursor >= kHistorySamples ||
        state.recentSampleCount > kHistorySamples ||
        state.pendingReadCursor >= kHistorySamples ||
        state.pendingSampleCount > kHistorySamples ||
        state.pulse1.sweepShift > 7u ||
        state.noise.clockShift > 15u) {
        throw std::invalid_argument("APU state contains invalid fields");
    }
    apu_.masterEnabled = state.masterEnabled;
    apu_.frameSequencerCounter = state.frameSequencerCounter;
    apu_.frameSequencerStep = state.frameSequencerStep;
    apu_.sampleAccumulator = state.sampleAccumulator;
    apu_.sampleCounter = state.sampleCounter;
    apu_.frameCounter = state.frameCounter;
    apu_.recentSamples = state.recentSamples;
    apu_.recentWriteCursor = state.recentWriteCursor;
    apu_.recentSampleCount = state.recentSampleCount;
    apu_.pendingReadCursor = state.pendingReadCursor;
    apu_.pendingSampleCount = state.pendingSampleCount;
    apu_.waveRam = state.waveRam;
    apu_.nr50 = state.nr50;
    apu_.nr51 = state.nr51;
    apu_.nr52 = state.nr52;
    apu_.pulse1 = state.pulse1;
    apu_.pulse2 = state.pulse2;
    apu_.wave = state.wave;
    apu_.noise = state.noise;
    recentVoiceStems_ = {};
    pendingEvents_.clear();
    pendingEventDropCount_ = 0u;
    observedGateStates_ = {{apu_.pulse1.enabled, apu_.pulse2.enabled,
                            apu_.wave.enabled, apu_.noise.enabled}};
    for (std::uint8_t voice = 0u; voice < 4u; ++voice) {
        recordVoiceEvent(voice, BMMQ::PsgEventKind::StateSnapshot);
    }
}

} // namespace GB
