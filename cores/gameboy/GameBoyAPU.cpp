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
    apu_ = ApuState{};
}

void GameBoyAPU::reset() {
    apu_ = ApuState{};
    apu_.masterEnabled = true;
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
            pushSample(generateSample());
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
        tickEnvelope(apu_.pulse1);
        tickEnvelope(apu_.pulse2);
        tickEnvelope(apu_.noise);
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

void GameBoyAPU::tickEnvelope(PulseChannel& channel) {
    if (channel.envelopePeriod == 0) return;
    channel.envelopeTimer--;
    if (channel.envelopeTimer > 0) return;
    channel.envelopeTimer = channel.envelopePeriod;

    if (channel.envelopeIncrease) {
        if (channel.volume < 15) {
            channel.volume++;
        }
    } else {
        if (channel.volume > 0) {
            channel.volume--;
        }
    }
}

void GameBoyAPU::tickEnvelope(NoiseChannel& channel) {
    if (channel.envelopePeriod == 0) return;
    channel.envelopeTimer--;
    if (channel.envelopeTimer > 0) return;
    channel.envelopeTimer = channel.envelopePeriod;

    if (channel.envelopeIncrease) {
        if (channel.volume < 15) {
            channel.volume++;
        }
    } else {
        if (channel.volume > 0) {
            channel.volume--;
        }
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

int16_t GameBoyAPU::mixCurrentSample() const noexcept
{
    const int ch1 = currentPulseSample(apu_.pulse1);
    const int ch2 = currentPulseSample(apu_.pulse2);
    const int ch3 = currentWaveSample();
    const int ch4 = currentNoiseSample();

    const int leftVolume = static_cast<int>((apu_.nr50 >> 4u) & 0x07u) + 1;
    const int rightVolume = static_cast<int>(apu_.nr50 & 0x07u) + 1;

    int left = 0;
    int right = 0;
    if ((apu_.nr51 & 0x10u) != 0u) left += ch1;
    if ((apu_.nr51 & 0x20u) != 0u) left += ch2;
    if ((apu_.nr51 & 0x40u) != 0u) left += ch3;
    if ((apu_.nr51 & 0x80u) != 0u) left += ch4;
    if ((apu_.nr51 & 0x01u) != 0u) right += ch1;
    if ((apu_.nr51 & 0x02u) != 0u) right += ch2;
    if ((apu_.nr51 & 0x04u) != 0u) right += ch3;
    if ((apu_.nr51 & 0x08u) != 0u) right += ch4;

    left *= leftVolume;
    right *= rightVolume;
    return static_cast<int16_t>(std::clamp((left + right) * 32, -32768, 32767));
}

int16_t GameBoyAPU::generateSample() {
    return mixCurrentSample();
}

void GameBoyAPU::pushSample(int16_t sample) {
    std::size_t cursor = apu_.recentWriteCursor;
    apu_.recentSamples[cursor] = sample;
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
        apu_.pulse1.sweepPeriod = (value >> 4) & 0x07u;
        apu_.pulse1.sweepNegate = (value & 0x08u) != 0;
        apu_.pulse1.sweepShift = value & 0x07u;
        apu_.pulse1.sweepEnabled = apu_.pulse1.sweepPeriod != 0 || apu_.pulse1.sweepShift != 0;
        break;
    case 0xFF11: // NR11
        apu_.pulse1.duty = (value >> 6u) & 0x03u;
        apu_.pulse1.lengthCounter = 64 - (value & 0x3Fu);
        break;
    case 0xFF12: // NR12
        apu_.pulse1.dacEnabled = (value & 0xF8u) != 0;
        apu_.pulse1.envelopeIncrease = (value & 0x08u) != 0;
        apu_.pulse1.envelopePeriod = value & 0x07u;
        apu_.pulse1.initialVolume = (value >> 4) & 0x0Fu;
        apu_.pulse1.volume = apu_.pulse1.initialVolume;
        apu_.pulse1.envelopeTimer = apu_.pulse1.envelopePeriod;
        break;
    case 0xFF13: // NR13
        apu_.pulse1.frequency = (apu_.pulse1.frequency & 0x0700u) | value;
        break;
    case 0xFF14: // NR14
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
        apu_.pulse2.duty = (value >> 6u) & 0x03u;
        apu_.pulse2.lengthCounter = 64 - (value & 0x3Fu);
        break;
    case 0xFF17: // NR22
        apu_.pulse2.dacEnabled = (value & 0xF8u) != 0;
        apu_.pulse2.envelopeIncrease = (value & 0x08u) != 0;
        apu_.pulse2.envelopePeriod = value & 0x07u;
        apu_.pulse2.initialVolume = (value >> 4) & 0x0Fu;
        apu_.pulse2.volume = apu_.pulse2.initialVolume;
        apu_.pulse2.envelopeTimer = apu_.pulse2.envelopePeriod;
        break;
    case 0xFF18: // NR23
        apu_.pulse2.frequency = (apu_.pulse2.frequency & 0x0700u) | value;
        break;
    case 0xFF19: // NR24
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
        apu_.wave.dacEnabled = (value & 0x80u) != 0;
        if (!apu_.wave.dacEnabled) apu_.wave.enabled = false;
        break;
    case 0xFF1B: // NR31
        apu_.wave.lengthCounter = 256 - value;
        break;
    case 0xFF1C: // NR32
        apu_.wave.outputLevel = static_cast<uint8_t>((value >> 5u) & 0x03u);
        break;
    case 0xFF1D: // NR33
        apu_.wave.frequency = (apu_.wave.frequency & 0x0700u) | value;
        break;
    case 0xFF1E: // NR34
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
        apu_.noise.lengthCounter = 64 - (value & 0x3Fu);
        break;
    case 0xFF21: // NR42
        apu_.noise.dacEnabled = (value & 0xF8u) != 0;
        apu_.noise.envelopeIncrease = (value & 0x08u) != 0;
        apu_.noise.envelopePeriod = value & 0x07u;
        apu_.noise.initialVolume = (value >> 4) & 0x0Fu;
        apu_.noise.volume = apu_.noise.initialVolume;
        apu_.noise.envelopeTimer = apu_.noise.envelopePeriod;
        break;
    case 0xFF22: // NR43
        apu_.noise.clockShift = (value >> 4);
        apu_.noise.divisorCode = value & 0x07u;
        apu_.noise.widthMode7 = (value & 0x08u) != 0;
        break;
    case 0xFF23: // NR44
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
        apu_.nr50 = value;
        break;
    case 0xFF25: // NR51 - PWR NR
        apu_.nr51 = value;
        break;
    case 0xFF26: // NR52 - APU power control
        apu_.masterEnabled = (value & 0x80u) != 0;
        apu_.nr52 = value;
        if (!apu_.masterEnabled) {
            apu_.pulse1.enabled = false;
            apu_.pulse2.enabled = false;
            apu_.wave.enabled = false;
            apu_.noise.enabled = false;
        }
        break;
    default:
        if (address >= 0xFF30u && address <= 0xFF3Fu) {
            apu_.waveRam[address - 0xFF30u] = value;
        }
        break;
    }
}

uint8_t GameBoyAPU::readRegister(uint16_t address) const {
    switch (address) {
    case 0xFF24:
        return apu_.nr50;
    case 0xFF25:
        return apu_.nr51;
    case 0xFF26: // NR52
        return static_cast<uint8_t>((apu_.masterEnabled ? 0x80u : 0u) |
               (apu_.pulse1.enabled ? 0x01u : 0) |
               (apu_.pulse2.enabled ? 0x02u : 0) |
               (apu_.wave.enabled ? 0x04u : 0) |
               (apu_.noise.enabled ? 0x08u : 0));
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
        state.pendingSampleCount > kHistorySamples) {
        throw std::invalid_argument("APU state contains invalid buffer cursors");
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
}

} // namespace GB
