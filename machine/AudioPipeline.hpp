#ifndef BMMQ_AUDIO_PIPELINE_HPP
#define BMMQ_AUDIO_PIPELINE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace BMMQ {

struct AudioBufferView {
    std::span<const int16_t> samples;
    int sampleRate = 48000;
};

struct AudioProcessorCapabilities {
    bool realtimeSafe = false;
    bool fixedCapacityOutput = false;
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

            current = {std::span<const int16_t>(scratch.data(), stageProduced), current.sampleRate};
            useA = !useA;
        }

        const auto copyCount = std::min(current.samples.size(), output.size());
        if (copyCount != 0u) {
            std::copy_n(current.samples.begin(), static_cast<std::ptrdiff_t>(copyCount), output.begin());
        }
        producedSamples = copyCount;
        return true;
    }

    // Non-real-time helper for tests/offline processing only. This path resizes
    // the caller-owned vector and must not be used from the live audio callback.
    AudioBufferView process(AudioBufferView input, std::vector<int16_t>& output)
    {
        const auto boundedSize = fixedCapacitySamples_ != 0u
            ? std::min(input.samples.size(), fixedCapacitySamples_)
            : input.samples.size();
        output.assign(boundedSize, 0);

        std::size_t producedSamples = 0;
        if (!process(input,
                     std::span<int16_t>(output.data(), output.size()),
                     producedSamples)) {
            output.clear();
            return {std::span<const int16_t>{}, input.sampleRate};
        }

        output.resize(producedSamples);
        return {std::span<const int16_t>(output.data(), output.size()), input.sampleRate};
    }

private:
    std::size_t fixedCapacitySamples_ = 0;
    std::vector<std::unique_ptr<IAudioProcessor>> processors_{};
    std::vector<int16_t> scratchA_{};
    std::vector<int16_t> scratchB_{};
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_PIPELINE_HPP
