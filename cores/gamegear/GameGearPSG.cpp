#include "GameGearPSG.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
constexpr std::array<int, 16> kAttenuationTable{{
    8192, 6507, 5168, 4105, 3261, 2590, 2057, 1634,
    1298, 1031, 819, 650, 516, 410, 326, 0,
}};
constexpr double kMaxImportedCounter = 1.0e9;

[[nodiscard]] int16_t clampSample(int value) noexcept
{
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

void validateImportedCounter(double value)
{
    if (!std::isfinite(value) || value < 0.0 || value > kMaxImportedCounter) {
        throw std::invalid_argument("Game Gear PSG state counter invalid");
    }
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

std::vector<uint8_t> GameGearPSG::exportState() const {
    std::vector<uint8_t> state;
    const auto appendU8 = [&state](uint8_t value) { state.push_back(value); };
    const auto appendBool = [&appendU8](bool value) { appendU8(value ? 1u : 0u); };
    const auto appendU16 = [&appendU8](uint16_t value) {
        appendU8(static_cast<uint8_t>(value & 0xFFu));
        appendU8(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    };
    const auto appendU32 = [&appendU8](uint32_t value) {
        appendU8(static_cast<uint8_t>(value & 0xFFu));
        appendU8(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        appendU8(static_cast<uint8_t>((value >> 16u) & 0xFFu));
        appendU8(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    };
    const auto appendU64 = [&appendU32](uint64_t value) {
        appendU32(static_cast<uint32_t>(value & 0xFFFFFFFFull));
        appendU32(static_cast<uint32_t>((value >> 32u) & 0xFFFFFFFFull));
    };
    const auto appendDouble = [&appendU64](double value) {
        static_assert(sizeof(double) == sizeof(uint64_t));
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendU64(bits);
    };
    const auto appendBytes = [&appendU32, &state](const auto& bytes) {
        appendU32(static_cast<uint32_t>(bytes.size()));
        state.insert(state.end(), bytes.begin(), bytes.end());
    };
    const auto appendSamples = [&appendU32, &appendU16](const std::vector<int16_t>& samples) {
        appendU32(static_cast<uint32_t>(samples.size()));
        for (const auto sample : samples) {
            appendU16(static_cast<uint16_t>(sample));
        }
    };

    for (const auto& tone : tones_) {
        appendU16(tone.period);
        appendU8(tone.attenuation);
        appendDouble(tone.counter);
        appendBool(tone.outputHigh);
        appendBool(tone.enabled);
    }
    appendU8(noiseControl_);
    appendU8(noiseAttenuation_);
    appendDouble(noiseCounter_);
    appendU16(noiseLfsr_);
    appendBool(noiseOutputHigh_);
    appendU8(stereoControl_);
    appendBytes(compatRegisters_);
    appendBytes(waveRam_);
    appendSamples(currentFrameSamples_);
    appendSamples(recentSamples_);
    appendU64(chunkSamplesLast_);
    appendU64(chunkSamplesMin_);
    appendU64(chunkSamplesMax_);
    appendU64(frameCounter_);
    appendU64(samplesGeneratedTotal_);
    appendU64(samplePhase_);
    appendU64(frameCounter_);
    appendU8(latchedChannel_);
    appendBool(latchedVolume_);
    return state;
}

void GameGearPSG::importState(const std::vector<uint8_t>& state) {
    std::size_t pos = 0;
    const auto require = [&state, &pos](std::size_t count) {
        if (pos > state.size() || count > state.size() - pos) {
            throw std::invalid_argument("Game Gear PSG state truncated");
        }
    };
    const auto readU8 = [&state, &pos, &require]() {
        require(1u);
        return state[pos++];
    };
    const auto readBool = [&readU8]() {
        const auto value = readU8();
        if (value > 1u) {
            throw std::invalid_argument("Game Gear PSG state boolean invalid");
        }
        return value != 0u;
    };
    const auto readU16 = [&readU8]() {
        const auto lo = static_cast<uint16_t>(readU8());
        const auto hi = static_cast<uint16_t>(readU8());
        return static_cast<uint16_t>(lo | (hi << 8u));
    };
    const auto readU32 = [&readU8]() {
        const auto b0 = static_cast<uint32_t>(readU8());
        const auto b1 = static_cast<uint32_t>(readU8());
        const auto b2 = static_cast<uint32_t>(readU8());
        const auto b3 = static_cast<uint32_t>(readU8());
        return b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u);
    };
    const auto readU64 = [&readU32]() {
        const auto lo = static_cast<uint64_t>(readU32());
        const auto hi = static_cast<uint64_t>(readU32());
        return lo | (hi << 32u);
    };
    const auto readDouble = [&readU64]() {
        const auto bits = readU64();
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };
    const auto readBytes = [&state, &pos, &require, &readU32](auto& out) {
        const auto count = static_cast<std::size_t>(readU32());
        if (count != out.size()) {
            throw std::invalid_argument("Game Gear PSG state array size mismatch");
        }
        require(count);
        std::copy_n(state.begin() + static_cast<std::ptrdiff_t>(pos), out.size(), out.begin());
        pos += out.size();
    };
    const auto readSamples = [&readU32, &readU16](std::size_t maxSamples) {
        const auto count = static_cast<std::size_t>(readU32());
        if (count > maxSamples) {
            throw std::invalid_argument("Game Gear PSG sample history too large");
        }
        std::vector<int16_t> samples;
        samples.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            samples.push_back(static_cast<int16_t>(readU16()));
        }
        return samples;
    };

    decltype(tones_) nextTones{};
    for (auto& tone : nextTones) {
        tone.period = readU16();
        tone.attenuation = readU8();
        tone.counter = readDouble();
        validateImportedCounter(tone.counter);
        tone.outputHigh = readBool();
        tone.enabled = readBool();
    }
    const auto nextNoiseControl = readU8();
    const auto nextNoiseAttenuation = readU8();
    const auto nextNoiseCounter = readDouble();
    validateImportedCounter(nextNoiseCounter);
    const auto nextNoiseLfsr = readU16();
    const auto nextNoiseOutputHigh = readBool();
    const auto nextStereoControl = readU8();
    decltype(compatRegisters_) nextCompat{};
    decltype(waveRam_) nextWaveRam{};
    readBytes(nextCompat);
    readBytes(nextWaveRam);
    auto nextCurrent = readSamples(kFramesPerChunk * kOutputChannelCount);
    auto nextRecent = readSamples(8192u);
    const auto nextChunkLast = static_cast<std::size_t>(readU64());
    const auto nextChunkMin = static_cast<std::size_t>(readU64());
    const auto nextChunkMax = static_cast<std::size_t>(readU64());
    const auto nextChunksEmitted = readU64();
    const auto nextSamplesTotal = readU64();
    const auto nextSamplePhase = readU64();
    const auto nextFrameCounter = readU64();
    const auto nextLatchedChannel = readU8();
    const auto nextLatchedVolume = readBool();
    if (nextLatchedChannel > 3u || pos != state.size()) {
        throw std::invalid_argument("Game Gear PSG state invalid");
    }

    tones_ = nextTones;
    noiseControl_ = nextNoiseControl;
    noiseAttenuation_ = nextNoiseAttenuation;
    noiseCounter_ = nextNoiseCounter;
    noiseLfsr_ = nextNoiseLfsr;
    noiseOutputHigh_ = nextNoiseOutputHigh;
    stereoControl_ = nextStereoControl;
    compatRegisters_ = nextCompat;
    waveRam_ = nextWaveRam;
    currentFrameSamples_ = std::move(nextCurrent);
    recentSamples_ = std::move(nextRecent);
    chunkSamplesLast_ = nextChunkLast;
    chunkSamplesMin_ = nextChunkMin;
    chunkSamplesMax_ = nextChunkMax;
    (void)nextChunksEmitted;
    samplesGeneratedTotal_ = nextSamplesTotal;
    samplePhase_ = nextSamplePhase;
    frameCounter_ = nextFrameCounter;
    latchedChannel_ = nextLatchedChannel;
    latchedVolume_ = nextLatchedVolume;
}
