#ifndef BMMQ_AUDIO_SERVICE_HPP
#define BMMQ_AUDIO_SERVICE_HPP

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "AudioPipeline.hpp"
#include "plugins/AudioEngine.hpp"

namespace BMMQ {

struct AudioOutputTransportConfig {
    int deviceSampleRate = 48000;
    uint8_t channelCount = 1;
    std::size_t callbackChunkSamples = 256;
    std::size_t readyQueueChunks = 3;
};

struct AudioOutputTransportStats {
    std::size_t readyQueueDepth = 0;
    std::size_t readyQueueHighWaterChunks = 0;
    std::size_t drainCallbackCount = 0;
    std::size_t underrunCount = 0;
    std::size_t silenceSamplesFilled = 0;
    std::size_t workerProducedBlocks = 0;
    std::size_t droppedReadyBlocks = 0;
    std::size_t workerWakeCount = 0;
};

class AudioService {
public:
    AudioService() = default;

    explicit AudioService(AudioEngineConfig config)
        : engine_(std::move(config)) {}

    ~AudioService()
    {
        stopOutputTransport();
    }

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

    [[nodiscard]] bool configureOutputTransport(AudioOutputTransportConfig config)
    {
        if (!guardResetSafety(false)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety(false)) {
            return false;
        }

        config.deviceSampleRate = std::max(config.deviceSampleRate, 1);
        config.channelCount = std::max<uint8_t>(config.channelCount, 1u);
        config.callbackChunkSamples = std::max<std::size_t>(config.callbackChunkSamples, 1u);
        config.readyQueueChunks = std::max<std::size_t>(config.readyQueueChunks, 1u);

        outputTransportConfig_ = config;
        readyBlocks_.assign(config.readyQueueChunks + 1u,
                            std::vector<int16_t>(config.callbackChunkSamples, 0));
        producerScratch_.assign(config.callbackChunkSamples, 0);
        transportPipelineScratch_.assign(config.callbackChunkSamples, 0);
        readyReadIndex_.store(0u, std::memory_order_release);
        readyWriteIndex_.store(0u, std::memory_order_release);
        resetTransportStats();
        outputTransportConfigured_ = true;
        return true;
    }

    [[nodiscard]] bool startOutputTransport()
    {
        if (!guardResetSafety(false)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!guardResetSafety(false) || !outputTransportConfigured_) {
            return false;
        }
        if (outputTransportRunning_.load(std::memory_order_acquire)) {
            return true;
        }

        outputTransportStopRequested_.store(false, std::memory_order_release);
        outputTransportRunning_.store(true, std::memory_order_release);
        backendPausedOrClosed_.store(false, std::memory_order_release);
        outputTransportWorker_ = std::thread([this]() { outputTransportWorkerLoop(); });
        outputTransportCv_.notify_one();
        return true;
    }

    void stopOutputTransport() noexcept
    {
        const bool wasRunning = outputTransportRunning_.load(std::memory_order_acquire);
        outputTransportStopRequested_.store(true, std::memory_order_release);
        outputTransportCv_.notify_all();
        if (outputTransportWorker_.joinable()) {
            outputTransportWorker_.join();
        }
        if (wasRunning) {
            outputTransportRunning_.store(false, std::memory_order_release);
            readyReadIndex_.store(0u, std::memory_order_release);
            readyWriteIndex_.store(0u, std::memory_order_release);
        }
        if (backendPausedOrClosed_.load(std::memory_order_acquire)) {
            backendPausedOrClosed_.store(true, std::memory_order_release);
        }
    }

    // Reports whether reset/configure calls are currently safe relative to the audio callback.
    // Callers should check this before invoking reset/configure methods.
    [[nodiscard]] bool canPerformReset() const noexcept
    {
        return backendPausedOrClosed_.load(std::memory_order_acquire) &&
               !outputTransportRunning_.load(std::memory_order_acquire);
    }

    // Marks whether the backend callback/output thread is paused/closed.
    // Reset/configure is only safe when this is true and the output transport worker is stopped.
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

    void appendRecentPcm(std::span<const int16_t> pcm, uint64_t frameCounter)
    {
        engine_.appendRecentPcm(pcm, frameCounter);
        outputTransportCv_.notify_one();
    }

