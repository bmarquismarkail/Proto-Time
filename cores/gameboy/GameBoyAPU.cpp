#include "GameBoyAPU.hpp"
#include <algorithm>
#include <cmath>

namespace GB {

// Duty cycle patterns (Pan Docs)
static constexpr std::array<std::array<uint8_t, 8>, 4> kDutyPatterns{{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // 12.5%
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF}, // 25%
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF}, // 50%
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // 100%
}};

GameBoyAPU::GameBoyAPU() {}

void GameBoyAPU::reset() {
    apu_ = ApuState{};
    apu_.masterEnabled = true;
    frameStepCounter_ = 0;
    frameCounter_ = 0;
    lastSampleAccumulator_ = 0;
}

void GameBoyAPU::step(uint32_t cpuCycles) {
    if (!apu_.masterEnabled) return;

    // Frame sequencer runs at 512Hz (8192 master clock cycles per step)
    frameStepCounter_ += cpuCycles;
    while (frameStepCounter_ >= kCyclesPerFrameStep) {
        frameStepCounter_ -= kCyclesPerFrameStep;
        stepFrameSequencer();
    }

    // Sample generation: accumulate samples at 48kHz
    // Master clock / sampleRate = ~87.58 cycles per sample
    uint32_t cyclesPerSample = kMasterClockHz / kSampleRate;
    lastSampleAccumulator_ += cpuCycles;
    while (lastSampleAccumulator_ >= cyclesPerSample) {
        lastSampleAccumulator_ -= cyclesPerSample;
        int16_t sample = generateSample();
        pushSample(sample);
    }
}

void GameBoyAPU::stepFrameSequencer() {
    apu_.frameSequencerStep++;
    if (apu_.frameSequencerStep > 4) {
        apu_.frameSequencerStep = 1;
        frameCounter_++;
    }

    switch (apu_.frameSequencerStep) {
    case 1: // Every 8192 cycles
        tickLengthCounters();
        tickEnvelope(apu_.pulse1);
        tickEnvelope(apu_.pulse2);
        tickEnvelope(apu_.noise);
        break;
    case 2: // Every 16384 cycles
        tickLengthCounters();
        tickSweep(apu_.pulse1);
        break;
    case 3: // Every 24576 cycles
        tickLengthCounters();
        tickEnvelope(apu_.pulse1);
        tickEnvelope(apu_.pulse2);
        tickEnvelope(apu_.noise);
        break;
    case 4: // Every 32768 cycles
        tickLengthCounters();
        tickSweep(apu_.pulse1);
        break;
    default:
        break;
    }
}

void GameBoyAPU::tickLengthCounters() {
    // Pulse 1
    if (apu_.pulse1.lengthEnabled && apu_.pulse1.lengthCounter > 0) {
        apu_.pulse1.lengthCounter--;
    }
    // Pulse 2
    if (apu_.pulse2.lengthEnabled && apu_.pulse2.lengthCounter > 0) {
        apu_.pulse2.lengthCounter--;
    }
    // Wave
    if (apu_.wave.lengthEnabled && apu_.wave.lengthCounter > 0) {
        apu_.wave.lengthCounter--;
    }
    // Noise
    if (apu_.noise.lengthEnabled && apu_.noise.lengthCounter > 0) {
        apu_.noise.lengthCounter--;
    }
}

