#include <cassert>
#include <string>

#include "emulator/AudioKpiStatus.hpp"

int main()
{
    using BMMQ::AudioKpiInputs;
    using BMMQ::AudioKpiStatus;
    using BMMQ::classifyAudioKpiStatus;
    using BMMQ::audioKpiStatusName;

    {
        const auto result = classifyAudioKpiStatus(AudioKpiInputs{
            .elapsedSeconds = 10.0,
            .generatedSamples = 864000u,
            .appendedSamples = 0u,
            .expectedSamplesPerSecond = 96000.0,
            .drainedReadySamples = 864500u,
            .drainRequestedSamples = 960000u,
        });
        assert(result.status == AudioKpiStatus::SourceLimited);
        assert(result.sourceRatio > 0.89 && result.sourceRatio < 0.91);
        assert(result.drainRatio > 0.89 && result.drainRatio < 0.91);
        assert(std::string(audioKpiStatusName(result.status)) == "source_limited");
    }

    {
        const auto result = classifyAudioKpiStatus(AudioKpiInputs{
            .elapsedSeconds = 10.0,
            .generatedSamples = 960000u,
            .appendedSamples = 0u,
            .expectedSamplesPerSecond = 96000.0,
            .drainedReadySamples = 800000u,
            .drainRequestedSamples = 960000u,
        });
        assert(result.status == AudioKpiStatus::TransportLimited);
        assert(std::string(audioKpiStatusName(result.status)) == "transport_limited");
    }

    {
        const auto result = classifyAudioKpiStatus(AudioKpiInputs{
            .elapsedSeconds = 10.0,
            .generatedSamples = 970000u,
            .appendedSamples = 0u,
            .expectedSamplesPerSecond = 96000.0,
            .drainedReadySamples = 950000u,
            .drainRequestedSamples = 960000u,
        });
        assert(result.status == AudioKpiStatus::Healthy);
        assert(std::string(audioKpiStatusName(result.status)) == "healthy");
    }

    {
        const auto result = classifyAudioKpiStatus(AudioKpiInputs{
            .elapsedSeconds = 0.0,
            .generatedSamples = 0u,
            .appendedSamples = 0u,
            .expectedSamplesPerSecond = 96000.0,
            .drainedReadySamples = 0u,
            .drainRequestedSamples = 0u,
        });
        assert(result.status == AudioKpiStatus::Unknown);
        assert(std::string(audioKpiStatusName(result.status)) == "unknown");
    }

    {
        const auto result = classifyAudioKpiStatus(AudioKpiInputs{
            .elapsedSeconds = 10.0,
            .generatedSamples = 0u,
            .appendedSamples = 864000u,
            .expectedSamplesPerSecond = 96000.0,
            .drainedReadySamples = 864000u,
            .drainRequestedSamples = 960000u,
        });
        assert(result.status == AudioKpiStatus::SourceLimited);
    }

    return 0;
}
