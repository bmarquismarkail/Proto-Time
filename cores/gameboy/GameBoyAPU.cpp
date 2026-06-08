#include "GameBoyAPU.hpp"

#include <algorithm>

#include <array>

namespace GB {

// Duty cycle patterns (Pan Docs)
static constexpr std::array<std::array<uint8_t, 8>, 4> kDutyPatterns{{
    {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u},
    {1u, 0u, 0u, 0u, 0u, 0u, 0u, 1u},
    {1u, 0u, 0u, 0u, 0u, 1u, 1u, 1u},
    {0u, 1u, 1u, 1u, 1u, 1u, 1u, 0u},
}};

namespace {

template <typename Channel>
void tickEnvelopeImpl(Channel& channel)
{
    if (!channel.enabled || channel.envelopePeriod == 0u) {
        return;
    }
    if (channel.envelopeTimer > 0u) {
        --channel.envelopeTimer;
    }
    if (channel.envelopeTimer != 0u) {
        return;
    }

    channel.envelopeTimer = channel.envelopePeriod;
    if (channel.envelopeIncrease) {
        if (channel.volume < 15u) {
            ++channel.volume;
        }
    } else if (channel.volume > 0u) {
        --channel.volume;
    }
}

} // namespace

GameBoyAPU::GameBoyAPU() {}

void GameBoyAPU::reset()
{
    apu_ = ApuState{};
    apu_.masterEnabled = true;
    apu_.pulse1.hasSweep = true;
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

void GameBoyAPU::step(uint32_t cpuCycles)
{
    for (uint32_t cycle = 0; cycle < cpuCycles; ++cycle) {
        stepOneCycle();
    }
}

void GameBoyAPU::stepOneCycle()
{
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
            const auto xorBit = static_cast<uint16_t>((apu_.noise.lfsr ^ (apu_.noise.lfsr >> 1u)) & 0x01u);
            apu_.noise.lfsr = static_cast<uint16_t>((apu_.noise.lfsr >> 1u) | (xorBit << 14u));
            if (apu_.noise.widthMode7) {
                apu_.noise.lfsr = static_cast<uint16_t>((apu_.noise.lfsr & ~(1u << 6u)) | (xorBit << 6u));
            }
        }
    }

    apu_.sampleAccumulator += kSampleRate;
    while (apu_.sampleAccumulator >= kMasterClockHz) {
        apu_.sampleAccumulator -= kMasterClockHz;
        pushSample(mixCurrentSample());
    }
}

void GameBoyAPU::stepFrameSequencer()
{
    const auto step = apu_.frameSequencerStep;
    if ((step & 0x01u) == 0u) {
        tickLengthCounters();
    }
    if (step == 2u || step == 6u) {
        tickSweep();
    }
    if (step == 7u) {
        tickEnvelope(apu_.pulse1);
        tickEnvelope(apu_.pulse2);
        tickEnvelope(apu_.noise);
    }

    apu_.frameSequencerStep = static_cast<uint8_t>((apu_.frameSequencerStep + 1u) & 0x07u);
}

void GameBoyAPU::tickLengthCounters()
{
    const auto tickPulseLength = [](PulseChannel& channel) {
        if (channel.enabled && channel.lengthEnabled && channel.lengthCounter > 0u) {
            --channel.lengthCounter;
            if (channel.lengthCounter == 0u) {
                channel.enabled = false;
            }
        }
    };

    tickPulseLength(apu_.pulse1);
    tickPulseLength(apu_.pulse2);
    if (apu_.wave.enabled && apu_.wave.lengthEnabled && apu_.wave.lengthCounter > 0u) {
        --apu_.wave.lengthCounter;
        if (apu_.wave.lengthCounter == 0u) {
            apu_.wave.enabled = false;
        }
    }
    if (apu_.noise.enabled && apu_.noise.lengthEnabled && apu_.noise.lengthCounter > 0u) {
        --apu_.noise.lengthCounter;
        if (apu_.noise.lengthCounter == 0u) {
            apu_.noise.enabled = false;
        }
    }
}