    [[nodiscard]] bool produceReadyOutputBlock() noexcept
    {
        if (!outputTransportConfigured_ || producerScratch_.empty() || engine_.bufferedSamples() == 0u) {
            return false;
        }

        const auto writeIndex = readyWriteIndex_.load(std::memory_order_relaxed);
        const auto nextWriteIndex = nextReadyIndex(writeIndex);
        if (nextWriteIndex == readyReadIndex_.load(std::memory_order_acquire)) {
            transportDroppedReadyBlocks_.fetch_add(1u, std::memory_order_relaxed);
            return false;
        }

        renderForOutputWithScratch(
            std::span<int16_t>(producerScratch_.data(), producerScratch_.size()),
            transportPipelineScratch_);
        readyBlocks_[writeIndex] = producerScratch_;
        readyWriteIndex_.store(nextWriteIndex, std::memory_order_release);
        transportWorkerProducedBlocks_.fetch_add(1u, std::memory_order_relaxed);
        noteReadyQueueHighWater(readyQueueDepth());
        return true;
    }

    void drainReadyOutput(std::span<int16_t> output) noexcept
    {
        transportDrainCallbackCount_.fetch_add(1u, std::memory_order_relaxed);
        if (output.empty()) {
            return;
        }

        const auto readIndex = readyReadIndex_.load(std::memory_order_relaxed);
        if (readIndex == readyWriteIndex_.load(std::memory_order_acquire)) {
            std::fill(output.begin(), output.end(), 0);
            transportUnderrunCount_.fetch_add(1u, std::memory_order_relaxed);
            transportSilenceSamplesFilled_.fetch_add(output.size(), std::memory_order_relaxed);
            outputTransportCv_.notify_one();
            return;
        }

        const auto& block = readyBlocks_[readIndex];
        const auto copyCount = std::min(output.size(), block.size());
        if (copyCount > 0u) {
            std::memmove(output.data(), block.data(), copyCount * sizeof(int16_t));
        }
        if (copyCount < output.size()) {
            std::fill(output.begin() + static_cast<std::ptrdiff_t>(copyCount), output.end(), 0);
            transportSilenceSamplesFilled_.fetch_add(output.size() - copyCount, std::memory_order_relaxed);
        }

        readyReadIndex_.store(nextReadyIndex(readIndex), std::memory_order_release);
        outputTransportCv_.notify_one();
    }

    [[nodiscard]] AudioOutputTransportStats transportStats() const noexcept
    {
        AudioOutputTransportStats stats;
        stats.readyQueueDepth = readyQueueDepth();
        stats.readyQueueHighWaterChunks = transportReadyQueueHighWaterChunks_.load(std::memory_order_relaxed);
        stats.drainCallbackCount = transportDrainCallbackCount_.load(std::memory_order_relaxed);
        stats.underrunCount = transportUnderrunCount_.load(std::memory_order_relaxed);
        stats.silenceSamplesFilled = transportSilenceSamplesFilled_.load(std::memory_order_relaxed);
        stats.workerProducedBlocks = transportWorkerProducedBlocks_.load(std::memory_order_relaxed);
        stats.droppedReadyBlocks = transportDroppedReadyBlocks_.load(std::memory_order_relaxed);
        stats.workerWakeCount = transportWorkerWakeCount_.load(std::memory_order_relaxed);
        return stats;
    }

    // Synchronous compatibility helper. Do not call this from audio callback/output threads;
    // production backends should use drainReadyOutput() and let the service-owned worker
    // perform render/resample/pipeline work.
    void renderForOutput(std::span<int16_t> output) noexcept
    {
        renderForOutputWithScratch(output, pipelineOutputScratch_);
    }

private:
    void renderForOutputWithScratch(std::span<int16_t> output, std::vector<int16_t>& scratch) noexcept
    {
        engine_.render(output);

        if (pipeline_.empty()) {
            return;
        }

        AudioBufferView input{std::span<const int16_t>(output.data(), output.size()),
                              engine_.config().deviceSampleRate,
                              engine_.config().channelCount};
        if (scratch.size() < output.size()) {
            engine_.notePipelineCapacitySkip();
            return;
        }

        std::size_t producedSamples = 0;
        const bool processed = pipeline_.process(
            input,
            std::span<int16_t>(scratch.data(), scratch.size()),
            producedSamples);
        if (!processed) {
            std::fill(output.begin(), output.end(), 0);
            return;
        }

        const auto copyCount = std::min(output.size(), producedSamples);
        if (copyCount > 0u) {
            std::memmove(output.data(), scratch.data(), copyCount * sizeof(int16_t));
        }
        if (copyCount < output.size()) {
            std::fill(output.begin() + static_cast<std::ptrdiff_t>(copyCount), output.end(), 0);
        }
    }

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

