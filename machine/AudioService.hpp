#ifndef BMMQ_AUDIO_SERVICE_HPP
#define BMMQ_AUDIO_SERVICE_HPP

#include <atomic>
#include <cassert>
#include <mutex>
#include <utility>

#include "plugins/AudioEngine.hpp"

namespace BMMQ {

class AudioService {
public:
    AudioService() = default;

    explicit AudioService(AudioEngineConfig config)
        : engine_(std::move(config)) {}

    [[nodiscard]] AudioEngine& engine() noexcept
    {
        return engine_;
    }

    [[nodiscard]] const AudioEngine& engine() const noexcept
    {
        return engine_;
    }

    // Reports whether reset/configure calls are currently safe relative to the audio callback.
    // Callers should check this before invoking reset/configure methods.
    [[nodiscard]] bool canPerformReset() const noexcept
    {
        return backendPausedOrClosed_.load(std::memory_order_acquire);
    }

    // Marks whether the backend is paused/closed (safe for reset/configure) or active.
    // Backend lifecycle code should call this when opening/closing or pausing/resuming callbacks.
    void setBackendPausedOrClosed(bool pausedOrClosed) noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        backendPausedOrClosed_.store(pausedOrClosed, std::memory_order_release);
    }

    // Not real-time safe. Requires canPerformReset() == true.
    // In debug builds, unsafe calls trigger an assertion and return early.
    void resetStream() noexcept
    {
        if (!guardResetSafety()) {
            return;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety()) {
            return;
        }
        engine_.resetStream();
    }

    // Not real-time safe. Requires canPerformReset() == true.
    // In debug builds, unsafe calls trigger an assertion and return early.
    void resetStats() noexcept
    {
        if (!guardResetSafety()) {
            return;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety()) {
            return;
        }
        engine_.resetStats();
    }

    // Not real-time safe. Requires canPerformReset() == true.
    // In debug builds, unsafe calls trigger an assertion and return early.
    void configureEngine(const AudioEngineConfig& config)
    {
        if (!guardResetSafety()) {
            return;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety()) {
            return;
        }
        engine_.configure(config);
    }

private:
    [[nodiscard]] bool guardResetSafety() const noexcept
    {
        const bool safe = canPerformReset();
        assert((safe) && "AudioService reset/configure called while audio callback may be active");
        return safe;
    }

    AudioEngine engine_{};
    std::atomic<bool> backendPausedOrClosed_{true};
    mutable std::mutex nonRealTimeMutex_;
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_SERVICE_HPP
