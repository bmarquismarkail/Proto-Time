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

// Nested channel structs must be declared before GameBoyAPUState so that
// the state struct can reference them.

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

    bool operator==(const PulseChannel& other) const = default;
};

struct WaveChannel {
    bool enabled = false;
    bool dacEnabled = false;
    bool lengthEnabled = false;
    uint16_t lengthCounter = 0;
    uint16_t frequency = 0;
    uint16_t timer = 0;
    uint8_t sampleIndex = 0;
    uint8_t outputLevel = 0;

    bool operator==(const WaveChannel& other) const = default;
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

    bool operator==(const NoiseChannel& other) const = default;
};

// GameBoyAPUState is a standalone struct (not nested inside the class)
// so the .cpp can reference it freely.
struct GameBoyAPUState {
    bool masterEnabled = true;
    uint32_t frameSequencerCounter = 0;
    uint8_t frameSequencerStep = 0;
    uint32_t sampleAccumulator = 0;
    uint64_t sampleCounter = 0;
    uint64_t frameCounter = 0;
    std::array<int16_t, 4096> recentSamples{};
    std::size_t recentWriteCursor = 0;
    std::size_t recentSampleCount = 0;
    mutable std::size_t pendingReadCursor = 0;
    mutable std::size_t pendingSampleCount = 0;
    std::array<uint8_t, 0x10> waveRam{};
    uint8_t nr50 = 0;
    uint8_t nr51 = 0;
    uint8_t nr52 = 0;
    PulseChannel pulse1{};
    PulseChannel pulse2{};
    WaveChannel wave{};
    NoiseChannel noise{};

    bool operator==(const GameBoyAPUState& other) const = default;
};

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
    [[nodiscard]] std::vector<int16_t> takePendingSamples() const;
    [[nodiscard]] uint64_t frameCounter() const noexcept { return apu_.frameCounter; }
    [[nodiscard]] uint32_t sampleRate() const noexcept { return kSampleRate; }
    [[nodiscard]] uint8_t outputChannelCount() const noexcept { return 1u; }

    // Status
    [[nodiscard]] bool masterEnabled() const noexcept { return apu_.masterEnabled; }

    // Save state export/import.
    [[nodiscard]] GameBoyAPUState exportState() const;
    void importState(const GameBoyAPUState& state);

private:
    static constexpr uint32_t kMasterClockHz = 4194304u;
    static constexpr uint32_t kSampleRate = 48000u;
    static constexpr std::size_t kHistorySamples = 4096u;
    static constexpr std::size_t kFrameChunkSamples = 256u;
    static constexpr uint32_t kCyclesPerFrameStep = 8192u; // 512Hz frame sequencer

    // GameBoyAPUState is the single source of truth. The apu_ member
    // stores it directly to avoid drift between a private mirror and the
    // state exported via exportState()/importState().

    [[nodiscard]] uint16_t pulseTimerPeriod(uint16_t frequency) const noexcept;
    [[nodiscard]] uint16_t waveTimerPeriod(uint16_t frequency) const noexcept;
    [[nodiscard]] uint16_t noiseTimerPeriod() const noexcept;

    void tickLengthCounters();
    void tickSweep();
    void tickEnvelope(PulseChannel& channel);
    void tickEnvelope(NoiseChannel& channel);
    void stepFrameSequencer();
    [[nodiscard]] int16_t generateSample();
    void pushSample(int16_t sample);
    [[nodiscard]] int currentPulseSample(const PulseChannel& channel) const noexcept;
    [[nodiscard]] int currentWaveSample() const noexcept;
    [[nodiscard]] int currentNoiseSample() const noexcept;
    [[nodiscard]] int16_t mixCurrentSample() const noexcept;

    GameBoyAPUState apu_{};
};

} // namespace GB
