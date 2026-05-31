#pragma once

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

#include "machine/DebugSnapshotTypes.hpp"
#include "machine/VideoDebugModel.hpp"
#include "machine/plugins/IoPlugin.hpp"

namespace BMMQ {

class BackgroundTaskService;

/// Decouples debug snapshot delivery from sharedStateMutex_ contention.
///
/// The emulation thread calls submitVideoModel() / submitAudioState() after
/// building the snapshot (which must happen on the emulation thread since it
/// reads live machine state).  The render thread calls tryConsumeVideo() /
/// tryConsumeAudio() inside serviceFrontend() to drain the queues and update
/// its own cached copies without waiting on the emulation thread.
///
/// Each queue is bounded (default 4 slots).  Overflow increments the stats
/// counter and returns false; the caller should fall back to the synchronous
/// in-place update path.
class DebugSnapshotService {
public:
    static constexpr std::size_t kDefaultVideoCapacity = 4u;
    static constexpr std::size_t kDefaultAudioCapacity = 4u;

    explicit DebugSnapshotService(
        std::size_t videoCapacity = kDefaultVideoCapacity,
        std::size_t audioCapacity = kDefaultAudioCapacity) noexcept;

    ~DebugSnapshotService() noexcept = default;

    void setBackgroundTaskService(BackgroundTaskService* service) noexcept;

    DebugSnapshotService(const DebugSnapshotService&) = delete;
    DebugSnapshotService& operator=(const DebugSnapshotService&) = delete;

    // -----------------------------------------------------------------------
    // Producer API — called from the emulation thread.
    // -----------------------------------------------------------------------

    /// Submit a video debug model for deferred consumption by the render thread.
    /// Returns true on success, false if the queue is full (overflow).
    /// Calling with nullopt is a no-op (returns true without enqueuing).
    bool submitVideoModel(std::optional<VideoDebugFrameModel> model);

    /// Submit an audio state snapshot for deferred consumption by the render thread.
    /// Returns true on success, false if the queue is full (overflow).
    /// Calling with nullopt is a no-op (returns true without enqueuing).
    bool submitAudioState(std::optional<AudioStateView> state);

    // -----------------------------------------------------------------------
    // Consumer API — called from the render thread.
    // -----------------------------------------------------------------------

    /// Dequeue the oldest video debug model, or nullopt if the queue is empty.
    [[nodiscard]] std::optional<VideoDebugFrameModel> tryConsumeVideo();

    /// Dequeue the oldest audio state snapshot, or nullopt if the queue is empty.
    [[nodiscard]] std::optional<AudioStateView> tryConsumeAudio();

    // -----------------------------------------------------------------------
    // Diagnostics — thread-safe.
    // -----------------------------------------------------------------------

    /// Snapshot of current counters.  All fields are read with relaxed order;
    /// values are approximate but consistent enough for diagnostics.
    [[nodiscard]] DebugSnapshotStats stats() const noexcept;

private:
    struct State;

    [[nodiscard]] static bool enqueueVideo(const std::shared_ptr<State>& state, VideoDebugFrameModel model);
    [[nodiscard]] static bool enqueueAudio(const std::shared_ptr<State>& state, AudioStateView stateView);

    std::shared_ptr<State> state_;
    BackgroundTaskService* backgroundTaskService_ = nullptr;
};

} // namespace BMMQ
