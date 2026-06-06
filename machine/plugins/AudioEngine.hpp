#ifndef BMMQ_AUDIO_ENGINE_HPP
#define BMMQ_AUDIO_ENGINE_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "SdlAudioResampler.hpp"

namespace BMMQ {

struct AudioEngineConfig {
    int sourceSampleRate = 48000;
    int deviceSampleRate = 48000;
    uint8_t channelCount = 1;
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
    std::size_t appendCallCount = 0;
    std::size_t appendSamplesRequested = 0;
    std::size_t appendSamplesAccepted = 0;
    std::size_t appendSamplesRejected = 0;
    std::size_t appendSamplesTruncated = 0;
    std::size_t appendBufferedSamplesLast = 0;
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
          resampler_(config.sourceSampleRate, config.deviceSampleRate, config.channelCount)
    {
        initializeBuffer();
        resetStats();
        resetStream();
    }

    void configure(const AudioEngineConfig& config)
    {
        config_ = config;
        resampler_.configure(config_.sourceSampleRate, config_.deviceSampleRate, config_.channelCount);
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
        resampler_.configure(config_.sourceSampleRate, config_.deviceSampleRate, config_.channelCount);
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
        appendCallCount_.store(0u, std::memory_order_relaxed);
        appendSamplesRequested_.store(0u, std::memory_order_relaxed);
        appendSamplesAccepted_.store(0u, std::memory_order_relaxed);
        appendSamplesRejected_.store(0u, std::memory_order_relaxed);
        appendSamplesTruncated_.store(0u, std::memory_order_relaxed);
        appendBufferedSamplesLast_.store(0u, std::memory_order_relaxed);
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
        appendCallCount_.fetch_add(1u, std::memory_order_relaxed);
        appendSamplesRequested_.fetch_add(pcm.size(), std::memory_order_relaxed);

        const bool isFirstFrame = (lastFrameCounter_ == 0u);
        const bool resetRequired = frameCounter < lastFrameCounter_;
        std::size_t desiredSampleCount = 0u;
        if (isFirstFrame || resetRequired) {
            desiredSampleCount = pcm.size();
        } else if (frameCounter > lastFrameCounter_) {
            desiredSampleCount = pcm.size();
        }

        if (desiredSampleCount == 0u) {
            appendSamplesRejected_.fetch_add(pcm.size(), std::memory_order_relaxed);
            return;
        }
        std::size_t acceptedSampleCount = 0u;

        if (resetRequired) {
            acceptedSampleCount = stageDeferredResetChunk(pcm, desiredSampleCount);
            pendingReset_.store(true, std::memory_order_release);
        } else if (pendingReset_.load(std::memory_order_acquire)) {
            // Keep post-reset audio out of the live ring until the callback has flushed
            // the old generation. With a single deferred chunk, newest data wins.
            acceptedSampleCount = stageDeferredResetChunk(pcm, desiredSampleCount);
        } else {
            const auto writableCapacity = writableCapacitySamples();
            const auto boundedSampleCount = std::min(desiredSampleCount, writableCapacity);
            const auto startIndex = pcm.size() - boundedSampleCount;
            for (std::size_t i = startIndex; i < pcm.size(); ++i) {
                if (pushSampleNoOverwrite(pcm[i])) {
                    ++acceptedSampleCount;
                }
            }
        }
        if (desiredSampleCount > acceptedSampleCount) {
            appendSamplesTruncated_.fetch_add(
                desiredSampleCount - acceptedSampleCount,
                std::memory_order_relaxed);
        }
        appendSamplesAccepted_.fetch_add(acceptedSampleCount, std::memory_order_relaxed);
        appendBufferedSamplesLast_.store(bufferedSamples(), std::memory_order_relaxed);
        lastFrameCounter_ = frameCounter;
    }

