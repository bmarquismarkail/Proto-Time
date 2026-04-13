#ifndef BMMQ_AUDIO_ENGINE_HPP
#define BMMQ_AUDIO_ENGINE_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include "SdlAudioResampler.hpp"

namespace BMMQ {

struct AudioEngineConfig {
    int sourceSampleRate = 48000;
    int deviceSampleRate = 48000;
    std::size_t ringBufferCapacitySamples = 2048;
    std::size_t frameChunkSamples = 256;
};

struct AudioEngineStats {
    std::size_t bufferedHighWaterSamples = 0;
    std::size_t callbackCount = 0;
    std::size_t samplesDelivered = 0;
    std::size_t underrunCount = 0;
    std::size_t silenceSamplesFilled = 0;
    std::size_t overrunDropCount = 0;
    std::size_t droppedSamples = 0;
    std::size_t sourceSamplesPushed = 0;
    std::size_t sourceSamplesConsumed = 0;
    std::size_t outputSamplesProduced = 0;
    std::size_t pipelineCapacitySkipCount = 0;
    bool resamplingActive = false;
    double resampleRatio = 1.0;
};

class AudioEngine {
public:
    explicit AudioEngine(const AudioEngineConfig& config = {})
        : config_(config),
          resampler_(config.sourceSampleRate, config.deviceSampleRate)
    {
        initializeBuffer();
        resetStats();
        resetStream();
    }

    void configure(const AudioEngineConfig& config)
    {
        config_ = config;
        resampler_.configure(config_.sourceSampleRate, config_.deviceSampleRate);
        initializeBuffer();
        resetStats();
        resetStream();
    }

    [[nodiscard]] const AudioEngineConfig& config() const noexcept
    {
        return config_;
    }

    void setDeviceSampleRate(int deviceSampleRate)
    {
        config_.deviceSampleRate = std::max(deviceSampleRate, 1);
        resampler_.configure(config_.sourceSampleRate, config_.deviceSampleRate);
    }

    [[nodiscard]] std::size_t bufferedSamples() const noexcept
    {
        const auto capacity = buffer_.size();
        if (capacity == 0u) {
            return 0u;
        }

        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        const auto writeIndex = writeIndex_.load(std::memory_order_acquire);
        if (writeIndex >= readIndex) {
            return writeIndex - readIndex;
        }
        return capacity - (readIndex - writeIndex);
    }

    [[nodiscard]] std::size_t queuedBytes() const noexcept
    {
        return bufferedSamples() * sizeof(int16_t);
    }

    [[nodiscard]] std::size_t bufferCapacitySamples() const noexcept
    {
        return buffer_.size();
    }

    // Not real-time safe against concurrent callbacks.
    // Call through AudioService reset helpers after the backend is paused/closed.
    // AudioService debug builds assert on unsafe reset usage.
    void resetStats() noexcept
    {
        bufferedHighWaterSamples_.store(0u, std::memory_order_relaxed);
        callbackCount_.store(0u, std::memory_order_relaxed);
        samplesDelivered_.store(0u, std::memory_order_relaxed);
        underrunCount_.store(0u, std::memory_order_relaxed);
        silenceSamplesFilled_.store(0u, std::memory_order_relaxed);
        overrunDropCount_.store(0u, std::memory_order_relaxed);
        droppedSamples_.store(0u, std::memory_order_relaxed);
        sourceSamplesPushed_.store(0u, std::memory_order_relaxed);
        sourceSamplesConsumed_.store(0u, std::memory_order_relaxed);
        outputSamplesProduced_.store(0u, std::memory_order_relaxed);
        pipelineCapacitySkipCount_.store(0u, std::memory_order_relaxed);
    }

    // Not real-time safe against concurrent callbacks.
    // Call through AudioService reset helpers after the backend is paused/closed.
    // AudioService debug builds assert on unsafe reset usage.
    void resetStream() noexcept
    {
        readIndex_.store(0u, std::memory_order_release);
        writeIndex_.store(0u, std::memory_order_release);
        pendingReset_.store(false, std::memory_order_release);
        resampler_.reset();
        lastFrameCounter_ = 0u;
        clearDeferredResetChunk();
    }