void GameBoyAPU::tickSweep()
{
    auto& channel = apu_.pulse1;
    if (!channel.hasSweep || !channel.enabled || !channel.sweepEnabled) {
        return;
    }
    if (channel.sweepTimer > 0u) {
        --channel.sweepTimer;
    }
    if (channel.sweepTimer != 0u) {
        return;
    }

    channel.sweepTimer = channel.sweepPeriod == 0u ? 8u : channel.sweepPeriod;
    if (channel.sweepPeriod == 0u) {
        return;
    }

    const auto delta = static_cast<uint16_t>(channel.shadowFrequency >> channel.sweepShift);
    const int nextFrequency = channel.sweepNegate
        ? static_cast<int>(channel.shadowFrequency) - static_cast<int>(delta)
        : static_cast<int>(channel.shadowFrequency) + static_cast<int>(delta);
    if (nextFrequency < 0 || nextFrequency > 2047) {
        channel.enabled = false;
        return;
    }

    if (channel.sweepShift != 0u) {
        channel.shadowFrequency = static_cast<uint16_t>(nextFrequency);
        channel.frequency = channel.shadowFrequency;
        channel.timer = pulseTimerPeriod(channel.frequency);
        if (!channel.sweepNegate) {
            const auto overflowCheck = static_cast<uint16_t>(
                channel.shadowFrequency + (channel.shadowFrequency >> channel.sweepShift));
            if (overflowCheck > 2047u) {
                channel.enabled = false;
            }
        }
    }
}

void GameBoyAPU::tickEnvelope(PulseChannel& channel)
{
    tickEnvelopeImpl(channel);
}

void GameBoyAPU::tickEnvelope(NoiseChannel& channel)
{
    tickEnvelopeImpl(channel);
}

void GameBoyAPU::triggerPulse(PulseChannel& channel, bool withSweep)
{
    if (channel.lengthCounter == 0u) {
        channel.lengthCounter = 64u;
    }
    channel.volume = channel.initialVolume;
    channel.envelopeTimer = channel.envelopePeriod == 0u ? 8u : channel.envelopePeriod;
    channel.enabled = apu_.masterEnabled && channel.dacEnabled;
    channel.timer = pulseTimerPeriod(channel.frequency);
    channel.dutyStep = 0u;

    if (withSweep) {
        channel.shadowFrequency = channel.frequency;
        channel.sweepTimer = channel.sweepPeriod == 0u ? 8u : channel.sweepPeriod;
        channel.sweepEnabled = channel.sweepPeriod != 0u || channel.sweepShift != 0u;
        if (channel.sweepShift != 0u) {
            const auto delta = static_cast<uint16_t>(channel.shadowFrequency >> channel.sweepShift);
            const int previewFrequency = channel.sweepNegate
                ? static_cast<int>(channel.shadowFrequency) - static_cast<int>(delta)
                : static_cast<int>(channel.shadowFrequency) + static_cast<int>(delta);
            if (previewFrequency < 0 || previewFrequency > 2047) {
                channel.enabled = false;
            }
        }
    }
}

void GameBoyAPU::triggerWave()
{
    auto& channel = apu_.wave;
    if (channel.lengthCounter == 0u) {
        channel.lengthCounter = 256u;
    }
    channel.enabled = apu_.masterEnabled && channel.dacEnabled;
    channel.timer = waveTimerPeriod(channel.frequency);
    channel.sampleIndex = 0u;
}