    // Real-time callback consumer entrypoint. Remains lock-free and unchanged.
    // Callers must avoid concurrent reset/configure unless AudioService reports reset-safe.
    void render(std::span<int16_t> output) noexcept
    {
        callbackCount_.fetch_add(1u, std::memory_order_relaxed);

        if (pendingReset_.load(std::memory_order_acquire)) {
            // Flush buffered source samples at a callback boundary instead of producer-side reset.
            // This avoids concurrent producer resets of callback-consumed state.
            resampler_.reset();
            readIndex_.store(writeIndex_.load(std::memory_order_acquire), std::memory_order_release);
            if (publishDeferredResetChunk()) {
                pendingReset_.store(false, std::memory_order_release);
            }
        }

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
        stats.appendCallCount = appendCallCount_.load(std::memory_order_relaxed);
        stats.appendSamplesRequested = appendSamplesRequested_.load(std::memory_order_relaxed);
        stats.appendSamplesAccepted = appendSamplesAccepted_.load(std::memory_order_relaxed);
        stats.appendSamplesRejected = appendSamplesRejected_.load(std::memory_order_relaxed);
        stats.appendSamplesTruncated = appendSamplesTruncated_.load(std::memory_order_relaxed);
        stats.appendBufferedSamplesLast = appendBufferedSamplesLast_.load(std::memory_order_relaxed);
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
    static constexpr uint8_t kInvalidDeferredResetSlot = 0xFFu;

    void initializeBuffer()
    {
        const auto capacity = std::max<std::size_t>(config_.ringBufferCapacitySamples, config_.frameChunkSamples);
        buffer_.assign(capacity, 0);
        const auto usableCapacity = capacity > 1u ? (capacity - 1u) : 0u;
        const auto chunkCapacity =
            std::max<std::size_t>(std::max<std::size_t>(usableCapacity, config_.frameChunkSamples), 1u);
        deferredResetChunks_[0].assign(chunkCapacity, 0);
        deferredResetChunks_[1].assign(chunkCapacity, 0);
        deferredResetPublishScratch_.assign(chunkCapacity, 0);
        deferredResetPublishedSlot_.store(kInvalidDeferredResetSlot, std::memory_order_relaxed);
        deferredResetPublishedSize_.store(0u, std::memory_order_relaxed);
        deferredResetNextWriteSlot_ = 0u;
    }

    [[nodiscard]] std::size_t stageDeferredResetChunk(
        std::span<const int16_t> pcm, std::size_t desiredSampleCount) noexcept
    {
        const auto publishedSlot = deferredResetPublishedSlot_.load(std::memory_order_acquire);
        const uint8_t targetSlot = publishedSlot < deferredResetChunks_.size()
            ? static_cast<uint8_t>(publishedSlot ^ 1u)
            : deferredResetNextWriteSlot_;
        auto& targetBuffer = deferredResetChunks_[targetSlot];
        const auto boundedCount = std::min(desiredSampleCount, targetBuffer.size());
        const auto startIndex = pcm.size() - boundedCount;
        for (std::size_t i = 0; i < boundedCount; ++i) {
            targetBuffer[i] = pcm[startIndex + i];
        }
        deferredResetPublishedSize_.store(boundedCount, std::memory_order_relaxed);
        deferredResetPublishedSlot_.store(targetSlot, std::memory_order_release);
        deferredResetNextWriteSlot_ = static_cast<uint8_t>(targetSlot ^ 1u);
        return boundedCount;
    }

    [[nodiscard]] bool publishDeferredResetChunk() noexcept
    {
        const auto publishedSlot = deferredResetPublishedSlot_.load(std::memory_order_acquire);
        if (publishedSlot >= deferredResetChunks_.size()) {
            return false;
        }

        const auto count = std::min(
            deferredResetPublishedSize_.load(std::memory_order_relaxed),
            deferredResetPublishScratch_.size());
        const auto& publishedBuffer = deferredResetChunks_[publishedSlot];
        for (std::size_t i = 0; i < count; ++i) {
            deferredResetPublishScratch_[i] = publishedBuffer[i];
        }

        if (deferredResetPublishedSlot_.load(std::memory_order_acquire) != publishedSlot) {
            return false;
        }

        uint8_t expectedSlot = publishedSlot;
        if (!deferredResetPublishedSlot_.compare_exchange_strong(
                expectedSlot,
                kInvalidDeferredResetSlot,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return false;
        }

        deferredResetPublishedSize_.store(0u, std::memory_order_relaxed);
        for (std::size_t i = 0; i < count; ++i) {
            (void)pushSampleNoOverwrite(deferredResetPublishScratch_[i]);
        }
        return true;
    }

    void clearDeferredResetChunk() noexcept
    {
        deferredResetPublishedSize_.store(0u, std::memory_order_relaxed);
        deferredResetPublishedSlot_.store(kInvalidDeferredResetSlot, std::memory_order_relaxed);
        deferredResetNextWriteSlot_ = 0u;
    }

    void noteBufferedHighWater(std::size_t buffered) noexcept
    {
        auto previous = bufferedHighWaterSamples_.load(std::memory_order_relaxed);
        while (buffered > previous &&
               !bufferedHighWaterSamples_.compare_exchange_weak(
                   previous, buffered, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    [[nodiscard]] std::size_t writableCapacitySamples() const noexcept
    {
        const auto capacity = buffer_.size();
        if (capacity <= 1u) {
            return 0u;
        }
        const auto usableCapacity = capacity - 1u;
        const auto buffered = bufferedSamples();
        return buffered >= usableCapacity ? 0u : (usableCapacity - buffered);
    }

    [[nodiscard]] bool pushSampleNoOverwrite(int16_t sample) noexcept
    {
        const auto capacity = buffer_.size();
        if (capacity <= 1u) {
            return false;
        }

        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        const auto writeIndex = writeIndex_.load(std::memory_order_relaxed);
        const auto nextWriteIndex = (writeIndex + 1u) % capacity;
        if (nextWriteIndex == readIndex) {
            // Preserve callback-thread safety: never overwrite unread slots.
            overrunDropCount_.fetch_add(1u, std::memory_order_relaxed);
            droppedSamples_.fetch_add(1u, std::memory_order_relaxed);
            return false;
        }

        buffer_[writeIndex] = sample;
        writeIndex_.store(nextWriteIndex, std::memory_order_release);
        sourceSamplesPushed_.fetch_add(1u, std::memory_order_relaxed);
        noteBufferedHighWater(bufferedSamples());
        return true;
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
    std::atomic<std::size_t> appendCallCount_{0};
    std::atomic<std::size_t> appendSamplesRequested_{0};
    std::atomic<std::size_t> appendSamplesAccepted_{0};
    std::atomic<std::size_t> appendSamplesRejected_{0};
    std::atomic<std::size_t> appendSamplesTruncated_{0};
    std::atomic<std::size_t> appendBufferedSamplesLast_{0};
    std::atomic<std::size_t> sourceSamplesConsumed_{0};
    std::atomic<std::size_t> outputSamplesProduced_{0};
    std::atomic<std::size_t> pipelineCapacitySkipCount_{0};
    std::atomic<bool> pendingReset_{false};
    std::array<std::vector<int16_t>, 2> deferredResetChunks_{};
    std::vector<int16_t> deferredResetPublishScratch_{};
    std::atomic<uint8_t> deferredResetPublishedSlot_{kInvalidDeferredResetSlot};
    std::atomic<std::size_t> deferredResetPublishedSize_{0u};
    uint8_t deferredResetNextWriteSlot_ = 0u;
    uint64_t lastFrameCounter_ = 0;
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_ENGINE_HPP
