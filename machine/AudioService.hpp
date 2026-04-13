#ifndef BMMQ_AUDIO_SERVICE_HPP
#define BMMQ_AUDIO_SERVICE_HPP

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "AudioPipeline.hpp"
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

    [[nodiscard]] AudioPipeline& pipeline() noexcept
    {
        return pipeline_;
    }

    [[nodiscard]] const AudioPipeline& pipeline() const noexcept
    {
        return pipeline_;
    }

    void addProcessor(std::unique_ptr<IAudioProcessor> processor)
    {
        if (!guardResetSafety()) {
            return;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety()) {
            return;
        }
        pipeline_.addProcessor(std::move(processor));
    }

    void clearProcessors()
    {
        if (!guardResetSafety()) {
            return;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety()) {
            return;
        }
        pipeline_.clearProcessors();
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

    void renderForOutput(std::span<int16_t> output) noexcept
    {
        engine_.render(output);

        if (pipeline_.empty()) {
            return;
        }

        AudioBufferView input{std::span<const int16_t>(output.data(), output.size()),
                              engine_.config().deviceSampleRate};
        auto processed = pipeline_.process(input, pipelineOutputScratch_);
        const auto copyCount = std::min(output.size(), processed.samples.size());
        if (processed.samples.data() != output.data() && copyCount > 0) {
            std::memmove(output.data(), processed.samples.data(), copyCount * sizeof(int16_t));
        }
        if (copyCount < output.size()) {
            std::fill(output.begin() + static_cast<std::ptrdiff_t>(copyCount), output.end(), 0);
        }
    }

private:
    [[nodiscard]] bool guardResetSafety() const noexcept
    {
        const bool safe = canPerformReset();
        assert((safe) && "AudioService reset/configure called while audio callback may be active");
        return safe;
    }

    AudioEngine engine_{};
    AudioPipeline pipeline_{};
    // Caller-owned pipeline output storage for renderForOutput(). This buffer belongs
    // to this AudioService instance, should be sized/reserved during non-real-time
    // configuration/init to the maximum expected frame count, and then retained for
    // the configured pipeline lifetime. Real-time code paths (render/callback) must
    // only read/write within preallocated capacity and must not trigger resize/allocation.
    std::vector<int16_t> pipelineOutputScratch_{};
    std::atomic<bool> backendPausedOrClosed_{true};
    mutable std::mutex nonRealTimeMutex_;
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_SERVICE_HPP
