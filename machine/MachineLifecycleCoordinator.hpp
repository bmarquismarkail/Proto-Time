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
    std::size_t failureCount = 0;
    std::array<std::size_t, 5> reasonCounts{};
    std::uint64_t transitionDurationP50Ns = 0;
    std::uint64_t transitionDurationP95Ns = 0;
    std::uint64_t transitionDurationMaxNs = 0;
};

class MachineLifecycleCoordinator {
public:
    using TransitionMutation = std::function<bool()>;

    void bindServices(AudioService* audio, VideoService* video) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        audioService_ = audio;
        videoService_ = video;
    }

    [[nodiscard]] bool runTransition(MachineTransitionReason reason, const TransitionMutation& mutation)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto start = std::chrono::steady_clock::now();
        ++stats_.transitionCount;
        ++stats_.reasonCounts[static_cast<std::size_t>(reason)];

        const bool wasVideoActive = pauseLanesLocked();
        const bool mutationOk = mutation ? mutation() : true;
        bumpEpochsLocked();
        const bool resumeOk = resumeLanesLocked(wasVideoActive);
        const bool ok = mutationOk && resumeOk;
        if (ok) {
            ++stats_.successCount;
        } else {
            ++stats_.failureCount;
        }
        noteDurationLocked(std::chrono::steady_clock::now() - start);
        return ok;
    }

    [[nodiscard]] MachineLifecycleCoordinatorStats stats() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

private:
    [[nodiscard]] bool pauseLanesLocked() noexcept;
    [[nodiscard]] bool resumeLanesLocked(bool resumeVideo) noexcept;
    void bumpEpochsLocked() noexcept;

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
    MachineLifecycleCoordinatorStats stats_{};
};

} // namespace BMMQ

#endif // BMMQ_MACHINE_LIFECYCLE_COORDINATOR_HPP
