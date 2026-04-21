#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
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

void stepUntilAudioFrames(GameBoyMachine& machine, uint64_t targetFrameCounter)
{
    for (int i = 0; i < 60000; ++i) {
        machine.step();
        if (machine.audioFrameCounter() >= targetFrameCounter) {
            return;
        }
    }
    assert(false && "audio frame counter did not reach target");
}

} // namespace

int main(int argc, char** argv)
{
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif

    BMMQ::SdlFrontendConfig config;
    config.enableVideo = false;
    config.enableInput = false;
    config.autoInitializeBackend = false;
    config.audioPreviewSampleCount = 64;
    config.enableAudioResamplingDiagnostics = true;
    config.testForcedAudioDeviceSampleRate = 44100;

    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    machine.loadRom(cartridgeRom);
    primeAudioRegisters(machine);

    const auto executablePath = (argc > 0 && argv != nullptr)
        ? std::filesystem::path(argv[0])
        : std::filesystem::path("time-smoke-sdl-audio-resampling-integration");
    auto frontendPlugin = BMMQ::loadSdlFrontendPlugin(
        BMMQ::defaultSdlFrontendPluginPath(executablePath),
        config);
    auto* frontend = frontendPlugin.get();
    machine.pluginManager().add(std::move(frontendPlugin));
    machine.pluginManager().initialize(machine.mutableView());

    assert(frontend->tryInitializeBackend());
    assert(frontend->audioOutputReady());
    assert(frontend->stats().audioSourceSampleRate == 48000);
    assert(frontend->stats().audioDeviceSampleRate == 44100);
    assert(frontend->stats().audioResamplingActive);
    assert(frontend->stats().audioResampleRatio > 0.91);
    assert(frontend->stats().audioResampleRatio < 0.93);

    stepUntilAudioFrames(machine, 4u);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    assert(frontend->stats().audioCallbackCount >= 1u);
    assert(frontend->stats().audioResampleOutputSamplesProduced >= 1u);
    assert(frontend->stats().audioResampleSourceSamplesConsumed >= 1u);
    assert(frontend->stats().audioSamplesDelivered >= 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stepUntilAudioFrames(machine, 8u);
    assert(frontend->stats().audioResampleOutputSamplesProduced >= frontend->stats().audioSamplesDelivered);

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}
