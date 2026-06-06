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
    SdlAudioResampler(int sourceSampleRate, int outputSampleRate, uint8_t channelCount = 1u)
    {
        configure(sourceSampleRate, outputSampleRate, channelCount);
    }

    void configure(int sourceSampleRate, int outputSampleRate, uint8_t channelCount = 1u)
    {
        sourceSampleRate_ = std::max(sourceSampleRate, 1);
        outputSampleRate_ = std::max(outputSampleRate, 1);
        channelCount_ = std::max<uint8_t>(channelCount, 1u);
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

    [[nodiscard]] uint8_t channelCount() const noexcept
    {
        return channelCount_;
    }

    template <typename PeekSampleFn, typename ConsumeSamplesFn>
    [[nodiscard]] SdlAudioResamplerRenderStats render(std::span<int16_t> output,
                                                      PeekSampleFn&& peekSample,
                                                      ConsumeSamplesFn&& consumeSamples)
    {
        SdlAudioResamplerRenderStats stats;
        const auto channels = static_cast<std::size_t>(std::max<uint8_t>(channelCount_, 1u));
        if (output.size() < channels) {
            return stats;
        }

        const auto outputFrames = output.size() / channels;
        for (std::size_t frame = 0; frame < outputFrames; ++frame) {
            bool frameAvailable = true;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                int16_t currentSample = 0;
                const bool hasCurrentSample = peekSample(channel, currentSample);
                if (!hasCurrentSample) {
                    output[(frame * channels) + channel] = 0;
                    frameAvailable = false;
                    continue;
                }

                int16_t nextSample = currentSample;
                const bool hasNextSample = peekSample(channels + channel, nextSample);
                const auto current = static_cast<double>(currentSample);
                const auto next = static_cast<double>(hasNextSample ? nextSample : currentSample);
                const auto mixed = std::lround(current + ((next - current) * sourcePhase_));
                output[(frame * channels) + channel] = static_cast<int16_t>(std::clamp<long>(mixed, -32768L, 32767L));
            }

            stats.outputSamplesProduced += channels;
            if (!frameAvailable) {
                stats.silenceSamplesFilled += channels;
            }

            sourcePhase_ += step_;
            const auto wholeSteps = static_cast<std::size_t>(sourcePhase_);
            if (wholeSteps != 0u) {
                const auto consumed = consumeSamples(wholeSteps * channels);
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
    uint8_t channelCount_ = 1u;
    double step_ = 1.0;
    double sourcePhase_ = 0.0;
};

} // namespace BMMQ

#endif // BMMQ_SDL_AUDIO_RESAMPLER_HPP
