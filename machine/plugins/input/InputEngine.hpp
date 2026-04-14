#ifndef BMMQ_INPUT_ENGINE_HPP
#define BMMQ_INPUT_ENGINE_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../../InputTypes.hpp"

namespace BMMQ {

struct InputAnalogState {
    static constexpr std::size_t ChannelCount = 4;

    std::array<int16_t, ChannelCount> channels{};

    [[nodiscard]] bool anyActive() const noexcept
    {
        return std::any_of(channels.begin(), channels.end(), [](int16_t value) {
            return value != 0;
        });
    }

    [[nodiscard]] bool operator==(const InputAnalogState&) const noexcept = default;
};

struct InputEngineConfig {
    std::size_t stagingCapacity = 8;
};

struct InputEngineStats {
    std::size_t eventOverflowCount = 0;
    std::size_t digitalOverflowCount = 0;
    std::size_t analogOverflowCount = 0;
    std::size_t staleGenerationDropCount = 0;
    std::size_t pollFailureCount = 0;
    std::size_t neutralFallbackCount = 0;
    uint64_t lastCommittedGeneration = 0;
};

class InputEngine {
public:
    explicit InputEngine(const InputEngineConfig& config = {})
        : config_(sanitizeConfig(config)),
          digitalQueue_(config_.stagingCapacity),
          analogQueue_(config_.stagingCapacity)
    {
        reset();
    }

    void configure(const InputEngineConfig& config)
    {
        config_ = sanitizeConfig(config);
        digitalQueue_.assign(config_.stagingCapacity, DigitalSnapshot{});
        analogQueue_.assign(config_.stagingCapacity, AnalogSnapshot{});
        reset();
    }

    [[nodiscard]] const InputEngineConfig& config() const noexcept
    {
        return config_;
    }

    void reset() noexcept
    {
        digitalWriteIndex_ = 0u;
        digitalCount_ = 0u;
        analogWriteIndex_ = 0u;
        analogCount_ = 0u;
        committedDigitalMask_ = 0u;
        committedAnalogState_ = {};
        committedDigitalValid_ = false;
        committedAnalogValid_ = false;
        currentGeneration_ = 0u;
        stats_ = {};
    }

    [[nodiscard]] bool stageDigitalSnapshot(InputButtonMask mask, uint64_t generation) noexcept
    {
        if (generation < currentGeneration_) {
            ++stats_.staleGenerationDropCount;
            return false;
        }

        if (digitalCount_ == digitalQueue_.size()) {
            digitalQueue_[lastWrittenIndex(digitalWriteIndex_, digitalQueue_.size())] = DigitalSnapshot{generation, mask};
            ++stats_.eventOverflowCount;
            ++stats_.digitalOverflowCount;
            return true;
        }

        digitalQueue_[digitalWriteIndex_] = DigitalSnapshot{generation, mask};
        digitalWriteIndex_ = nextIndex(digitalWriteIndex_, digitalQueue_.size());
        ++digitalCount_;
        return true;
    }

    [[nodiscard]] bool stageAnalogSnapshot(const InputAnalogState& state, uint64_t generation) noexcept
    {
        if (generation < currentGeneration_) {
            ++stats_.staleGenerationDropCount;
            return false;
        }

        if (analogCount_ == analogQueue_.size()) {
            analogQueue_[lastWrittenIndex(analogWriteIndex_, analogQueue_.size())] = AnalogSnapshot{generation, state};
            ++stats_.eventOverflowCount;
            ++stats_.analogOverflowCount;
            return true;
        }

        analogQueue_[analogWriteIndex_] = AnalogSnapshot{generation, state};
        analogWriteIndex_ = nextIndex(analogWriteIndex_, analogQueue_.size());
        ++analogCount_;
        return true;
    }

    [[nodiscard]] bool commitDigitalSnapshot() noexcept
    {
        if (digitalCount_ == 0u) {
            return false;
        }

        const auto snapshot = digitalQueue_[lastWrittenIndex(digitalWriteIndex_, digitalQueue_.size())];
        committedDigitalMask_ = snapshot.mask;
        committedDigitalValid_ = true;
        currentGeneration_ = std::max(currentGeneration_, snapshot.generation);
        stats_.lastCommittedGeneration = currentGeneration_;
        clearDigitalQueue();
        return true;
    }

