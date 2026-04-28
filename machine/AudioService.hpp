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

    [[nodiscard]] bool addProcessor(std::unique_ptr<IAudioProcessor> processor)
    {
        if (!guardResetSafety(false)) {
            return false;
        }
        if (processor == nullptr || !isLiveCallbackCompatible(*processor)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety(false)) {
            return false;
        }
        pipeline_.addProcessor(std::move(processor));
        return true;
    }

    [[nodiscard]] bool clearProcessors()
    {
        if (!guardResetSafety(false)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety(false)) {
            return false;
        }
        pipeline_.clearProcessors();
        return true;
    }

    [[nodiscard]] bool configureFixedCallbackCapacity(std::size_t callbackSamples)
    {
        if (!guardResetSafety(false)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety(false)) {
            return false;
        }

        callbackCapacitySamples_ = std::max<std::size_t>(callbackSamples, 1u);
        pipeline_.configureFixedCapacity(callbackCapacitySamples_);
        pipelineOutputScratch_.assign(callbackCapacitySamples_, 0);
        return true;
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
    [[nodiscard]] bool resetStream() noexcept
    {
        if (!guardResetSafety(true)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety(true)) {
            return false;
        }
        engine_.resetStream();
        return true;
    }

    // Not real-time safe. Requires canPerformReset() == true.
    // In debug builds, unsafe calls trigger an assertion and return early.
    [[nodiscard]] bool resetStats() noexcept
    {
        if (!guardResetSafety(true)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety(true)) {
            return false;
        }
        engine_.resetStats();
        return true;
    }

    // Not real-time safe. Requires canPerformReset() == true.
    // In debug builds, unsafe calls trigger an assertion and return early.
    [[nodiscard]] bool configureEngine(const AudioEngineConfig& config)
    {
        if (!guardResetSafety(true)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety(true)) {
            return false;
        }
        engine_.configure(config);
        callbackCapacitySamples_ = std::max<std::size_t>(engine_.config().frameChunkSamples, 1u);
        pipeline_.configureFixedCapacity(callbackCapacitySamples_);
        pipelineOutputScratch_.assign(callbackCapacitySamples_, 0);
        return true;
    }

    void renderForOutput(std::span<int16_t> output) noexcept
    {
        engine_.render(output);

        if (pipeline_.empty()) {
            return;
        }

        AudioBufferView input{std::span<const int16_t>(output.data(), output.size()),
                              engine_.config().deviceSampleRate,
                              engine_.config().channelCount};
        if (pipelineOutputScratch_.size() < output.size()) {
            engine_.notePipelineCapacitySkip();
            return;
        }

        std::size_t producedSamples = 0;
        const bool processed = pipeline_.process(
            input,
            std::span<int16_t>(pipelineOutputScratch_.data(), pipelineOutputScratch_.size()),
            producedSamples);
        if (!processed) {
            std::fill(output.begin(), output.end(), 0);
            return;
        }

        const auto copyCount = std::min(output.size(), producedSamples);
        if (copyCount > 0u) {
            std::memmove(output.data(), pipelineOutputScratch_.data(), copyCount * sizeof(int16_t));
        }
        if (copyCount < output.size()) {
            std::fill(output.begin() + static_cast<std::ptrdiff_t>(copyCount), output.end(), 0);
        }
    }

private:
    [[nodiscard]] static bool isLiveCallbackCompatible(const IAudioProcessor& processor) noexcept
    {
        const auto caps = processor.capabilities();
        return caps.realtimeSafe && caps.fixedCapacityOutput;
    }

    [[nodiscard]] bool guardResetSafety(bool assertOnUnsafe) const noexcept
    {
        const bool safe = canPerformReset();
        if (assertOnUnsafe) {
            assert((safe) && "AudioService reset/configure called while audio callback may be active");
        }
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
    std::size_t callbackCapacitySamples_ = 0;
    std::atomic<bool> backendPausedOrClosed_{true};
    mutable std::mutex nonRealTimeMutex_;
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_SERVICE_HPP