    // Real-time path producer entrypoint. Remains lock-free and unchanged.
    // Callers must avoid concurrent reset/configure unless AudioService reports reset-safe.
    void appendRecentPcm(std::span<const int16_t> pcm, uint64_t frameCounter)
    {
        if (pcm.empty()) {
            return;
        }

        const bool isFirstFrame = (lastFrameCounter_ == 0u);
        const bool resetRequired = frameCounter < lastFrameCounter_;
        std::size_t desiredSampleCount = 0u;
        if (isFirstFrame || resetRequired) {
            desiredSampleCount = std::min<std::size_t>(pcm.size(), config_.frameChunkSamples);
        } else if (frameCounter > lastFrameCounter_) {
            const auto frameDelta = frameCounter - lastFrameCounter_;
            desiredSampleCount = static_cast<std::size_t>(std::min<uint64_t>(
                static_cast<uint64_t>(pcm.size()),
                frameDelta * static_cast<uint64_t>(config_.frameChunkSamples)));
        }

        if (desiredSampleCount == 0u) {
            return;
        }

        if (resetRequired) {
            stageDeferredResetChunk(pcm, desiredSampleCount);
            pendingReset_.store(true, std::memory_order_release);
        } else if (pendingReset_.load(std::memory_order_acquire)) {
            // Keep post-reset audio out of the live ring until the callback has flushed
            // the old generation. With a single deferred chunk, newest data wins.
            stageDeferredResetChunk(pcm, desiredSampleCount);
        } else {
            const auto startIndex = pcm.size() - desiredSampleCount;
            for (std::size_t i = startIndex; i < pcm.size(); ++i) {
                pushSample(pcm[i]);
            }
        }
        lastFrameCounter_ = frameCounter;
    }

    // Real-time callback consumer entrypoint. Remains lock-free and unchanged.
    // Callers must avoid concurrent reset/configure unless AudioService reports reset-safe.
    void render(std::span<int16_t> output) noexcept
    {
        if (pendingReset_.exchange(false, std::memory_order_acq_rel)) {
            // Flush buffered source samples at a callback boundary instead of producer-side reset.
            // This avoids concurrent producer resets of callback-consumed state.
            resampler_.reset();
            readIndex_.store(writeIndex_.load(std::memory_order_acquire), std::memory_order_release);
            publishDeferredResetChunk();
        }

        callbackCount_.fetch_add(1u, std::memory_order_relaxed);

        const auto renderStats = resampler_.render(
            output,
            [this](std::size_t offset, int16_t& sample) noexcept {
                return peekSample(offset, sample);
            },
            [this](std::size_t requested) noexcept {
                const auto actual = std::min(requested, bufferedSamples());
                consumeSamples(actual);
                return actual;
            });

        const auto delivered = renderStats.outputSamplesProduced - renderStats.silenceSamplesFilled;
        if (renderStats.silenceSamplesFilled != 0u) {
            underrunCount_.fetch_add(1u, std::memory_order_relaxed);
            silenceSamplesFilled_.fetch_add(renderStats.silenceSamplesFilled, std::memory_order_relaxed);
        }

        samplesDelivered_.fetch_add(delivered, std::memory_order_relaxed);
        sourceSamplesConsumed_.fetch_add(renderStats.sourceSamplesConsumed, std::memory_order_relaxed);
        outputSamplesProduced_.fetch_add(renderStats.outputSamplesProduced, std::memory_order_relaxed);
    }

    [[nodiscard]] AudioEngineStats stats() const noexcept
    {
        AudioEngineStats stats;
        stats.bufferedHighWaterSamples = bufferedHighWaterSamples_.load(std::memory_order_relaxed);
        stats.callbackCount = callbackCount_.load(std::memory_order_relaxed);
        stats.samplesDelivered = samplesDelivered_.load(std::memory_order_relaxed);
        stats.underrunCount = underrunCount_.load(std::memory_order_relaxed);
        stats.silenceSamplesFilled = silenceSamplesFilled_.load(std::memory_order_relaxed);
        stats.overrunDropCount = overrunDropCount_.load(std::memory_order_relaxed);
        stats.droppedSamples = droppedSamples_.load(std::memory_order_relaxed);
        stats.sourceSamplesPushed = sourceSamplesPushed_.load(std::memory_order_relaxed);
        stats.sourceSamplesConsumed = sourceSamplesConsumed_.load(std::memory_order_relaxed);
        stats.outputSamplesProduced = outputSamplesProduced_.load(std::memory_order_relaxed);
        stats.pipelineCapacitySkipCount = pipelineCapacitySkipCount_.load(std::memory_order_relaxed);
        stats.resamplingActive = config_.deviceSampleRate != config_.sourceSampleRate;
        stats.resampleRatio = resampler_.ratio();
        return stats;
    }

    void notePipelineCapacitySkip() noexcept
    {
        pipelineCapacitySkipCount_.fetch_add(1u, std::memory_order_relaxed);
    }

private:
    void initializeBuffer()
    {
        const auto capacity = std::max<std::size_t>(config_.ringBufferCapacitySamples, config_.frameChunkSamples);
        buffer_.assign(capacity, 0);
        deferredResetChunk_.assign(std::max<std::size_t>(config_.frameChunkSamples, 1u), 0);
        deferredResetChunkSize_ = 0u;
    }