    [[nodiscard]] bool commitAnalogSnapshot() noexcept
    {
        if (analogCount_ == 0u) {
            return false;
        }

        const auto snapshot = analogQueue_[lastWrittenIndex(analogWriteIndex_, analogQueue_.size())];
        committedAnalogState_ = snapshot.state;
        committedAnalogValid_ = true;
        currentGeneration_ = std::max(currentGeneration_, snapshot.generation);
        stats_.lastCommittedGeneration = currentGeneration_;
        clearAnalogQueue();
        return true;
    }

    [[nodiscard]] bool commitSnapshots() noexcept
    {
        const bool committedDigital = commitDigitalSnapshot();
        const bool committedAnalog = commitAnalogSnapshot();
        return committedDigital || committedAnalog;
    }

    void advanceGeneration(uint64_t generation) noexcept
    {
        currentGeneration_ = std::max(currentGeneration_, generation);
        stats_.staleGenerationDropCount += digitalCount_ + analogCount_;
        clearDigitalQueue();
        clearAnalogQueue();
    }

    void notePollFailure() noexcept
    {
        ++stats_.pollFailureCount;
    }

    void applyNeutralFallback(uint64_t generation) noexcept
    {
        advanceGeneration(generation);
        committedDigitalMask_ = 0u;
        committedAnalogState_ = {};
        committedDigitalValid_ = true;
        committedAnalogValid_ = true;
        ++stats_.neutralFallbackCount;
        stats_.lastCommittedGeneration = currentGeneration_;
    }

    [[nodiscard]] std::optional<InputButtonMask> committedDigitalMask() const noexcept
    {
        if (!committedDigitalValid_) {
            return std::nullopt;
        }
        return committedDigitalMask_;
    }

    [[nodiscard]] std::optional<InputAnalogState> committedAnalogState() const noexcept
    {
        if (!committedAnalogValid_) {
            return std::nullopt;
        }
        return committedAnalogState_;
    }

    [[nodiscard]] uint64_t currentGeneration() const noexcept
    {
        return currentGeneration_;
    }

    [[nodiscard]] const InputEngineStats& stats() const noexcept
    {
        return stats_;
    }

private:
    struct DigitalSnapshot {
        uint64_t generation = 0u;
        InputButtonMask mask = 0u;
    };

    struct AnalogSnapshot {
        uint64_t generation = 0u;
        InputAnalogState state{};
    };

    [[nodiscard]] static InputEngineConfig sanitizeConfig(const InputEngineConfig& config) noexcept
    {
        InputEngineConfig sanitized = config;
        sanitized.stagingCapacity = std::max<std::size_t>(sanitized.stagingCapacity, 1u);
        return sanitized;
    }

    [[nodiscard]] static std::size_t nextIndex(std::size_t index, std::size_t size) noexcept
    {
        return (index + 1u) % size;
    }

    [[nodiscard]] static std::size_t lastWrittenIndex(std::size_t writeIndex, std::size_t size) noexcept
    {
        return writeIndex == 0u ? (size - 1u) : (writeIndex - 1u);
    }

    void clearDigitalQueue() noexcept
    {
        digitalCount_ = 0u;
    }

    void clearAnalogQueue() noexcept
    {
        analogCount_ = 0u;
    }

    InputEngineConfig config_{};
    std::vector<DigitalSnapshot> digitalQueue_{};
    std::vector<AnalogSnapshot> analogQueue_{};
    std::size_t digitalWriteIndex_ = 0u;
    std::size_t digitalCount_ = 0u;
    std::size_t analogWriteIndex_ = 0u;
    std::size_t analogCount_ = 0u;
    InputButtonMask committedDigitalMask_ = 0u;
    InputAnalogState committedAnalogState_{};
    bool committedDigitalValid_ = false;
    bool committedAnalogValid_ = false;
    uint64_t currentGeneration_ = 0u;
    InputEngineStats stats_{};
};

} // namespace BMMQ

#endif // BMMQ_INPUT_ENGINE_HPP