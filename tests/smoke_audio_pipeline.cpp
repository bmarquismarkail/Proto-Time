#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

#include "machine/AudioPipeline.hpp"

namespace {

class GainProcessor final : public BMMQ::IAudioProcessor {
public:
    explicit GainProcessor(float gain)
        : gain_(gain) {}

    void process(BMMQ::AudioBufferView input,
                 std::vector<int16_t>& output) override
    {
        output.resize(input.samples.size());
        for (std::size_t i = 0; i < input.samples.size(); ++i) {
            const auto value = static_cast<float>(input.samples[i]) * gain_;
            const auto clamped = std::max(-32768.0f, std::min(32767.0f, value));
            output[i] = static_cast<int16_t>(clamped);
        }
    }

private:
    float gain_ = 1.0f;
};

class DoubleLengthProcessor final : public BMMQ::IAudioProcessor {
public:
    void process(BMMQ::AudioBufferView input,
                 std::vector<int16_t>& output) override
    {
        output.resize(input.samples.size() * 2);
        for (std::size_t i = 0; i < input.samples.size(); ++i) {
            output[i * 2] = input.samples[i];
            output[i * 2 + 1] = input.samples[i];
        }
    }
};

class HalfLengthProcessor final : public BMMQ::IAudioProcessor {
public:
    void process(BMMQ::AudioBufferView input,
                 std::vector<int16_t>& output) override
    {
        const auto outCount = input.samples.size() / 2;
        output.resize(outCount);
        for (std::size_t i = 0; i < outCount; ++i) {
            output[i] = input.samples[i * 2];
        }
    }
};

} // namespace

int main()
{
    BMMQ::AudioPipeline pipeline;

    std::vector<int16_t> baseSamples = {100, -100, 200, -200};
    BMMQ::AudioBufferView baseView{std::span<const int16_t>(baseSamples.data(), baseSamples.size()), 48000};
    std::vector<int16_t> processedSamples;

    const auto passthrough = pipeline.process(baseView, processedSamples);
    assert(passthrough.samples.size() == baseSamples.size());
    assert(passthrough.samples[0] == 100);

    pipeline.addProcessor(std::make_unique<GainProcessor>(0.5f));
    const auto gained = pipeline.process(baseView, processedSamples);
    assert(gained.samples.size() == baseSamples.size());
    assert(gained.samples[0] == 50);

    pipeline.clearProcessors();
    pipeline.addProcessor(std::make_unique<DoubleLengthProcessor>());
    const auto doubled = pipeline.process(baseView, processedSamples);
    assert(doubled.samples.size() == baseSamples.size() * 2);

    pipeline.clearProcessors();
    pipeline.addProcessor(std::make_unique<HalfLengthProcessor>());
    const auto halved = pipeline.process(baseView, processedSamples);
    assert(halved.samples.size() == baseSamples.size() / 2);
    assert(halved.samples[0] == 100);

    (void)passthrough;
    (void)gained;
    (void)doubled;
    (void)halved;

    return 0;
}
