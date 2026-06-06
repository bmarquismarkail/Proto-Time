#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace BMMQ {

enum class AudioKpiStatus {
    Healthy,
    SourceLimited,
    TransportLimited,
    Unknown,
};

struct AudioKpiInputs {
    double elapsedSeconds = 0.0;
    std::uint64_t generatedSamples = 0;
    std::uint64_t appendedSamples = 0;
    double expectedSamplesPerSecond = 0.0;
    std::uint64_t drainedReadySamples = 0;
    std::uint64_t drainRequestedSamples = 0;
};

struct AudioKpiClassification {
    AudioKpiStatus status = AudioKpiStatus::Unknown;
    double sourceRatio = 0.0;
    double drainRatio = 0.0;
    bool hasSourceRatio = false;
    bool hasDrainRatio = false;
};

inline constexpr double kAudioKpiSourceRealtimeThreshold = 0.95;
inline constexpr double kAudioKpiDrainRealtimeThreshold = 0.95;
inline constexpr double kAudioKpiSourceDrainCloseTolerance = 0.05;

[[nodiscard]] inline std::string_view audioKpiStatusName(AudioKpiStatus status) noexcept
{
    switch (status) {
    case AudioKpiStatus::Healthy:
        return "healthy";
    case AudioKpiStatus::SourceLimited:
        return "source_limited";
    case AudioKpiStatus::TransportLimited:
        return "transport_limited";
    case AudioKpiStatus::Unknown:
        return "unknown";
    }
    return "unknown";
}

[[nodiscard]] inline AudioKpiClassification classifyAudioKpiStatus(const AudioKpiInputs& inputs) noexcept
{
    AudioKpiClassification result{};

    const auto sourceSamples =
        inputs.generatedSamples != 0u ? inputs.generatedSamples : inputs.appendedSamples;
    const auto expectedSamples = inputs.expectedSamplesPerSecond * inputs.elapsedSeconds;
    if (expectedSamples > 0.0) {
        result.sourceRatio = static_cast<double>(sourceSamples) / expectedSamples;
        result.hasSourceRatio = true;
    }
    if (inputs.drainRequestedSamples != 0u) {
        result.drainRatio =
            static_cast<double>(inputs.drainedReadySamples) / static_cast<double>(inputs.drainRequestedSamples);
        result.hasDrainRatio = true;
    }

    if (!result.hasSourceRatio || !result.hasDrainRatio) {
        result.status = AudioKpiStatus::Unknown;
        return result;
    }

    if (result.sourceRatio < kAudioKpiSourceRealtimeThreshold &&
        std::abs(result.sourceRatio - result.drainRatio) <= kAudioKpiSourceDrainCloseTolerance) {
        result.status = AudioKpiStatus::SourceLimited;
    } else if (result.sourceRatio >= kAudioKpiSourceRealtimeThreshold &&
               result.drainRatio < kAudioKpiDrainRealtimeThreshold) {
        result.status = AudioKpiStatus::TransportLimited;
    } else if (result.sourceRatio >= kAudioKpiSourceRealtimeThreshold &&
               result.drainRatio >= kAudioKpiDrainRealtimeThreshold) {
        result.status = AudioKpiStatus::Healthy;
    } else {
        result.status = AudioKpiStatus::Unknown;
    }
    return result;
}

} // namespace BMMQ
