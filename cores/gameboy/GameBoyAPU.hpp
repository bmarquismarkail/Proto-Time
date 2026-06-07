#pragma once
// Game Boy APU (Audio Processing Unit) abstraction.
// References: Pan Docs (NR10-NR52 registers)
//
// Responsibilities:
//   - Pulse channels 1 & 2 (duty cycle, length counter, envelope, sweep)
//   - Wave channel (PCM wave RAM, sample output)
//   - Noise channel (LFSR, clock shift, width mode)
//   - Frame sequencer (4-step cycle at 512Hz / 8192Hz)
//   - Sample generation and history buffer
//   - Audio frame counter for event emission

#include <array>
#include <cstdint>
#include <vector>

namespace GB {

class GameBoyAPU {
public:
    GameBoyAPU();
    ~GameBoyAPU() = default;

    void reset();

    // Advance APU by CPU cycles (4194304 Hz master clock)
    void step(uint32_t cpuCycles);

    // Register access (called from LR3592_DMG or memory map intercept)
    void writeRegister(uint16_t address, uint8_t value);
    [[nodiscard]] uint8_t readRegister(uint16_t address) const;

    // Output queries
    [[nodiscard]] std::vector<int16_t> copyRecentSamples() const;
    [[nodiscard]] uint64_t frameCounter() const noexcept { return frameCounter_; }
    [[nodiscard]] uint32_t sampleRate() const noexcept { return kSampleRate; }
    [[nodiscard]] uint8_t outputChannelCount() const noexcept { return 2u; }

    // Status
    [[nodiscard]] bool masterEnabled() const noexcept { return apu_.masterEnabled; }

private:
    static constexpr uint32_t kMasterClockHz = 4194304u;
    static constexpr uint32_t kSampleRate = 48000u;
    static constexpr std::size_t kHistorySamples = 4096u;
    static constexpr uint32_t kCyclesPerFrameStep = 8192u; // 512Hz frame sequencer

    struct PulseChannel {
        bool enabled = false;
        bool dacEnabled = false;
        bool lengthEnabled = false;
        uint8_t duty = 0;       // Duty cycle pattern (0-3)
        uint8_t dutyStep = 0;
        uint16_t lengthCounter = 0;
        uint8_t initialVolume = 0;
        uint8_t volume = 0;
        bool envelopeIncrease = false;
        uint8_t envelopePeriod = 0;
        uint8_t envelopeTimer = 0;
        uint16_t frequency = 0;
        uint16_t timer = 0;
        bool hasSweep = false;
        uint8_t sweepPeriod = 0;
        uint8_t sweepTimer = 0;
        bool sweepNegate = false;
        uint8_t sweepShift = 0;
        uint16_t shadowFrequency = 0;
        bool sweepEnabled = false;
    };

    struct WaveChannel {
        bool enabled = false;
        bool dacEnabled = false;
        bool lengthEnabled = false;
        uint16_t lengthCounter = 0;
        uint16_t frequency = 0;
        uint16_t timer = 0;
        uint8_t sampleIndex = 0;
        uint8_t sampleVolume = 0; // 0=off, 1=1/4, 2=1/2, 3=full
    };

    struct NoiseChannel {
        bool enabled = false;
        bool dacEnabled = false;
        bool lengthEnabled = false;
        uint16_t lengthCounter = 0;
        uint8_t initialVolume = 0;
        uint8_t volume = 0;
        bool envelopeIncrease = false;
        uint8_t envelopePeriod = 0;
        uint8_t envelopeTimer = 0;
        uint8_t clockShift = 0;
        uint8_t divisorCode = 0;
        bool widthMode7 = false;
        uint16_t timer = 0;
        uint16_t lfsr = 0x7FFFu;
    };

    struct ApuState {
        bool masterEnabled = true;
        uint32_t frameSequencerCounter = 0;
        uint8_t frameSequencerStep = 0;
        uint64_t sampleCounter = 0;
        uint64_t frameCounter = 0;
        std::array<int16_t, kHistorySamples> recentSamples{};
        std::size_t recentWriteCursor = 0;
        std::size_t recentSampleCount = 0;
        PulseChannel pulse1{};
        PulseChannel pulse2{};
        WaveChannel wave{};
        NoiseChannel noise{};
    };

    // Generate one sample from all channels
    [[nodiscard]] int16_t generateSample();

    // Tick length counters
    void tickLengthCounters();

    // Tick sweep logic
    void tickSweep(PulseChannel& channel);

    // Tick envelope
    void tickEnvelope(PulseChannel& channel);
    void tickEnvelope(NoiseChannel& channel);

    // Frame sequencer step
    void stepFrameSequencer();

    // Push generated sample to history buffer
    void pushSample(int16_t sample);

    ApuState apu_{};
    uint32_t frameStepCounter_ = 0;
    uint64_t frameCounter_ = 0;
    uint32_t lastSampleAccumulator_ = 0;
};

} // namespace GB
