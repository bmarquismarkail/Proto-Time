#ifndef BMMQ_SDL_AUDIO_RESAMPLER_HPP
#define BMMQ_SDL_AUDIO_RESAMPLER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace BMMQ {

struct SdlAudioResamplerRenderStats {
    std::size_t sourceSamplesConsumed = 0;
    std::size_t outputSamplesProduced = 0;
    std::size_t silenceSamplesFilled = 0;
};

class SdlAudioResampler {
public:
    SdlAudioResampler(int sourceSampleRate, int outputSampleRate)
    {
        configure(sourceSampleRate, outputSampleRate);
    }

    void configure(int sourceSampleRate, int outputSampleRate)
    {
        sourceSampleRate_ = std::max(sourceSampleRate, 1);
        outputSampleRate_ = std::max(outputSampleRate, 1);
        step_ = static_cast<double>(sourceSampleRate_) / static_cast<double>(outputSampleRate_);
        reset();
    }

    void reset() noexcept
    {
        sourcePhase_ = 0.0;
    }

    [[nodiscard]] int sourceSampleRate() const noexcept
    {
        return sourceSampleRate_;
    }

    [[nodiscard]] int outputSampleRate() const noexcept
    {
        return outputSampleRate_;
    }

    [[nodiscard]] double ratio() const noexcept
    {
        return static_cast<double>(outputSampleRate_) / static_cast<double>(sourceSampleRate_);
    }

    template <typename PeekSampleFn, typename ConsumeSamplesFn>
    [[nodiscard]] SdlAudioResamplerRenderStats render(std::span<int16_t> output,
                                                      PeekSampleFn&& peekSample,
                                                      ConsumeSamplesFn&& consumeSamples)
    {
        SdlAudioResamplerRenderStats stats;

        for (std::size_t i = 0; i < output.size(); ++i) {
            int16_t leftSample = 0;
            const bool hasLeftSample = peekSample(0u, leftSample);
            if (!hasLeftSample) {
                output[i] = 0;
                ++stats.silenceSamplesFilled;
            } else {
                int16_t rightSample = leftSample;
                const bool hasRightSample = peekSample(1u, rightSample);
                if (!hasRightSample) {
                    output[i] = leftSample;
                } else {
                    const auto left = static_cast<double>(leftSample);
                    const auto right = static_cast<double>(rightSample);
                    const auto mixed = std::lround(left + ((right - left) * sourcePhase_));
                    output[i] = static_cast<int16_t>(std::clamp<long>(mixed, -32768L, 32767L));
                }
            }

            ++stats.outputSamplesProduced;

            sourcePhase_ += step_;
            const auto wholeSteps = static_cast<std::size_t>(sourcePhase_);
            if (wholeSteps != 0u) {
                const auto consumed = consumeSamples(wholeSteps);
                stats.sourceSamplesConsumed += consumed;
                sourcePhase_ -= static_cast<double>(wholeSteps);
            }
        }

        return stats;
    }

    [[nodiscard]] SdlAudioResamplerRenderStats render(std::span<const int16_t> input,
                                                      std::span<int16_t> output)
    {
        std::size_t baseIndex = 0u;
        return render(output,
                      [&input, &baseIndex](std::size_t offset, int16_t& sample) {
                          const auto index = baseIndex + offset;
                          if (index >= input.size()) {
                              return false;
                          }
                          sample = input[index];
                          return true;
                      },
                      [&input, &baseIndex](std::size_t requested) {
                          const auto available = input.size() > baseIndex ? input.size() - baseIndex : 0u;
                          const auto consumed = std::min(requested, available);
                          baseIndex += consumed;
                          return consumed;
                      });
    }

private:
    int sourceSampleRate_ = 48000;
    int outputSampleRate_ = 48000;
    double step_ = 1.0;
    double sourcePhase_ = 0.0;
};

} // namespace BMMQ

#endif // BMMQ_SDL_AUDIO_RESAMPLER_HPP
