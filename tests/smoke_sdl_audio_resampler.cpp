#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include "machine/plugins/SdlAudioResampler.hpp"

int main()
{
    {
        BMMQ::SdlAudioResampler resampler(48000, 48000);
        const std::vector<int16_t> input{100, 200, 300, 400};
        std::vector<int16_t> output(4, 0);
        const auto stats = resampler.render(input, output);
        (void)stats;
        assert(stats.outputSamplesProduced == 4u);
        assert(stats.sourceSamplesConsumed >= 3u);
        assert(output[0] == 100);
        assert(output[1] == 200);
        assert(output[2] == 300);
        assert(output[3] == 400);
    }

    {
        BMMQ::SdlAudioResampler resampler(48000, 24000);
        const std::vector<int16_t> input{0, 1000, 2000, 3000, 4000, 5000};
        std::vector<int16_t> output(3, 0);
        const auto stats = resampler.render(input, output);
        (void)stats;
        assert(stats.outputSamplesProduced == 3u);
        assert(stats.sourceSamplesConsumed > 0u);
        assert(output[0] == 0);
        assert(output[1] == 2000);
        assert(output[2] == 4000);
    }

    {
        BMMQ::SdlAudioResampler resampler(48000, 24000, 2u);
        const std::vector<int16_t> input{
            10, -10,
            20, -20,
            30, -30,
            40, -40,
        };
        std::vector<int16_t> output(4, 0);
        const auto stats = resampler.render(input, output);
        (void)stats;
        assert(stats.outputSamplesProduced == 4u);
        assert(stats.sourceSamplesConsumed > 0u);
        assert(output[0] == 10);
        assert(output[1] == -10);
        assert(output[2] == 30);
        assert(output[3] == -30);
    }

    {
        BMMQ::SdlAudioResampler resampler(48000, 96000);
        const std::vector<int16_t> input{0, 1000, 2000, 3000};
        std::vector<int16_t> output(6, 0);
        const auto stats = resampler.render(input, output);
        (void)stats;
        assert(stats.outputSamplesProduced == 6u);
        assert(stats.sourceSamplesConsumed >= 2u);
        assert(output[0] == 0);
        assert(output[1] == 500);
        assert(output[2] == 1000);
        assert(output[3] == 1500);
        assert(output[4] == 2000);
        assert(output[5] == 2500);
    }

    {
        BMMQ::SdlAudioResampler resampler(48000, 96000);
        const std::vector<int16_t> input{1200};
        std::vector<int16_t> output(4, -1);
        const auto stats = resampler.render(input, output);
        (void)stats;
        assert(stats.outputSamplesProduced == 4u);
        assert(stats.silenceSamplesFilled >= 1u);
        assert(output[0] == input.front());
        assert(output[1] == input.front());
    }

    {
        BMMQ::SdlAudioResampler resampler(48000, 96000);
        const std::vector<int16_t> input{0, 1000, 2000, 3000};
        std::vector<int16_t> output(3, 0);
        (void)resampler.render(input, output);
        resampler.reset();
        std::fill(output.begin(), output.end(), 0);
        const auto stats = resampler.render(input, output);
        (void)stats;
        assert(stats.outputSamplesProduced == 3u);
        assert(output[0] == 0);
        assert(output[1] == 500);
        assert(output[2] == 1000);
    }

    {
        BMMQ::SdlAudioResampler resampler(48000, 96000);
        const std::vector<int16_t> sparseInput{1200};
        std::vector<int16_t> sparseOutput(4, 0);
        const auto sparseStats = resampler.render(sparseInput, sparseOutput);
        (void)sparseStats;
        assert(sparseStats.silenceSamplesFilled >= 1u);

        const std::vector<int16_t> resumedInput{0, 1000, 2000, 3000};
        std::vector<int16_t> resumedOutput(4, 0);
        const auto resumedStats = resampler.render(resumedInput, resumedOutput);
        (void)resumedStats;
        assert(resumedOutput[0] == 0);
        assert(resumedOutput[1] == 500);
        assert(resumedOutput[2] == 1000);
        assert(resumedOutput[3] == 1500);
    }

    return 0;
}
