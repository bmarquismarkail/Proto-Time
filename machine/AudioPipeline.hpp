#ifndef BMMQ_AUDIO_PIPELINE_HPP
#define BMMQ_AUDIO_PIPELINE_HPP

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace BMMQ {

struct AudioBufferView {
    std::span<const int16_t> samples;
    int sampleRate = 48000;
};

class IAudioProcessor {
public:
    virtual ~IAudioProcessor() = default;
    virtual void process(AudioBufferView input,
                         std::vector<int16_t>& output) = 0;
};

class AudioPipeline {
public:
    void addProcessor(std::unique_ptr<IAudioProcessor> processor)
    {
        if (processor) {
            processors_.push_back(std::move(processor));
        }
    }

    void clearProcessors()
    {
        processors_.clear();
        scratchA_.clear();
        scratchB_.clear();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return processors_.empty();
    }

    // Returned samples view points to caller-owned `output` storage.
    AudioBufferView process(AudioBufferView input, std::vector<int16_t>& output)
    {
        if (processors_.empty()) {
            output.assign(input.samples.begin(), input.samples.end());
            return {std::span<const int16_t>(output.data(), output.size()), input.sampleRate};
        }

        AudioBufferView current = input;
        bool useA = true;
        for (auto& processor : processors_) {
            auto& scratch = useA ? scratchA_ : scratchB_;
            scratch.reserve(current.samples.size());
            scratch.clear();
            processor->process(current, scratch);
            current = {std::span<const int16_t>(scratch.data(), scratch.size()), current.sampleRate};
            useA = !useA;
        }
        output.assign(current.samples.begin(), current.samples.end());
        return {std::span<const int16_t>(output.data(), output.size()), current.sampleRate};
    }

private:
    std::vector<std::unique_ptr<IAudioProcessor>> processors_{};
    std::vector<int16_t> scratchA_{};
    std::vector<int16_t> scratchB_{};
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_PIPELINE_HPP