    void stageDeferredResetChunk(std::span<const int16_t> pcm, std::size_t desiredSampleCount) noexcept
    {
        lockDeferredResetChunk();
        const auto boundedCount = std::min(desiredSampleCount, deferredResetChunk_.size());
        deferredResetChunkSize_ = boundedCount;
        const auto startIndex = pcm.size() - boundedCount;
        for (std::size_t i = 0; i < boundedCount; ++i) {
            deferredResetChunk_[i] = pcm[startIndex + i];
        }
        unlockDeferredResetChunk();
    }

    void publishDeferredResetChunk() noexcept
    {
        lockDeferredResetChunk();
        const auto count = deferredResetChunkSize_;
        for (std::size_t i = 0; i < count; ++i) {
            pushSample(deferredResetChunk_[i]);
        }
        deferredResetChunkSize_ = 0u;
        unlockDeferredResetChunk();
    }

    void clearDeferredResetChunk() noexcept
    {
        lockDeferredResetChunk();
        deferredResetChunkSize_ = 0u;
        unlockDeferredResetChunk();
    }

    void lockDeferredResetChunk() noexcept
    {
        while (deferredResetChunkLock_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void unlockDeferredResetChunk() noexcept
    {
        deferredResetChunkLock_.clear(std::memory_order_release);
    }

    void noteBufferedHighWater(std::size_t buffered) noexcept
    {
        auto previous = bufferedHighWaterSamples_.load(std::memory_order_relaxed);
        while (buffered > previous &&
               !bufferedHighWaterSamples_.compare_exchange_weak(
                   previous, buffered, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    void pushSample(int16_t sample) noexcept
    {
        const auto capacity = buffer_.size();
        if (capacity == 0u) {
            return;
        }

        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        const auto writeIndex = writeIndex_.load(std::memory_order_relaxed);
        const auto nextWriteIndex = (writeIndex + 1u) % capacity;
        if (nextWriteIndex == readIndex) {
            // Keep index ownership simple: producer drops on full buffer.
            overrunDropCount_.fetch_add(1u, std::memory_order_relaxed);
            droppedSamples_.fetch_add(1u, std::memory_order_relaxed);
            return;
        }

        buffer_[writeIndex] = sample;
        writeIndex_.store(nextWriteIndex, std::memory_order_release);
        sourceSamplesPushed_.fetch_add(1u, std::memory_order_relaxed);
        noteBufferedHighWater(bufferedSamples());
    }

    [[nodiscard]] bool peekSample(std::size_t offset, int16_t& sample) const noexcept
    {
        const auto capacity = buffer_.size();
        if (capacity == 0u) {
            return false;
        }

        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        const auto writeIndex = writeIndex_.load(std::memory_order_acquire);
        const auto available = writeIndex >= readIndex
                                 ? (writeIndex - readIndex)
                                 : (capacity - (readIndex - writeIndex));
        if (offset >= available) {
            return false;
        }

        sample = buffer_[(readIndex + offset) % capacity];
        return true;
    }

    void consumeSamples(std::size_t count) noexcept
    {
        const auto capacity = buffer_.size();
        if (capacity == 0u || count == 0u) {
            return;
        }

        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        const auto writeIndex = writeIndex_.load(std::memory_order_acquire);
        const auto available = writeIndex >= readIndex
                                 ? (writeIndex - readIndex)
                                 : (capacity - (readIndex - writeIndex));
        const auto consumed = std::min(count, available);
        if (consumed == 0u) {
            return;
        }

        readIndex_.store((readIndex + consumed) % capacity, std::memory_order_release);
    }

    AudioEngineConfig config_{};
    SdlAudioResampler resampler_{48000, 48000};
    std::vector<int16_t> buffer_;
    std::atomic<std::size_t> readIndex_{0};
    std::atomic<std::size_t> writeIndex_{0};
    std::atomic<std::size_t> bufferedHighWaterSamples_{0};
    std::atomic<std::size_t> callbackCount_{0};
    std::atomic<std::size_t> samplesDelivered_{0};
    std::atomic<std::size_t> underrunCount_{0};
    std::atomic<std::size_t> silenceSamplesFilled_{0};
    std::atomic<std::size_t> overrunDropCount_{0};
    std::atomic<std::size_t> droppedSamples_{0};
    std::atomic<std::size_t> sourceSamplesPushed_{0};
    std::atomic<std::size_t> sourceSamplesConsumed_{0};
    std::atomic<std::size_t> outputSamplesProduced_{0};
    std::atomic<std::size_t> pipelineCapacitySkipCount_{0};
    std::atomic<bool> pendingReset_{false};
    std::vector<int16_t> deferredResetChunk_{};
    std::size_t deferredResetChunkSize_ = 0u;
    std::atomic_flag deferredResetChunkLock_ = ATOMIC_FLAG_INIT;
    uint64_t lastFrameCounter_ = 0;
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_ENGINE_HPP