    [[nodiscard]] std::size_t nextReadyIndex(std::size_t index) const noexcept
    {
        const auto size = readyBlocks_.size();
        return size == 0u ? 0u : (index + 1u) % size;
    }

    [[nodiscard]] std::size_t readyQueueDepth() const noexcept
    {
        const auto size = readyBlocks_.size();
        if (size == 0u) {
            return 0u;
        }
        const auto readIndex = readyReadIndex_.load(std::memory_order_acquire);
        const auto writeIndex = readyWriteIndex_.load(std::memory_order_acquire);
        if (writeIndex >= readIndex) {
            return writeIndex - readIndex;
        }
        return size - (readIndex - writeIndex);
    }

    void noteReadyQueueHighWater(std::size_t depth) noexcept
    {
        auto previous = transportReadyQueueHighWaterChunks_.load(std::memory_order_relaxed);
        while (depth > previous &&
               !transportReadyQueueHighWaterChunks_.compare_exchange_weak(
                   previous, depth, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    [[nodiscard]] bool readyQueueHasSpace() const noexcept
    {
        if (readyBlocks_.empty()) {
            return false;
        }
        const auto writeIndex = readyWriteIndex_.load(std::memory_order_acquire);
        return nextReadyIndex(writeIndex) != readyReadIndex_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::chrono::milliseconds outputTransportWakePeriod() const noexcept
    {
        const auto channels = std::max<int>(outputTransportConfig_.channelCount, 1);
        const auto sampleRate = std::max(outputTransportConfig_.deviceSampleRate, 1);
        const auto frames = static_cast<double>(outputTransportConfig_.callbackChunkSamples) /
                            static_cast<double>(channels);
        const auto durationMs = static_cast<long long>((frames / static_cast<double>(sampleRate)) * 1000.0);
        return std::chrono::milliseconds(std::max(1LL, durationMs));
    }

    void outputTransportWorkerLoop() noexcept
    {
        while (!outputTransportStopRequested_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(outputTransportWaitMutex_);
            outputTransportCv_.wait_for(lock, outputTransportWakePeriod(), [this]() noexcept {
                return outputTransportStopRequested_.load(std::memory_order_acquire) ||
                       (readyQueueHasSpace() && engine_.bufferedSamples() != 0u);
            });
            lock.unlock();

            if (outputTransportStopRequested_.load(std::memory_order_acquire)) {
                break;
            }

            transportWorkerWakeCount_.fetch_add(1u, std::memory_order_relaxed);
            while (!outputTransportStopRequested_.load(std::memory_order_acquire) &&
                   produceReadyOutputBlock()) {
            }
        }
    }

    void resetTransportStats() noexcept
    {
        transportReadyQueueHighWaterChunks_.store(0u, std::memory_order_relaxed);
        transportDrainCallbackCount_.store(0u, std::memory_order_relaxed);
        transportUnderrunCount_.store(0u, std::memory_order_relaxed);
        transportSilenceSamplesFilled_.store(0u, std::memory_order_relaxed);
        transportWorkerProducedBlocks_.store(0u, std::memory_order_relaxed);
        transportDroppedReadyBlocks_.store(0u, std::memory_order_relaxed);
        transportWorkerWakeCount_.store(0u, std::memory_order_relaxed);
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

    AudioOutputTransportConfig outputTransportConfig_{};
    bool outputTransportConfigured_ = false;
    std::vector<std::vector<int16_t>> readyBlocks_{};
    std::vector<int16_t> producerScratch_{};
    std::vector<int16_t> transportPipelineScratch_{};
    std::atomic<std::size_t> readyReadIndex_{0};
    std::atomic<std::size_t> readyWriteIndex_{0};
    std::atomic<bool> outputTransportRunning_{false};
    std::atomic<bool> outputTransportStopRequested_{false};
    std::thread outputTransportWorker_{};
    std::condition_variable outputTransportCv_{};
    std::mutex outputTransportWaitMutex_{};
    std::atomic<std::size_t> transportReadyQueueHighWaterChunks_{0};
    std::atomic<std::size_t> transportDrainCallbackCount_{0};
    std::atomic<std::size_t> transportUnderrunCount_{0};
    std::atomic<std::size_t> transportSilenceSamplesFilled_{0};
    std::atomic<std::size_t> transportWorkerProducedBlocks_{0};
    std::atomic<std::size_t> transportDroppedReadyBlocks_{0};
    std::atomic<std::size_t> transportWorkerWakeCount_{0};
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_SERVICE_HPP
