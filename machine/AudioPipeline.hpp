#ifndef BMMQ_AUDIO_PIPELINE_HPP
#define BMMQ_AUDIO_PIPELINE_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace BMMQ {

struct AudioBufferView {
    std::span<const int16_t> samples;
    int sampleRate = 48000;
    uint8_t channelCount = 1;
};

enum class PsgVoiceKind : std::uint8_t {
    Tone = 0,
    Pulse = 1,
    Wave = 2,
    Noise = 3,
};

enum class PsgEventKind : std::uint8_t {
    StateSnapshot = 0,
    GateOn = 1,
    GateOff = 2,
    Retrigger = 3,
    PitchChange = 4,
    LevelChange = 5,
    RoutingChange = 6,
    TimbreChange = 7,
    RawWrite = 8,
};

struct PsgVoiceDescriptor {
    std::uint8_t voiceId = 0u;
    PsgVoiceKind kind = PsgVoiceKind::Tone;
};

struct PsgAudioEvent {
    std::uint32_t sampleFrameOffset = 0u;
    std::uint64_t sequence = 0u;
    std::uint8_t voiceId = 0u;
    PsgVoiceKind voiceKind = PsgVoiceKind::Tone;
    PsgEventKind kind = PsgEventKind::StateSnapshot;
    std::uint32_t frequencyMilliHz = 0u;
    std::uint16_t levelQ15 = 0u;
    std::uint8_t routingMask = 0u;
    std::uint8_t timbre = 0u;
    std::uint16_t rawAddress = 0u;
    std::uint8_t rawValue = 0u;
    bool gate = false;
    bool hasRawWrite = false;
};

// Immutable source-rate data handed from the emulation lane to the audio worker.
// Voice stems are voice-major; each voice contains sampleCount interleaved samples.
struct AudioSourceBlock {
    std::uint16_t contractVersion = 1u;
    std::uint32_t sampleRate = 48000u;
    std::uint8_t channelCount = 1u;
    std::uint64_t frameCounter = 0u;
    std::uint64_t firstSampleFrame = 0u;
    std::uint64_t lifecycleEpoch = 1u;
    std::vector<std::int16_t> mixedSamples;
    std::vector<PsgVoiceDescriptor> voices;
    std::vector<std::int16_t> voiceStems;
    std::vector<PsgAudioEvent> events;
};

struct AudioSourceBlockView {
    AudioBufferView mixed{};
    std::span<const PsgVoiceDescriptor> voices;
    std::span<const std::int16_t> voiceStems;
    std::span<const PsgAudioEvent> events;
    std::uint64_t frameCounter = 0u;
    std::uint64_t firstSampleFrame = 0u;
    std::uint64_t lifecycleEpoch = 1u;
};

struct AudioProcessorCapabilities {
    bool realtimeSafe = false;
    bool fixedCapacityOutput = false;
};

struct AudioPipelineStats {
    std::uint64_t sourceProcessCalls = 0u;
    std::uint64_t sourceProcessFailures = 0u;
    std::int64_t sourceProcessDurationLastNs = 0;
    std::int64_t sourceProcessDurationHighWaterNs = 0;
};

class IAudioProcessor {
public:
    virtual ~IAudioProcessor() = default;
    [[nodiscard]] virtual AudioProcessorCapabilities capabilities() const noexcept
    {
        return {};
    }

    // Process `input` into the caller-provided fixed-capacity `output` buffer.
    // Returns false when the processor cannot produce output within capacity.
    virtual bool process(AudioBufferView input,
                         std::span<int16_t> output,
                         std::size_t& producedSamples) noexcept = 0;

    // Rich PSG-aware entrypoint. Existing PCM-only processors retain their
    // behavior through this default adapter.
    virtual bool processSource(const AudioSourceBlockView& input,
                               std::span<int16_t> output,
                               std::size_t& producedSamples) noexcept
    {
        return process(input.mixed, output, producedSamples);
    }

    virtual void flush(std::uint64_t) noexcept {}
};

class AudioPipeline {
public:
    void configureFixedCapacity(std::size_t maxSamples)
    {
        fixedCapacitySamples_ = maxSamples;
        scratchA_.assign(fixedCapacitySamples_, 0);
        scratchB_.assign(fixedCapacitySamples_, 0);
    }

    void addProcessor(std::unique_ptr<IAudioProcessor> processor)
    {
        if (processor) {
            processors_.push_back(std::move(processor));
        }
    }

    void clearProcessors()
    {
        processors_.clear();
        std::fill(scratchA_.begin(), scratchA_.end(), 0);
        std::fill(scratchB_.begin(), scratchB_.end(), 0);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return processors_.empty();
    }

