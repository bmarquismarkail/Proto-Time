#include "GameGearPSG.hpp"

#include <algorithm>
#include <array>

namespace {
constexpr std::array<int, 16> kAttenuationTable{{
    8192, 6507, 5168, 4105, 3261, 2590, 2057, 1634,
    1298, 1031, 819, 650, 516, 410, 326, 0,
}};

[[nodiscard]] int16_t clampSample(int value) noexcept
{
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}
} // namespace

GameGearPSG::GameGearPSG() {}
GameGearPSG::~GameGearPSG() {}

void GameGearPSG::reset() {
    for (auto& tone : tones_) {
        tone = ToneChannel{};
    }
    noiseControl_ = 0u;
    noiseAttenuation_ = 0x0Fu;
    noiseCounter_ = 0.0;
    noiseLfsr_ = 0x8000u;
    noiseOutputHigh_ = false;
    stereoControl_ = 0xFFu;
    compatRegisters_.fill(0u);
    waveRam_.fill(0u);
    currentFrameSamples_.clear();
    recentSamples_.clear();
    chunkSamplesLast_ = 0u;
    chunkSamplesMin_ = 0u;
    chunkSamplesMax_ = 0u;
    samplesGeneratedTotal_ = 0u;
    samplePhase_ = 0u;
    frameCounter_ = 0u;
    latchedChannel_ = 0u;
    latchedVolume_ = false;
    compatRegisters_[0x16u] = 0x80u;
}

void GameGearPSG::step(uint32_t cpuCycles) {
    samplePhase_ += static_cast<uint64_t>(cpuCycles) * static_cast<uint64_t>(kSampleRate);
    while (samplePhase_ >= kClockHz) {
        samplePhase_ -= kClockHz;
        produceFrame();
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
        } else if (latchedVolume_) {
            noiseAttenuation_ = static_cast<uint8_t>(value & 0x0Fu);
        } else {
            noiseControl_ = static_cast<uint8_t>(value & 0x07u);
            noiseLfsr_ = 0x8000u;
            noiseCounter_ = 0.0;
            noiseOutputHigh_ = false;
        }
    } else if (latchedChannel_ < 3u && !latchedVolume_) {
        tones_[latchedChannel_].period = static_cast<uint16_t>((tones_[latchedChannel_].period & 0x000Fu) |
                                                               (static_cast<uint16_t>(value & 0x3Fu) << 4u));
        tones_[latchedChannel_].enabled = true;
    }
    updateCompatStatus();
}