void GameBoyAPU::tickSweep(PulseChannel& channel) {
    if (!channel.sweepEnabled || channel.sweepPeriod == 0 || channel.sweepShift == 0) {
        return;
    }

    channel.sweepTimer--;
    if (channel.sweepTimer > 0) return;
    channel.sweepTimer = channel.sweepPeriod;

    const uint16_t delta = static_cast<uint16_t>(channel.shadowFrequency >> channel.sweepShift);
    const uint16_t newFreq = channel.sweepNegate
        ? static_cast<uint16_t>(channel.shadowFrequency - delta)
        : static_cast<uint16_t>(channel.shadowFrequency + delta);
    if (newFreq > 0x07FFu) {
        channel.enabled = false; // Overflow
        return;
    }

    channel.shadowFrequency = newFreq;
    channel.frequency = newFreq;
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

int16_t GameBoyAPU::generateSample() {
    int16_t sample = 0;

    // Pulse 1
    if (apu_.pulse1.dacEnabled && apu_.pulse1.lengthCounter > 0) {
        uint8_t dutyIdx = apu_.pulse1.dutyStep & 0x07u;
        uint8_t dutyVal = kDutyPatterns[apu_.pulse1.duty][dutyIdx];
        const auto amplitude = static_cast<int16_t>(apu_.pulse1.volume) * 128;
        sample += dutyVal != 0 ? amplitude : static_cast<int16_t>(-amplitude);
        apu_.pulse1.dutyStep++;
    }

    // Pulse 2
    if (apu_.pulse2.dacEnabled && apu_.pulse2.lengthCounter > 0) {
        uint8_t dutyIdx = apu_.pulse2.dutyStep & 0x07u;
        uint8_t dutyVal = kDutyPatterns[apu_.pulse2.duty][dutyIdx];
        const auto amplitude = static_cast<int16_t>(apu_.pulse2.volume) * 128;
        sample += dutyVal != 0 ? amplitude : static_cast<int16_t>(-amplitude);
        apu_.pulse2.dutyStep++;
    }

    // Wave
    if (apu_.wave.dacEnabled && apu_.wave.lengthCounter > 0) {
        if (apu_.wave.sampleVolume > 0) {
            sample += static_cast<int16_t>(apu_.wave.sampleVolume) * 64;
        }
    }

    // Noise
    if (apu_.noise.dacEnabled && apu_.noise.lengthCounter > 0) {
        const auto amplitude = static_cast<int16_t>(apu_.noise.volume) * 128;
        sample += (apu_.noise.lfsr & 1) ? amplitude : static_cast<int16_t>(-amplitude);
    }

    // Normalize to -32768..32767 range
    sample = std::max(-32768, std::min(32767, static_cast<int>(sample)));
    return sample;
}

void GameBoyAPU::pushSample(int16_t sample) {
    std::size_t cursor = apu_.recentWriteCursor;
    apu_.recentSamples[cursor] = sample;
    apu_.recentWriteCursor = (cursor + 1) % kHistorySamples;
    if (apu_.recentSampleCount < kHistorySamples) {
        apu_.recentSampleCount++;
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
        break;
    case 0xFF11: // NR11
        apu_.pulse1.duty = value & 0x03u;
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
        }
        apu_.pulse1.hasSweep = true;
        apu_.pulse1.shadowFrequency = apu_.pulse1.frequency;
        apu_.pulse1.sweepTimer = apu_.pulse1.sweepPeriod;
        break;
    case 0xFF16: // NR21
        apu_.pulse2.duty = value & 0x03u;
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
        }
        break;
    case 0xFF1A: // NR30
        apu_.wave.dacEnabled = (value & 0x80u) != 0;
        apu_.wave.enabled = true;
        break;
    case 0xFF1B: // NR31
        apu_.wave.lengthCounter = 256 - value;
        break;
    case 0xFF1C: // NR32
        apu_.wave.sampleVolume = (value & 0x06u) >> 1; // 0=off, 1=1/4, 2=1/2, 3=full
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
    case 0xFF24: // NR50 - volume / mixing
        // Bit 7: Wave DAC power (master)
        // Bit 6: NR1 power
        // Bit 5: NR2 power
        // Bit 4: NR3 power
        // Bit 3: NR4 power
        // Bits 0-2: Left speaker volume
        // Bits 4-6: Right speaker volume
        break;
    case 0xFF25: // NR51 - PWR NR
        // Bit 0-3: NR1-NR4 right output enable
        // Bit 4-7: NR1-NR4 left output enable
        break;
    case 0xFF26: // NR52 - APU power control
        apu_.masterEnabled = (value & 0x80u) != 0;
        break;
    default:
        break;
    }
}

uint8_t GameBoyAPU::readRegister(uint16_t address) const {
    switch (address) {
    case 0xFF26: // NR52
        return 0x80u | // APU on
               (apu_.pulse1.dacEnabled ? 0x01u : 0) |
               (apu_.pulse2.dacEnabled ? 0x02u : 0) |
               (apu_.wave.dacEnabled ? 0x04u : 0) |
               (apu_.noise.dacEnabled ? 0x08u : 0);
    default:
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
