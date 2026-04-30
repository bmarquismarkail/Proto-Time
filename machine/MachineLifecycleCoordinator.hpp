#ifndef BMMQ_MACHINE_LIFECYCLE_COORDINATOR_HPP
#define BMMQ_MACHINE_LIFECYCLE_COORDINATOR_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace BMMQ {

class AudioService;
class VideoService;

enum class MachineTransitionReason : uint8_t {
    RomLoad = 0,
    HardReset,
    AudioBackendRestart,
    VideoBackendRestart,
    ConfigReconfigure,
};

struct MachineLifecycleCoordinatorStats {
    std::size_t transitionCount = 0;
    std::size_t successCount = 0;
    std::size_t degradedCount = 0;
    std::size_t failureCount = 0;
    std::array<std::size_t, 5> reasonCounts{};
    std::uint64_t transitionDurationP50Ns = 0;
    std::uint64_t transitionDurationP95Ns = 0;
    std::uint64_t transitionDurationMaxNs = 0;
};

enum class MachineTransitionOutcome : uint8_t {
    Succeeded = 0,
    Degraded,
    Failed,
};

enum class MachineTransitionFailureStage : uint8_t {
    None = 0,
    Pause,
    Mutation,
    EpochBump,
    Resume,
};

struct MachineTransitionPolicy {
    std::size_t maxRetries = 0;
    bool allowHeadlessVideoFallback = false;
    bool allowAudioBackendDisable = false;
    bool failHard = true;
};

struct MachineTransitionMutationResult {
    bool success = true;
    bool videoReady = true;
    bool audioReady = true;
};

struct MachineTransitionResult {
    MachineTransitionReason reason = MachineTransitionReason::ConfigReconfigure;
    MachineTransitionOutcome outcome = MachineTransitionOutcome::Succeeded;
    MachineTransitionFailureStage failureStage = MachineTransitionFailureStage::None;
    std::size_t retryCountUsed = 0;
    bool degradedHeadlessVideo = false;
    bool degradedAudioDisabled = false;
};

class MachineLifecycleCoordinator {
public:
    using TransitionMutation = std::function<MachineTransitionMutationResult()>;

    void bindServices(AudioService* audio, VideoService* video) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        audioService_ = audio;
        videoService_ = video;
    }

    [[nodiscard]] bool runTransition(MachineTransitionReason reason, const TransitionMutation& mutation);
    [[nodiscard]] bool transitionAudioBackendRestart(const TransitionMutation& mutation)
    {
        return runTransition(MachineTransitionReason::AudioBackendRestart, mutation);
    }

    [[nodiscard]] bool transitionVideoBackendRestart(const TransitionMutation& mutation)
    {
        return runTransition(MachineTransitionReason::VideoBackendRestart, mutation);
    }

    [[nodiscard]] bool transitionConfigReconfigure(const TransitionMutation& mutation)
    {
        return runTransition(MachineTransitionReason::ConfigReconfigure, mutation);
    }

    [[nodiscard]] MachineLifecycleCoordinatorStats stats() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    [[nodiscard]] MachineTransitionResult lastTransitionResult() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastResult_;
    }

    [[nodiscard]] bool degradedHeadlessVideoActive() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return degradedHeadlessVideoActive_;
    }

    [[nodiscard]] bool degradedAudioDisabledActive() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return degradedAudioDisabledActive_;
    }

    [[nodiscard]] MachineTransitionPolicy policyFor(MachineTransitionReason reason) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return policies_[static_cast<std::size_t>(reason)];
    }

    void setPolicyFor(MachineTransitionReason reason, const MachineTransitionPolicy& policy) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        policies_[static_cast<std::size_t>(reason)] = policy;
    }

private:
    [[nodiscard]] bool pauseLanesLocked(bool& wasVideoActive) noexcept;
    [[nodiscard]] bool resumeLanesLocked(bool resumeVideo) noexcept;
    [[nodiscard]] bool bumpEpochsLocked() noexcept;

    template <typename DurationT>
    void noteDurationLocked(DurationT duration) noexcept
    {
        const auto ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
        durationHistoryNs_.push_back(ns);
        if (durationHistoryNs_.size() > kDurationHistoryLimit) {
            durationHistoryNs_.erase(durationHistoryNs_.begin());
        }
        stats_.transitionDurationMaxNs = std::max(stats_.transitionDurationMaxNs, ns);
        if (durationHistoryNs_.empty()) {
            return;
        }
        std::vector<std::uint64_t> sorted = durationHistoryNs_;
        std::sort(sorted.begin(), sorted.end());
        const auto p50 = sorted[(sorted.size() - 1u) / 2u];
        const auto p95Index = (sorted.size() - 1u) * 95u / 100u;
        stats_.transitionDurationP50Ns = p50;
        stats_.transitionDurationP95Ns = sorted[p95Index];
    }

    static constexpr std::size_t kDurationHistoryLimit = 128u;
    mutable std::mutex mutex_{};
    AudioService* audioService_ = nullptr;
    VideoService* videoService_ = nullptr;
    std::vector<std::uint64_t> durationHistoryNs_{};
    std::array<MachineTransitionPolicy, 5> policies_{{
        MachineTransitionPolicy{.maxRetries = 0, .allowHeadlessVideoFallback = false, .allowAudioBackendDisable = false, .failHard = true},  // RomLoad
        MachineTransitionPolicy{.maxRetries = 0, .allowHeadlessVideoFallback = false, .allowAudioBackendDisable = false, .failHard = true},  // HardReset
        MachineTransitionPolicy{.maxRetries = 1, .allowHeadlessVideoFallback = false, .allowAudioBackendDisable = true, .failHard = false},  // AudioBackendRestart
        MachineTransitionPolicy{.maxRetries = 1, .allowHeadlessVideoFallback = true, .allowAudioBackendDisable = false, .failHard = false},  // VideoBackendRestart
        MachineTransitionPolicy{.maxRetries = 1, .allowHeadlessVideoFallback = true, .allowAudioBackendDisable = true, .failHard = false},   // ConfigReconfigure
    }};
    MachineLifecycleCoordinatorStats stats_{};
    MachineTransitionResult lastResult_{};
    bool degradedHeadlessVideoActive_ = false;
    bool degradedAudioDisabledActive_ = false;
};

} // namespace BMMQ

#endif // BMMQ_MACHINE_LIFECYCLE_COORDINATOR_HPP
