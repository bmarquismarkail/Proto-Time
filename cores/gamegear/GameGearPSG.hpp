#pragma once
// Sega Game Gear Programmable Sound Generator (PSG) minimal baseline
// References: SMS Power, MAME, Emulicious

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class GameGearPSG {
public:
    GameGearPSG();
    ~GameGearPSG();

    void reset();
    void step(uint32_t cpuCycles);
    void writeData(uint8_t value);
    void writeStereoControl(uint8_t value) noexcept;
    [[nodiscard]] uint8_t readCompatRegister(uint16_t addr) const noexcept;
    void writeCompatRegister(uint16_t addr, uint8_t value);
    [[nodiscard]] uint8_t readWaveRam(uint16_t addr) const noexcept;
    void writeWaveRam(uint16_t addr, uint8_t value) noexcept;
    [[nodiscard]] std::vector<int16_t> copyRecentSamples() const;
    [[nodiscard]] uint32_t sampleRate() const noexcept;
    [[nodiscard]] uint8_t outputChannelCount() const noexcept;
    [[nodiscard]] uint64_t frameCounter() const noexcept;
    [[nodiscard]] uint8_t stereoControl() const noexcept;
    [[nodiscard]] uint16_t tonePeriod(std::size_t channel) const noexcept;
    [[nodiscard]] uint8_t channelAttenuation(std::size_t channel) const noexcept;
    [[nodiscard]] uint8_t noiseControl() const noexcept;

private:
    struct ToneChannel {
        uint16_t period = 0x0010u;
        uint8_t attenuation = 0x0Fu;
        double counter = 0.0;
        bool outputHigh = false;
        bool enabled = false;
    };

    static constexpr uint32_t kClockHz = 3579545u;
    static constexpr uint32_t kSampleRate = 48000u;
    static constexpr uint8_t kOutputChannelCount = 2u;
    static constexpr std::size_t kFramesPerChunk = 256u;

    void produceFrame();
    void advanceGenerators();
    void applyCompatToneLow(uint8_t value);
    void applyCompatToneHigh(uint8_t value);
    void applyCompatVolume(uint8_t value);
    void updateCompatStatus() noexcept;
    [[nodiscard]] std::array<int16_t, 2> mixFrame() noexcept;
    [[nodiscard]] int channelAmplitude(std::size_t channel) const noexcept;
    [[nodiscard]] bool channelRoutedLeft(std::size_t channel) const noexcept;
    [[nodiscard]] bool channelRoutedRight(std::size_t channel) const noexcept;

    std::array<ToneChannel, 3> tones_{};
    uint8_t noiseControl_ = 0u;
    uint8_t noiseAttenuation_ = 0x0Fu;
    double noiseCounter_ = 0.0;
    uint16_t noiseLfsr_ = 0x8000u;
    bool noiseOutputHigh_ = false;
    uint8_t stereoControl_ = 0xFFu;
    std::array<uint8_t, 0x17u> compatRegisters_{};
    std::array<uint8_t, 0x10u> waveRam_{};
    std::vector<int16_t> currentFrameSamples_;
    std::vector<int16_t> recentSamples_;
    uint64_t samplePhase_ = 0u;
    uint64_t frameCounter_ = 0u;
    uint8_t latchedChannel_ = 0u;
    bool latchedVolume_ = false;
};
