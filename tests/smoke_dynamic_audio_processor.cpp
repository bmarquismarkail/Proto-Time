#include <cassert>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "machine/AudioService.hpp"
#include "machine/plugins/DynamicPluginModule.hpp"

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    auto module = BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[1]));
    const auto ids = module.audioProcessorIds();
    assert(ids.size() == 1u && ids.front() == "test.audio-processor.rich");

    BMMQ::AudioService service({.sourceSampleRate = 48000, .deviceSampleRate = 48000,
                                .channelCount = 1u, .ringBufferCapacitySamples = 64u,
                                .frameChunkSamples = 4u});
    assert(service.configureFixedCallbackCapacity(4u));
    assert(service.addProcessor(module.createAudioProcessor(ids.front(), 48000u, 1u, 4u)));

    BMMQ::AudioSourceBlock block;
    block.sampleRate = 48000u;
    block.channelCount = 1u;
    block.frameCounter = 1u;
    block.mixedSamples = {1, 2, 3, 4};
    block.voices = {{0u, BMMQ::PsgVoiceKind::Tone}};
    block.voiceStems = block.mixedSamples;
    block.events = {{.voiceId = 0u, .voiceKind = BMMQ::PsgVoiceKind::Tone,
                     .kind = BMMQ::PsgEventKind::GateOn, .gate = true}};
    assert(service.submitSourceBlock(std::move(block)));
    std::vector<std::int16_t> output(4u, 0);
    service.renderForOutput(output);
    assert((output == std::vector<std::int16_t>{101, 102, 103, 104}));

    auto failingProcessor = module.createAudioProcessor(ids.front(), 48000u, 1u, 4u);
    BMMQ::AudioPipeline failingPipeline;
    failingPipeline.configureFixedCapacity(4u);
    failingPipeline.addProcessor(std::move(failingProcessor));
    const std::vector<std::int16_t> input{1, 2, 3, 4};
    std::vector<std::int16_t> passthrough(4u, 0);
    std::size_t produced = 0u;
    assert(failingPipeline.process({input, 48000, 1u}, passthrough, produced));
    assert(produced == input.size());
    assert(passthrough == input);
    assert(failingPipeline.stats().lastProcessorError == "rich audio metadata required");

    std::fill(passthrough.begin(), passthrough.end(), 0);
    assert(failingPipeline.process({input, 48000, 1u}, passthrough, produced));
    assert(passthrough == input);
    return 0;
}