    [[nodiscard]] bool process(AudioBufferView input,
                               std::span<int16_t> output,
                               std::size_t& producedSamples) noexcept
    {
        producedSamples = 0;

        if (processors_.empty()) {
            const auto copyCount = std::min(input.samples.size(), output.size());
            if (copyCount != 0u) {
                std::copy_n(input.samples.begin(), static_cast<std::ptrdiff_t>(copyCount), output.begin());
            }
            producedSamples = copyCount;
            return true;
        }

        if (fixedCapacitySamples_ == 0u || output.size() < fixedCapacitySamples_) {
            return false;
        }

        AudioBufferView current = input;
        bool useA = true;
        for (auto& processor : processors_) {
            auto& scratch = useA ? scratchA_ : scratchB_;
            if (scratch.empty()) {
                return false;
            }

            std::size_t stageProduced = 0;
            if (!processor->process(current, std::span<int16_t>(scratch.data(), scratch.size()), stageProduced)) {
                return false;
            }
            if (stageProduced > scratch.size()) {
                return false;
            }

            current = {std::span<const int16_t>(scratch.data(), stageProduced), current.sampleRate, current.channelCount};
            useA = !useA;
        }

        const auto copyCount = std::min(current.samples.size(), output.size());
        if (copyCount != 0u) {
            std::copy_n(current.samples.begin(), static_cast<std::ptrdiff_t>(copyCount), output.begin());
        }
        producedSamples = copyCount;
        return true;
    }

    [[nodiscard]] bool processSource(const AudioSourceBlockView& input,
                                     std::span<int16_t> output,
                                     std::size_t& producedSamples) noexcept
    {
        const auto started = std::chrono::steady_clock::now();
        sourceProcessCalls_.fetch_add(1u, std::memory_order_relaxed);
        producedSamples = 0u;
        if (processors_.empty()) {
            const auto count = std::min(input.mixed.samples.size(), output.size());
            std::copy_n(input.mixed.samples.begin(), static_cast<std::ptrdiff_t>(count), output.begin());
            producedSamples = count;
            return true;
        }
        if (fixedCapacitySamples_ == 0u ||
            input.mixed.samples.size() > fixedCapacitySamples_ ||
            output.size() < input.mixed.samples.size()) {
            sourceProcessFailures_.fetch_add(1u, std::memory_order_relaxed);
            noteSourceDuration(started);
            return false;
        }

        AudioSourceBlockView current = input;
        bool useA = true;
        for (auto& processor : processors_) {
            auto& scratch = useA ? scratchA_ : scratchB_;
            std::size_t stageProduced = 0u;
            if (!processor->processSource(current, scratch, stageProduced) ||
                stageProduced != input.mixed.samples.size()) {
                sourceProcessFailures_.fetch_add(1u, std::memory_order_relaxed);
                noteSourceDuration(started);
                return false;
            }
            current.mixed.samples = std::span<const int16_t>(scratch.data(), stageProduced);
            useA = !useA;
        }
        std::copy_n(current.mixed.samples.begin(), static_cast<std::ptrdiff_t>(current.mixed.samples.size()), output.begin());
        producedSamples = current.mixed.samples.size();
        noteSourceDuration(started);
        return true;
    }

    [[nodiscard]] AudioPipelineStats stats() const noexcept
    {
        return {sourceProcessCalls_.load(std::memory_order_relaxed),
                sourceProcessFailures_.load(std::memory_order_relaxed),
                sourceProcessDurationLastNs_.load(std::memory_order_relaxed),
                sourceProcessDurationHighWaterNs_.load(std::memory_order_relaxed)};
    }

    void flush(std::uint64_t lifecycleEpoch) noexcept
    {
        for (auto& processor : processors_) processor->flush(lifecycleEpoch);
    }

    // Non-real-time helper for tests/offline processing only. This path resizes
    // the caller-owned vector and must not be used from the live audio callback.
    AudioBufferView process(AudioBufferView input, std::vector<int16_t>& output)
    {
        if (fixedCapacitySamples_ == 0u && !processors_.empty()) {
            output.clear();
            return {std::span<const int16_t>{}, input.sampleRate, input.channelCount};
        }

        const auto boundedSize = fixedCapacitySamples_ != 0u
            ? std::min(input.samples.size(), fixedCapacitySamples_)
            : input.samples.size();
        output.assign(boundedSize, 0);

        std::size_t producedSamples = 0;
        if (!process(input,
                     std::span<int16_t>(output.data(), output.size()),
                     producedSamples)) {
            output.clear();
            return {std::span<const int16_t>{}, input.sampleRate, input.channelCount};
        }

        output.resize(producedSamples);
        return {std::span<const int16_t>(output.data(), output.size()), input.sampleRate, input.channelCount};
    }

private:
    void noteSourceDuration(std::chrono::steady_clock::time_point started) noexcept
    {
        const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        sourceProcessDurationLastNs_.store(nanos, std::memory_order_relaxed);
        auto high = sourceProcessDurationHighWaterNs_.load(std::memory_order_relaxed);
        while (nanos > high && !sourceProcessDurationHighWaterNs_.compare_exchange_weak(
                   high, nanos, std::memory_order_relaxed)) {}
    }
    std::size_t fixedCapacitySamples_ = 0;
    std::vector<std::unique_ptr<IAudioProcessor>> processors_{};
    std::vector<int16_t> scratchA_{};
    std::vector<int16_t> scratchB_{};
    std::atomic<std::uint64_t> sourceProcessCalls_{0u};
    std::atomic<std::uint64_t> sourceProcessFailures_{0u};
    std::atomic<std::int64_t> sourceProcessDurationLastNs_{0};
    std::atomic<std::int64_t> sourceProcessDurationHighWaterNs_{0};
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_PIPELINE_HPP