void GameBoyAPU::triggerNoise()
{
    auto& channel = apu_.noise;
    if (channel.lengthCounter == 0u) {
        channel.lengthCounter = 64u;
    }

    channel.volume = channel.initialVolume;
    channel.envelopeTimer = channel.envelopePeriod == 0u ? 8u : channel.envelopePeriod;
    channel.enabled = apu_.masterEnabled && channel.dacEnabled;
    channel.lfsr = 0x7FFFu;
    channel.timer = noiseTimerPeriod();
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
        ? static_cast<uint8_t>((packed >> 4) & 0x0Fu)
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

    const int leftVolume = static_cast<int>((apu_.nr50 >> 4) & 0x07u) + 1;
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

void GameBoyAPU::pushSample(int16_t sample)
{
    const auto cursor = apu_.recentWriteCursor;
    apu_.recentSamples[cursor] = sample;
    apu_.recentWriteCursor = (cursor + 1u) % kHistorySamples;
    apu_.recentSampleCount = std::min<std::size_t>(apu_.recentSampleCount + 1u, kHistorySamples);
    ++apu_.sampleCounter;
    if ((apu_.sampleCounter % kFrameChunkSamples) == 0u) {
        ++apu_.frameCounter;
    }

    if (apu_.pendingSampleCount < kHistorySamples) {
        ++apu_.pendingSampleCount;
    } else {
        apu_.pendingReadCursor = (apu_.pendingReadCursor + 1u) % kHistorySamples;
    }
}

uint8_t GameBoyAPU::statusRegister() const noexcept
{
    return static_cast<uint8_t>((apu_.masterEnabled ? 0x80u : 0x00u) |
                                0x70u |
                                (apu_.pulse1.enabled ? 0x01u : 0x00u) |
                                (apu_.pulse2.enabled ? 0x02u : 0x00u) |
                                (apu_.wave.enabled ? 0x04u : 0x00u) |
                                (apu_.noise.enabled ? 0x08u : 0x00u));
}

void GameBoyAPU::writeRegister(uint16_t address, uint8_t value)
{
    if (address >= 0xFF30u && address <= 0xFF3Fu) {
        apu_.waveRam[address - 0xFF30u] = value;
        return;
    }

    switch (address) {
    case 0xFF10u:
        apu_.pulse1.hasSweep = true;
        apu_.pulse1.sweepPeriod = static_cast<uint8_t>((value >> 4) & 0x07u);
        apu_.pulse1.sweepNegate = (value & 0x08u) != 0u;
        apu_.pulse1.sweepShift = static_cast<uint8_t>(value & 0x07u);
        break;
    case 0xFF11u:
        apu_.pulse1.duty = static_cast<uint8_t>((value >> 6) & 0x03u);
        apu_.pulse1.lengthCounter = static_cast<uint8_t>(64u - (value & 0x3Fu));
        break;
    case 0xFF12u:
        apu_.pulse1.initialVolume = static_cast<uint8_t>((value >> 4) & 0x0Fu);
        apu_.pulse1.envelopeIncrease = (value & 0x08u) != 0u;
        apu_.pulse1.envelopePeriod = static_cast<uint8_t>(value & 0x07u);
        apu_.pulse1.dacEnabled = (value & 0xF8u) != 0u;
        if (!apu_.pulse1.dacEnabled) {
            apu_.pulse1.enabled = false;
        }
        break;
    case 0xFF13u:
        apu_.pulse1.frequency = static_cast<uint16_t>((apu_.pulse1.frequency & 0x0700u) | value);
        break;
    case 0xFF14u:
        apu_.pulse1.lengthEnabled = (value & 0x40u) != 0u;
        apu_.pulse1.frequency = static_cast<uint16_t>((apu_.pulse1.frequency & 0x00FFu) | ((value & 0x07u) << 8u));
        if ((value & 0x80u) != 0u) {
            triggerPulse(apu_.pulse1, true);
        }
        break;
    case 0xFF16u:
        apu_.pulse2.duty = static_cast<uint8_t>((value >> 6) & 0x03u);
        apu_.pulse2.lengthCounter = static_cast<uint8_t>(64u - (value & 0x3Fu));
        break;
    case 0xFF17u:
        apu_.pulse2.initialVolume = static_cast<uint8_t>((value >> 4) & 0x0Fu);
        apu_.pulse2.envelopeIncrease = (value & 0x08u) != 0u;
        apu_.pulse2.envelopePeriod = static_cast<uint8_t>(value & 0x07u);
        apu_.pulse2.dacEnabled = (value & 0xF8u) != 0u;
        if (!apu_.pulse2.dacEnabled) {
            apu_.pulse2.enabled = false;
        }
        break;
    case 0xFF18u:
        apu_.pulse2.frequency = static_cast<uint16_t>((apu_.pulse2.frequency & 0x0700u) | value);
        break;
    case 0xFF19u:
        apu_.pulse2.lengthEnabled = (value & 0x40u) != 0u;
        apu_.pulse2.frequency = static_cast<uint16_t>((apu_.pulse2.frequency & 0x00FFu) | ((value & 0x07u) << 8u));
        if ((value & 0x80u) != 0u) {
            triggerPulse(apu_.pulse2, false);
        }
        break;
    case 0xFF1Au:
        apu_.wave.dacEnabled = (value & 0x80u) != 0u;
        if (!apu_.wave.dacEnabled) {
            apu_.wave.enabled = false;
        }
        break;
    case 0xFF1Bu:
        apu_.wave.lengthCounter = static_cast<uint16_t>(256u - value);
        break;
    case 0xFF1Cu:
        apu_.wave.outputLevel = static_cast<uint8_t>((value >> 5) & 0x03u);
        break;
    case 0xFF1Du:
        apu_.wave.frequency = static_cast<uint16_t>((apu_.wave.frequency & 0x0700u) | value);
        break;
    case 0xFF1Eu:
        apu_.wave.lengthEnabled = (value & 0x40u) != 0u;
        apu_.wave.frequency = static_cast<uint16_t>((apu_.wave.frequency & 0x00FFu) | ((value & 0x07u) << 8u));
        if ((value & 0x80u) != 0u) {
            triggerWave();
        }
        break;
    case 0xFF20u:
        apu_.noise.lengthCounter = static_cast<uint8_t>(64u - (value & 0x3Fu));
        break;
    case 0xFF21u:
        apu_.noise.initialVolume = static_cast<uint8_t>((value >> 4) & 0x0Fu);
        apu_.noise.envelopeIncrease = (value & 0x08u) != 0u;
        apu_.noise.envelopePeriod = static_cast<uint8_t>(value & 0x07u);
        apu_.noise.dacEnabled = (value & 0xF8u) != 0u;
        if (!apu_.noise.dacEnabled) {
            apu_.noise.enabled = false;
        }
        break;
    case 0xFF22u:
        apu_.noise.clockShift = static_cast<uint8_t>((value >> 4) & 0x0Fu);
        apu_.noise.divisorCode = static_cast<uint8_t>(value & 0x07u);
        apu_.noise.widthMode7 = (value & 0x08u) != 0u;
        break;
    case 0xFF23u:
        apu_.noise.lengthEnabled = (value & 0x40u) != 0u;
        if ((value & 0x80u) != 0u) {
            triggerNoise();
        }
        break;
    case 0xFF24u:
        apu_.nr50 = value;
        break;
    case 0xFF25u:
        apu_.nr51 = value;
        break;
    case 0xFF26u:
        if ((value & 0x80u) == 0u) {
            const auto preservedWaveRam = apu_.waveRam;
            apu_ = ApuState{};
            apu_.waveRam = preservedWaveRam;
            apu_.pulse1.hasSweep = true;
            apu_.masterEnabled = false;
            return;
        }
        if (!apu_.masterEnabled) {
            apu_.masterEnabled = true;
            apu_.frameSequencerCounter = 0u;
            apu_.frameSequencerStep = 0u;
        }
        break;
    default:
        break;
    }
}

uint8_t GameBoyAPU::readRegister(uint16_t address) const
{
    if (address >= 0xFF30u && address <= 0xFF3Fu) {
        return apu_.waveRam[address - 0xFF30u];
    }

    switch (address) {
    case 0xFF24u:
        return apu_.nr50;
    case 0xFF25u:
        return apu_.nr51;
    case 0xFF26u:
        return statusRegister();
    default:
        return 0xFFu;
    }
}

std::vector<int16_t> GameBoyAPU::copyRecentSamples() const
{
    std::vector<int16_t> samples;
    if (apu_.recentSampleCount == 0u) {
        return samples;
    }

    const auto start = (apu_.recentWriteCursor - apu_.recentSampleCount + kHistorySamples) % kHistorySamples;
    samples.reserve(apu_.recentSampleCount);
    for (std::size_t i = 0; i < apu_.recentSampleCount; ++i) {
        samples.push_back(apu_.recentSamples[(start + i) % kHistorySamples]);
    }
    return samples;
}

std::vector<int16_t> GameBoyAPU::takePendingSamples() const
{
    std::vector<int16_t> samples;
    if (apu_.pendingSampleCount == 0u) {
        return samples;
    }

    samples.reserve(apu_.pendingSampleCount);
    for (std::size_t i = 0; i < apu_.pendingSampleCount; ++i) {
        samples.push_back(apu_.recentSamples[(apu_.pendingReadCursor + i) % kHistorySamples]);
    }
    apu_.pendingReadCursor = apu_.recentWriteCursor;
    apu_.pendingSampleCount = 0u;
    return samples;
}

} // namespace GB
