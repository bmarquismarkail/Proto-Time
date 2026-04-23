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
    [[nodiscard]] uint8_t readCompatRegister(uint16_t addr) const noexcept;
    void writeCompatRegister(uint16_t addr, uint8_t value);
    [[nodiscard]] uint8_t readWaveRam(uint16_t addr) const noexcept;
    void writeWaveRam(uint16_t addr, uint8_t value) noexcept;
    [[nodiscard]] std::vector<int16_t> copyRecentSamples() const;
    [[nodiscard]] uint32_t sampleRate() const noexcept;
    [[nodiscard]] uint64_t frameCounter() const noexcept;

private:
    struct ToneChannel {
        uint16_t period = 0x0010u;
        uint8_t attenuation = 0x0Fu;
        uint32_t counter = 0u;
        bool outputHigh = false;
        bool enabled = false;
    };

    static constexpr uint32_t kClockHz = 3579545u;
    static constexpr uint32_t kSampleRate = 48000u;
    static constexpr std::size_t kSamplesPerFrame = 256u;

    void produceSample();
    void applyCompatToneLow(uint8_t value);
    void applyCompatToneHigh(uint8_t value);
    void applyCompatVolume(uint8_t value);
    void updateCompatStatus() noexcept;
    [[nodiscard]] int16_t mixSample() noexcept;

    std::array<ToneChannel, 3> tones_{};
    std::array<uint8_t, 0x17u> compatRegisters_{};
    std::array<uint8_t, 0x10u> waveRam_{};
    std::vector<int16_t> currentFrameSamples_;
    std::vector<int16_t> recentSamples_;
    uint64_t samplePhase_ = 0u;
    uint64_t frameCounter_ = 0u;
    uint8_t latchedChannel_ = 0u;
    bool latchedVolume_ = false;
};
