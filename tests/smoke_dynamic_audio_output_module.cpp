#include <cassert>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "machine/AudioService.hpp"
#include "machine/plugins/DynamicPluginModule.hpp"

int main(int argc, char** argv)
{
    assert(argc == 3);
    auto module = BMMQ::Plugin::DynamicPluginModule::load(argv[1]);
    assert(module.audioOutputIds() == std::vector<std::string>{"test.audio-output.pure-c"});
    auto output = module.createAudioOutput("test.audio-output.pure-c");
    module = {};
    assert(output->name() == "pure-c-test-audio");

    BMMQ::AudioService service({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .channelCount = 1,
        .ringBufferCapacitySamples = 8192,
        .frameChunkSamples = 128,
    });
    assert(output->open(service.engine(), {
        .backend = "test.audio-output.pure-c",
        .requestedSampleRate = 48000,
        .callbackChunkSamples = 128,
        .readyQueueChunks = 2,
        .channels = 1,
        .filePath = {},
        .audioService = &service,
    }));
    std::vector<std::int16_t> pcm(4096u, 1200);
    service.appendRecentPcm(pcm, 1u);
    for (int attempt = 0; attempt < 100 && service.transportStats().drainCallbackCount == 0u;
         ++attempt) {
        output->service();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(service.transportStats().drainCallbackCount > 0u);
    assert(output->ready());
    const auto drainsBeforeClose = service.transportStats().drainCallbackCount;
    output->close();
    assert(!output->ready());
    assert(service.transportStats().drainCallbackCount == drainsBeforeClose);
    output.reset();

    auto invalidFormat = BMMQ::Plugin::DynamicPluginModule::load(argv[1])
        .createAudioOutput("test.audio-output.pure-c");
    assert(!invalidFormat->open(service.engine(), {
        .backend = "test.audio-output.pure-c",
        .requestedSampleRate = 12345,
        .callbackChunkSamples = 128,
        .readyQueueChunks = 2,
        .channels = 1,
        .filePath = {},
        .audioService = &service,
    }));
    assert(invalidFormat->lastErrorCode() == BMMQ::AudioOutputErrorCode::UnsupportedConfig);

    // Start failure must close the successfully opened module exactly once,
    // retain its error, and permit reopening the same instance.
    auto failedStart = BMMQ::Plugin::DynamicPluginModule::load(argv[1])
        .createAudioOutput("test.audio-output.pure-c");
    auto openConfig = BMMQ::AudioOutputOpenConfig{
        .backend = "test.audio-output.pure-c",
        .requestedSampleRate = 23456,
        .callbackChunkSamples = 128,
        .readyQueueChunks = 2,
        .channels = 1,
        .filePath = {},
        .audioService = &service,
    };
    assert(failedStart->open(service.engine(), openConfig));
    service.appendRecentPcm(pcm, 1u);
    for (int attempt = 0; attempt < 500 && failedStart->ready(); ++attempt) {
        failedStart->service();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(!failedStart->ready());
    assert(failedStart->lastErrorCode() == BMMQ::AudioOutputErrorCode::RuntimeError);
    assert(failedStart->lastError() == "intentional start failure");
    failedStart->service();
    failedStart->close();
    failedStart->close();
    assert(failedStart->lastError() == "intentional start failure");
    openConfig.requestedSampleRate = 48000;
    assert(failedStart->open(service.engine(), openConfig));
    const auto drainsBeforeReopen = service.transportStats().drainCallbackCount;
    service.appendRecentPcm(pcm, 1u);
    for (int attempt = 0; attempt < 500 &&
         service.transportStats().drainCallbackCount == drainsBeforeReopen; ++attempt) {
        failedStart->service();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(failedStart->ready());
    assert(service.transportStats().drainCallbackCount > drainsBeforeReopen);
    failedStart.reset(); // The fixture aborts if destroyed open or closed twice.

    bool malformedRejected = false;
    try { (void)BMMQ::Plugin::DynamicPluginModule::load(argv[2]); }
    catch (const std::runtime_error& ex) {
        malformedRejected = std::string(ex.what()).find("incomplete audio-output API") !=
            std::string::npos;
    }
    assert(malformedRejected);
}
