#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/SdlFrontendPlugin.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"

namespace {

void primeAudioRegisters(GameBoyMachine& machine)
{
    machine.runtimeContext().write8(0xFF26, 0x80u);
    machine.runtimeContext().write8(0xFF24, 0x77u);
    machine.runtimeContext().write8(0xFF25, 0xFFu);
    machine.runtimeContext().write8(0xFF10, 0x16u);
    machine.runtimeContext().write8(0xFF11, 0x80u);
    machine.runtimeContext().write8(0xFF12, 0xF3u);
    machine.runtimeContext().write8(0xFF13, 0x70u);
    machine.runtimeContext().write8(0xFF14, 0x87u);
}

bool stepUntilAudioFrames(GameBoyMachine& machine, uint64_t targetFrameCounter)
{
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.audioFrameCounter() >= targetFrameCounter) {
            return true;
        }
    }
    return false;
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate, std::chrono::milliseconds timeout, std::chrono::milliseconds pollInterval)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(pollInterval);
    }
    return predicate();
}

} // namespace

int main(int argc, char** argv)
{
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#elif defined(_WIN32)
    _putenv_s("SDL_AUDIODRIVER", "dummy");
    _putenv_s("SDL_VIDEODRIVER", "dummy");
#endif

    BMMQ::SdlFrontendConfig config;
    config.enableVideo = false;
    config.enableInput = false;
    config.autoInitializeBackend = false;
    config.audioPreviewSampleCount = 64;

    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    machine.loadRom(cartridgeRom);
    primeAudioRegisters(machine);

    const auto executablePath = (argc > 0 && argv != nullptr)
        ? std::filesystem::path(argv[0])
        : std::filesystem::path("smoke-sdl-audio-transport");
    auto frontendPlugin = BMMQ::loadSdlFrontendPlugin(
        BMMQ::defaultSdlFrontendPluginPath(executablePath),
        config);
    auto* frontend = frontendPlugin.get();
    machine.pluginManager().add(std::move(frontendPlugin));
    machine.pluginManager().initialize(machine.mutableView());

    const bool initResult = frontend->tryInitializeBackend();
    if (initResult && frontend->audioOutputReady()) {
        assert(frontend->stats().audioSourceSampleRate == 48000);
        assert(frontend->stats().audioDeviceSampleRate == 48000);
        assert(frontend->stats().audioCallbackChunkSamples == 256u);
        assert(frontend->stats().audioRingBufferCapacitySamples == 2048u);
        assert(!frontend->stats().audioResamplingActive);
        assert(frontend->stats().audioResampleRatio == 1.0);
        assert(frontend->stats().audioPipelineCapacitySkipCount == 0u);

        if (!stepUntilAudioFrames(machine, 2u)) {
            std::cerr << "smoke_sdl_audio_transport: audio frame counter did not reach 2" << '\n';
            machine.pluginManager().shutdown(machine.mutableView());
            return 1;
        }
        assert(frontend->stats().audioBufferedHighWaterSamples >= 256u);
        const bool primed = waitUntil([frontend]() {
            const auto stats = frontend->stats();
            return stats.audioTransportPrimedForDrain ||
                   stats.audioTransportWorkerProducedBlocks > 0u ||
                   frontend->bufferedAudioSamples() > 0u;
        }, std::chrono::milliseconds(150), std::chrono::milliseconds(5));
        assert(primed);

        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        const auto bufferedAfterDrain = frontend->bufferedAudioSamples();
        (void)bufferedAfterDrain;
        assert(bufferedAfterDrain <= frontend->stats().audioRingBufferCapacitySamples);
        assert(frontend->stats().audioCallbackCount >= 1u);
        assert(frontend->stats().audioSamplesDelivered >= 256u);
        assert(frontend->stats().audioResampleOutputSamplesProduced >= frontend->stats().audioSamplesDelivered);
        assert(frontend->stats().audioResampleSourceSamplesConsumed >= 1u);

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        assert(frontend->stats().audioTransportUnderrunCount >= 1u);
        assert(frontend->stats().audioTransportSilenceSamplesFilled >= 1u);

        if (!stepUntilAudioFrames(machine, 20u)) {
            std::cerr << "smoke_sdl_audio_transport: audio frame counter did not reach 20" << '\n';
            machine.pluginManager().shutdown(machine.mutableView());
            return 1;
        }
        assert(frontend->bufferedAudioSamples() <= frontend->stats().audioRingBufferCapacitySamples);
        assert(frontend->stats().audioDroppedSamples >= frontend->stats().audioOverrunDropCount);
        if (frontend->stats().audioOverrunDropCount > 0u) {
            assert(frontend->stats().audioDroppedSamples > 0u);
        }
    }

    machine.pluginManager().shutdown(machine.mutableView());

    // Torture lifecycle: repeatedly re-attach/re-detach frontend and reopen audio backend.
    constexpr int kLifecycleCycles = 25;
    for (int cycle = 0; cycle < kLifecycleCycles; ++cycle) {
        machine.pluginManager().initialize(machine.mutableView());
        const bool cycleInitResult = frontend->tryInitializeBackend();
        if (cycleInitResult && frontend->audioOutputReady()) {
            const auto startFrameCounter = machine.audioFrameCounter();
            if (!stepUntilAudioFrames(machine, startFrameCounter + 1u)) {
                std::cerr << "smoke_sdl_audio_transport: churn cycle frame advance failed at cycle "
                          << cycle << '\n';
                machine.pluginManager().shutdown(machine.mutableView());
                return 1;
            }
            assert(frontend->bufferedAudioSamples() <= frontend->stats().audioRingBufferCapacitySamples);
        }
        machine.pluginManager().shutdown(machine.mutableView());
    }

    // Stress tiny callback chunks with forced sample-rate mismatch.
    BMMQ::SdlFrontendConfig resampleStressConfig;
    resampleStressConfig.enableVideo = false;
    resampleStressConfig.enableInput = false;
    resampleStressConfig.autoInitializeBackend = false;
    resampleStressConfig.audioPreviewSampleCount = 64;
    resampleStressConfig.audioCallbackChunkSamples = 32;
    resampleStressConfig.audioRingBufferCapacitySamples = 512;
    resampleStressConfig.enableAudioResamplingDiagnostics = true;
    resampleStressConfig.testForcedAudioDeviceSampleRate = 44100;

    GameBoyMachine resampleStressMachine;
    resampleStressMachine.loadRom(cartridgeRom);
    primeAudioRegisters(resampleStressMachine);

    auto resampleStressPlugin = BMMQ::loadSdlFrontendPlugin(
        BMMQ::defaultSdlFrontendPluginPath(executablePath),
        resampleStressConfig);
    auto* resampleStressFrontend = resampleStressPlugin.get();
    resampleStressMachine.pluginManager().add(std::move(resampleStressPlugin));

    constexpr int kResampleCycles = 12;
    for (int cycle = 0; cycle < kResampleCycles; ++cycle) {
        resampleStressMachine.pluginManager().initialize(resampleStressMachine.mutableView());
        const bool cycleInitResult = resampleStressFrontend->tryInitializeBackend();
        if (cycleInitResult && resampleStressFrontend->audioOutputReady()) {
            assert(resampleStressFrontend->stats().audioDeviceSampleRate == 44100);
            assert(resampleStressFrontend->stats().audioResamplingActive);
            assert(resampleStressFrontend->stats().audioPipelineCapacitySkipCount == 0u);

            const auto startFrameCounter = resampleStressMachine.audioFrameCounter();
            if (!stepUntilAudioFrames(resampleStressMachine, startFrameCounter + 1u)) {
                std::cerr << "smoke_sdl_audio_transport: resample churn frame advance failed at cycle "
                          << cycle << '\n';
                resampleStressMachine.pluginManager().shutdown(resampleStressMachine.mutableView());
                return 1;
            }
            assert(resampleStressFrontend->bufferedAudioSamples() <= resampleStressFrontend->stats().audioRingBufferCapacitySamples);
        }
        resampleStressMachine.pluginManager().shutdown(resampleStressMachine.mutableView());
    }

    assert(frontend->stats().attachCount >= static_cast<std::size_t>(kLifecycleCycles + 1));
    assert(frontend->stats().detachCount >= static_cast<std::size_t>(kLifecycleCycles + 1));
    assert(resampleStressFrontend->stats().attachCount >= static_cast<std::size_t>(kResampleCycles));
    assert(resampleStressFrontend->stats().detachCount >= static_cast<std::size_t>(kResampleCycles));

    return 0;
}
