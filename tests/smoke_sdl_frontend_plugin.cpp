#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/SdlFrontendPlugin.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"

namespace {

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

} // namespace

int main(int argc, char** argv)
{
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif

    BMMQ::SdlFrontendConfig config;
    config.windowTitle = "Proto-Time SDL Smoke";
    config.windowScale = 3;
    config.frameWidth = 32;
    config.frameHeight = 24;
    config.audioPreviewSampleCount = 64;
    config.autoInitializeBackend = false;
    config.autoPresentOnVideoEvent = false;

    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    cartridgeRom[0x0100] = 0x00;
    machine.loadRom(cartridgeRom);

    const auto executablePath = (argc > 0 && argv != nullptr)
        ? std::filesystem::path(argv[0])
        : std::filesystem::path("time-smoke-sdl-frontend-plugin");
    auto frontendPlugin = BMMQ::loadSdlFrontendPlugin(
        BMMQ::defaultSdlFrontendPluginPath(executablePath),
        config);
    auto* frontend = frontendPlugin.get();
    machine.pluginManager().add(std::move(frontendPlugin));

    bool missingLoadThrew = false;
    try {
        (void)BMMQ::loadSdlFrontendPlugin(
            executablePath.parent_path() / "missing-time-sdl-frontend-plugin.so",
            config);
    } catch (const std::runtime_error&) {
        missingLoadThrew = true;
    }
    (void)missingLoadThrew;
    assert(missingLoadThrew);

    machine.pluginManager().initialize(machine.mutableView());
    assert(frontend->config().windowTitle == "Proto-Time SDL Smoke");
    assert(frontend->stats().attachCount == 1);
    assert(!frontend->backendReady());
    assert(!frontend->backendName().empty());
    const bool initResult = frontend->tryInitializeBackend();
    assert(frontend->stats().backendInitAttempts >= 1);
    assert(!frontend->backendStatusSummary().empty());
    if (initResult) {
        assert(frontend->backendReady());
    }
    assert(frontend->stats().eventPumpCalls == 0);
    assert(!frontend->windowVisible());
    assert(!frontend->windowVisibilityRequested());
    frontend->requestWindowVisibility(true);
    assert(frontend->windowVisibilityRequested());

    frontend->pressButton(BMMQ::InputButton::Right);
    frontend->pressButton(BMMQ::InputButton::Up);
    frontend->pressButton(BMMQ::InputButton::Button1);
    assert(frontend->queuedDigitalInputMask().has_value());
    assert(*frontend->queuedDigitalInputMask() == 0x15u);
    assert(frontend->isButtonPressed(BMMQ::InputButton::Button1));

    GameBoyMachine dmaVisibilityMachine;
    dmaVisibilityMachine.loadRom(cartridgeRom);
    dmaVisibilityMachine.runtimeContext().write8(0xFF40, 0x91u);
    dmaVisibilityMachine.runtimeContext().write8(0x8002, 0x12u);
    dmaVisibilityMachine.runtimeContext().write8(0x9801, 0x34u);
    dmaVisibilityMachine.runtimeContext().write8(0xFF46, 0xC0u);
    assert(dmaVisibilityMachine.runtimeContext().read8(0x8002) == 0xFFu);
    const auto dmaVisibleState = dmaVisibilityMachine.view().videoState();
    assert(dmaVisibleState.has_value());
    assert(dmaVisibleState->lcdc == 0x91u);
    assert(dmaVisibleState->vram[2] == 0x12u);
    assert(dmaVisibleState->vram[0x1801] == 0x34u);

    machine.runtimeContext().write8(0xFF40, 0x00u);
    machine.runtimeContext().write8(0x8000, 0xFFu);
    machine.runtimeContext().write8(0x8001, 0x00u);
    for (uint16_t row = 1; row < 8; ++row) {
        machine.runtimeContext().write8(static_cast<uint16_t>(0x8000u + row * 2u), 0xFFu);
        machine.runtimeContext().write8(static_cast<uint16_t>(0x8001u + row * 2u), 0x00u);
    }
    machine.runtimeContext().write8(0x9800, 0x00u);
    machine.runtimeContext().write8(0x8010, 0xFFu);
    machine.runtimeContext().write8(0x8011, 0xFFu);
    for (uint16_t row = 1; row < 8; ++row) {
        machine.runtimeContext().write8(static_cast<uint16_t>(0x8010u + row * 2u), 0xFFu);
        machine.runtimeContext().write8(static_cast<uint16_t>(0x8011u + row * 2u), 0xFFu);
    }
    machine.runtimeContext().write8(0xFE00, 32u);
    machine.runtimeContext().write8(0xFE01, 16u);
    machine.runtimeContext().write8(0xFE02, 0x01u);
    machine.runtimeContext().write8(0xFE03, 0x00u);
    machine.runtimeContext().write8(0xFF47, 0xE4u);
    machine.runtimeContext().write8(0xFF48, 0xE4u);
    machine.runtimeContext().write8(0xFF40, 0x93u);
    machine.runtimeContext().write8(0xFF12, 0xF3u);
    if (initResult && frontend->audioOutputReady()) {
        assert(frontend->serviceFrontend());
        assert(frontend->bufferedAudioSamples() <= frontend->stats().audioRingBufferCapacitySamples);
    }
    const auto pumpedBeforeStep = frontend->pumpBackendEvents();
    (void)pumpedBeforeStep;
    for (int i = 0; i < 20000; ++i) {
        machine.step();
        if (frontend->lastVideoState().has_value() && frontend->lastVideoState()->inVBlank()) {
            break;
        }
    }
    assert(frontend->serviceFrontend());

    const auto& stats = frontend->stats();
    (void)stats;
    assert(stats.videoEvents >= 1);
    assert(stats.audioEvents >= 1);
    assert(stats.inputEvents >= 1);
    assert(stats.inputPolls >= 1);
    assert(stats.inputSamplesProvided >= 1);
    assert(stats.framesPrepared >= 1);
    assert(stats.renderAttempts >= 1);
    assert(stats.audioPreviewsBuilt >= 1);
    assert(stats.buttonTransitions >= 3);
    assert(stats.eventPumpCalls >= 2);
    assert(stats.serviceCalls >= 1);
    assert(!frontend->diagnostics().empty());
    assert(frontend->lastVideoState().has_value());
    assert(frontend->lastVideoState()->lcdc == 0x93u);
    assert(frontend->lastAudioPreview().has_value());
    assert(frontend->lastAudioPreview()->sampleCount() == 64u);
    if (initResult && frontend->audioOutputReady()) {
        assert(stats.audioSourceSampleRate == 48000);
        assert(stats.audioDeviceSampleRate == 48000);
        assert(stats.audioCallbackChunkSamples == 256u);
        assert(stats.audioRingBufferCapacitySamples == 2048u);
        assert(!stats.audioResamplingActive);
        assert(stats.audioResampleRatio == 1.0);
        assert(frontend->bufferedAudioSamples() <= stats.audioRingBufferCapacitySamples);
        assert(stats.audioOverrunDropCount == 0u);
        assert(stats.audioQueueRecoveryClears == 0u);

        std::vector<int16_t> drain(stats.audioRingBufferCapacitySamples + stats.audioCallbackChunkSamples, 0);
        machine.audioService().renderForOutput(drain);
        assert(frontend->bufferedAudioSamples() == 0u);
        assert(!frontend->audioQueueBackpressureActive());

        const auto lowWaterBefore = frontend->stats().audioQueueLowWaterHits;
        const auto framesPreparedBefore = frontend->stats().framesPrepared;
        const auto audioEventsBeforeLowWater = frontend->stats().audioEvents;
        frontend->onVideoEvent(BMMQ::MachineEvent{
            BMMQ::MachineEventType::VBlank,
            BMMQ::PluginCategory::Video,
            0,
            0xFF44u,
            machine.runtimeContext().read8(0xFF44u),
            nullptr,
            "audio-low-water video pressure regression"
        }, machine.view());
        assert(frontend->stats().audioQueueLowWaterHits == lowWaterBefore + 1u);
        assert(frontend->stats().framesPrepared == framesPreparedBefore);

        const auto nextAudioFrame = machine.audioFrameCounter() + 1u;
        assert(stepUntilAudioFrames(machine, nextAudioFrame));
        assert(frontend->stats().audioEvents >= audioEventsBeforeLowWater + 1u);
        assert(frontend->bufferedAudioSamples() > 0u);

        const auto highWaterStartFrame = machine.audioFrameCounter() + 1u;
        std::vector<int16_t> highWaterChunk(256u, 700);
        for (uint64_t frame = highWaterStartFrame; frame < highWaterStartFrame + 32u; ++frame) {
            machine.audioService().engine().appendRecentPcm(highWaterChunk, frame);
        }
        assert(frontend->audioQueueBackpressureActive());
    }
    assert(frontend->lastInputState().has_value());
    assert(frontend->lastInputState()->pressedMask == 0x15u);
    assert(frontend->lastFrame().has_value());
    assert(frontend->lastFrame()->width == 32);
    assert(frontend->lastFrame()->height == 24);
    assert(frontend->lastFrame()->pixelCount() == 32u * 24u);
    const auto shade1 = 0xFF88C070u;
    const auto shade3 = 0xFF081820u;
    (void)shade1;
    (void)shade3;
    assert(frontend->lastFrame()->pixels[0] == shade1);
    assert(frontend->lastFrame()->pixels[1] == shade1);
    assert(frontend->lastFrame()->pixels[7] == shade1);
    assert(frontend->lastFrame()->pixels[8] == shade1);
    assert(frontend->lastFrame()->pixels[16 * 32 + 8] == shade3);
    assert(frontend->lastFrame()->pixels[16 * 32 + 15] == shade3);
    assert(!frontend->lastRenderSummary().empty());
    assert(frontend->windowVisible());

    frontend->releaseButton(BMMQ::InputButton::Up);
    assert(frontend->queuedDigitalInputMask().has_value());
    assert(*frontend->queuedDigitalInputMask() == 0x11u);

    assert(frontend->handleHostEvent({BMMQ::SdlFrontendHostEventType::KeyDown, BMMQ::SdlFrontendHostKey::Return, false}));
    assert(frontend->isButtonPressed(BMMQ::InputButton::Meta2));
    assert(frontend->handleHostEvent({BMMQ::SdlFrontendHostEventType::KeyDown, BMMQ::SdlFrontendHostKey::X, false}));
    assert(frontend->isButtonPressed(BMMQ::InputButton::Button2));
    assert(frontend->handleHostEvent({BMMQ::SdlFrontendHostEventType::KeyUp, BMMQ::SdlFrontendHostKey::X, false}));
    assert(!frontend->isButtonPressed(BMMQ::InputButton::Button2));
    assert(frontend->stats().hostEventsHandled >= 3);
    assert(frontend->stats().keyEventsHandled >= 3);
    assert(!frontend->lastHostEventSummary().empty());

    assert(frontend->handleHostEvent({BMMQ::SdlFrontendHostEventType::Quit, BMMQ::SdlFrontendHostKey::Unknown, false}));
    assert(frontend->quitRequested());
    frontend->clearQuitRequest();
    assert(!frontend->quitRequested());

    frontend->clearQueuedDigitalInputMask();
    assert(!frontend->queuedDigitalInputMask().has_value());

    machine.pluginManager().shutdown(machine.mutableView());
    assert(frontend->stats().detachCount == 1);

    return 0;
}
