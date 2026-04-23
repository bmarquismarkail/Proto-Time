#include "GameGearPSG.hpp"

#include <algorithm>

GameGearPSG::GameGearPSG() {}
GameGearPSG::~GameGearPSG() {}

void GameGearPSG::reset() {
    for (auto& tone : tones_) {
        tone = ToneChannel{};
    }
    compatRegisters_.fill(0u);
    waveRam_.fill(0u);
    currentFrameSamples_.clear();
    recentSamples_.clear();
    samplePhase_ = 0u;
    frameCounter_ = 0u;
    latchedChannel_ = 0u;
    latchedVolume_ = false;
    compatRegisters_[0x16u] = 0x70u;
}

void GameGearPSG::step(uint32_t cpuCycles) {
    samplePhase_ += static_cast<uint64_t>(cpuCycles) * static_cast<uint64_t>(kSampleRate);
    while (samplePhase_ >= kClockHz) {
        samplePhase_ -= kClockHz;
        produceSample();
    }
}

void GameGearPSG::writeData(uint8_t value) {
    if ((value & 0x80u) != 0u) {
        latchedChannel_ = static_cast<uint8_t>((value >> 5u) & 0x03u);
        latchedVolume_ = (value & 0x10u) != 0u;
        if (latchedChannel_ < 3u) {
            if (latchedVolume_) {
                tones_[latchedChannel_].attenuation = static_cast<uint8_t>(value & 0x0Fu);
                tones_[latchedChannel_].enabled = tones_[latchedChannel_].attenuation < 0x0Fu;
            } else {
                tones_[latchedChannel_].period = static_cast<uint16_t>((tones_[latchedChannel_].period & 0x03F0u) |
                                                                       static_cast<uint16_t>(value & 0x0Fu));
                tones_[latchedChannel_].enabled = true;
            }
        }
    } else if (latchedChannel_ < 3u && !latchedVolume_) {
        tones_[latchedChannel_].period = static_cast<uint16_t>((tones_[latchedChannel_].period & 0x000Fu) |
                                                               (static_cast<uint16_t>(value & 0x3Fu) << 4u));
        tones_[latchedChannel_].enabled = true;
    }
    updateCompatStatus();
}

uint8_t GameGearPSG::readCompatRegister(uint16_t addr) const noexcept {
    if (addr < 0xFF10u || addr > 0xFF26u) {
        return 0xFFu;
    }
    return compatRegisters_[static_cast<std::size_t>(addr - 0xFF10u)];
}

void GameGearPSG::writeCompatRegister(uint16_t addr, uint8_t value) {
    if (addr < 0xFF10u || addr > 0xFF26u) {
        return;
    }

    compatRegisters_[static_cast<std::size_t>(addr - 0xFF10u)] = value;
    switch (addr) {
        case 0xFF12u:
            applyCompatVolume(value);
            break;
        case 0xFF13u:
            applyCompatToneLow(value);
            break;
        case 0xFF14u:
            applyCompatToneHigh(value);
            break;
        case 0xFF26u:
            if ((value & 0x80u) == 0u) {
                for (auto& tone : tones_) {
                    tone.enabled = false;
                }
                currentFrameSamples_.clear();
                recentSamples_.clear();
            }
            break;
        default:
            break;
    }
    updateCompatStatus();
}

uint8_t GameGearPSG::readWaveRam(uint16_t addr) const noexcept {
    if (addr < 0xFF30u || addr > 0xFF3Fu) {
        return 0xFFu;
    }
    return waveRam_[static_cast<std::size_t>(addr - 0xFF30u)];
}

void GameGearPSG::writeWaveRam(uint16_t addr, uint8_t value) noexcept {
    if (addr < 0xFF30u || addr > 0xFF3Fu) {
        return;
    }
    waveRam_[static_cast<std::size_t>(addr - 0xFF30u)] = value;
}

std::vector<int16_t> GameGearPSG::copyRecentSamples() const {
    return recentSamples_;
}

uint32_t GameGearPSG::sampleRate() const noexcept {
    return kSampleRate;
}

uint64_t GameGearPSG::frameCounter() const noexcept {
    return frameCounter_;
}

void GameGearPSG::produceSample() {
    currentFrameSamples_.push_back(mixSample());
    if (currentFrameSamples_.size() >= kSamplesPerFrame) {
        recentSamples_ = currentFrameSamples_;
        currentFrameSamples_.clear();
        ++frameCounter_;
    }
}

void GameGearPSG::applyCompatToneLow(uint8_t value) {
    tones_[0].period = static_cast<uint16_t>((tones_[0].period & 0x0700u) | value);
}

void GameGearPSG::applyCompatToneHigh(uint8_t value) {
    tones_[0].period = static_cast<uint16_t>((tones_[0].period & 0x00FFu) |
                                             ((static_cast<uint16_t>(value & 0x07u)) << 8u));
    if ((value & 0x80u) != 0u && (compatRegisters_[0x16u] & 0x80u) != 0u) {
        tones_[0].enabled = true;
        tones_[0].counter = 0u;
        tones_[0].outputHigh = true;
    }
}

void GameGearPSG::applyCompatVolume(uint8_t value) {
    const uint8_t envelope = static_cast<uint8_t>((value >> 4u) & 0x0Fu);
    tones_[0].attenuation = static_cast<uint8_t>(0x0Fu - envelope);
    tones_[0].enabled = envelope != 0u && (compatRegisters_[0x16u] & 0x80u) != 0u;
}

void GameGearPSG::updateCompatStatus() noexcept {
    uint8_t status = static_cast<uint8_t>(compatRegisters_[0x16u] & 0x80u);
    for (std::size_t i = 0; i < tones_.size(); ++i) {
        if (tones_[i].enabled && tones_[i].attenuation < 0x0Fu) {
            status = static_cast<uint8_t>(status | static_cast<uint8_t>(1u << i));
        }
    }
    compatRegisters_[0x16u] = status;
}

int16_t GameGearPSG::mixSample() noexcept {
    if ((compatRegisters_[0x16u] & 0x80u) == 0u) {
        return 0;
    }

    int mixed = 0;
    for (auto& tone : tones_) {
        if (!tone.enabled || tone.attenuation >= 0x0Fu) {
            continue;
        }
        const uint32_t period = tone.period == 0u ? 1u : tone.period;
        const uint32_t togglePeriod = std::max<uint32_t>(1u, period * 16u);
        tone.counter += 1u;
        if (tone.counter >= togglePeriod) {
            tone.counter = 0u;
            tone.outputHigh = !tone.outputHigh;
        }
        const int amplitude = (15 - static_cast<int>(tone.attenuation)) * 256;
        mixed += tone.outputHigh ? amplitude : -amplitude;
    }
    mixed = std::clamp(mixed, -32767, 32767);
    return static_cast<int16_t>(mixed);
}