void GameGearPSG::writeStereoControl(uint8_t value) noexcept {
    stereoControl_ = value;
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

uint8_t GameGearPSG::outputChannelCount() const noexcept {
    return kOutputChannelCount;
}

uint64_t GameGearPSG::frameCounter() const noexcept {
    return frameCounter_;
}

uint8_t GameGearPSG::stereoControl() const noexcept {
    return stereoControl_;
}

uint16_t GameGearPSG::tonePeriod(std::size_t channel) const noexcept {
    return channel < tones_.size() ? tones_[channel].period : 0u;
}

uint8_t GameGearPSG::channelAttenuation(std::size_t channel) const noexcept {
    if (channel < tones_.size()) {
        return tones_[channel].attenuation;
    }
    return channel == 3u ? noiseAttenuation_ : 0x0Fu;
}

uint8_t GameGearPSG::noiseControl() const noexcept {
    return noiseControl_;
}

std::size_t GameGearPSG::chunkSamplesLast() const noexcept {
    return chunkSamplesLast_;
}

std::size_t GameGearPSG::chunkSamplesMin() const noexcept {
    return chunkSamplesMin_;
}

std::size_t GameGearPSG::chunkSamplesMax() const noexcept {
    return chunkSamplesMax_;
}

std::uint64_t GameGearPSG::chunksEmitted() const noexcept {
    return frameCounter_;
}

std::uint64_t GameGearPSG::samplesGeneratedTotal() const noexcept {
    return samplesGeneratedTotal_;
}

std::size_t GameGearPSG::pendingSamples() const noexcept {
    return currentFrameSamples_.size();
}

void GameGearPSG::produceFrame() {
    advanceGenerators();
    const auto frame = mixFrame();
    currentFrameSamples_.push_back(frame[0]);
    currentFrameSamples_.push_back(frame[1]);
    samplesGeneratedTotal_ += 2u;
    if (currentFrameSamples_.size() >= kFramesPerChunk * kOutputChannelCount) {
        const auto chunkSamples = currentFrameSamples_.size();
        recentSamples_ = currentFrameSamples_;
        chunkSamplesLast_ = chunkSamples;
        if (chunkSamplesMin_ == 0u || chunkSamples < chunkSamplesMin_) {
            chunkSamplesMin_ = chunkSamples;
        }
        chunkSamplesMax_ = std::max(chunkSamplesMax_, chunkSamples);
        currentFrameSamples_.clear();
        ++frameCounter_;
    }
}

void GameGearPSG::advanceGenerators() {
    constexpr double kCyclesPerSample = static_cast<double>(kClockHz) / static_cast<double>(kSampleRate);
    for (auto& tone : tones_) {
        if (!tone.enabled || tone.attenuation >= 0x0Fu) {
            continue;
        }
        const auto period = static_cast<double>(std::max<uint16_t>(tone.period, 1u));
        const auto togglePeriod = std::max(1.0, period * 16.0);
        tone.counter += kCyclesPerSample;
        while (tone.counter >= togglePeriod) {
            tone.counter -= togglePeriod;
            tone.outputHigh = !tone.outputHigh;
        }
    }

    if (noiseAttenuation_ >= 0x0Fu) {
        return;
    }
    const uint8_t rateSelect = static_cast<uint8_t>(noiseControl_ & 0x03u);
    const uint16_t noisePeriod = rateSelect == 0u ? 0x10u
        : rateSelect == 1u ? 0x20u
        : rateSelect == 2u ? 0x40u
        : std::max<uint16_t>(tones_[2].period, 1u);
    noiseCounter_ += kCyclesPerSample;
    const auto togglePeriod = static_cast<double>(noisePeriod) * 16.0;
    while (noiseCounter_ >= togglePeriod) {
        noiseCounter_ -= togglePeriod;
        const bool whiteNoise = (noiseControl_ & 0x04u) != 0u;
        const uint16_t feedback = whiteNoise
            ? static_cast<uint16_t>((noiseLfsr_ ^ (noiseLfsr_ >> 3u)) & 0x0001u)
            : static_cast<uint16_t>(noiseLfsr_ & 0x0001u);
        noiseLfsr_ = static_cast<uint16_t>((noiseLfsr_ >> 1u) | static_cast<uint16_t>(feedback << 15u));
        noiseOutputHigh_ = (noiseLfsr_ & 0x0001u) != 0u;
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
    if (noiseAttenuation_ < 0x0Fu) {
        status = static_cast<uint8_t>(status | 0x08u);
    }
    compatRegisters_[0x16u] = status;
}

std::array<int16_t, 2> GameGearPSG::mixFrame() noexcept {
    int left = 0;
    int right = 0;
    for (std::size_t channel = 0; channel < tones_.size(); ++channel) {
        if (!tones_[channel].enabled || tones_[channel].attenuation >= 0x0Fu) {
            continue;
        }
        const int amplitude = channelAmplitude(channel);
        const int signedAmplitude = tones_[channel].outputHigh ? amplitude : -amplitude;
        if (channelRoutedLeft(channel)) {
            left += signedAmplitude;
        }
        if (channelRoutedRight(channel)) {
            right += signedAmplitude;
        }
    }

    if (noiseAttenuation_ < 0x0Fu) {
        const int amplitude = channelAmplitude(3u);
        const int signedAmplitude = noiseOutputHigh_ ? amplitude : -amplitude;
        if (channelRoutedLeft(3u)) {
            left += signedAmplitude;
        }
        if (channelRoutedRight(3u)) {
            right += signedAmplitude;
        }
    }

    return {clampSample(left), clampSample(right)};
}

int GameGearPSG::channelAmplitude(std::size_t channel) const noexcept {
    const uint8_t attenuation = channel < tones_.size() ? tones_[channel].attenuation : noiseAttenuation_;
    return kAttenuationTable[std::min<uint8_t>(attenuation, 0x0Fu)];
}

bool GameGearPSG::channelRoutedLeft(std::size_t channel) const noexcept {
    if (channel >= 4u) {
        return false;
    }
    return (stereoControl_ & static_cast<uint8_t>(0x10u << channel)) != 0u;
}

bool GameGearPSG::channelRoutedRight(std::size_t channel) const noexcept {
    if (channel >= 4u) {
        return false;
    }
    return (stereoControl_ & static_cast<uint8_t>(0x01u << channel)) != 0u;
}
