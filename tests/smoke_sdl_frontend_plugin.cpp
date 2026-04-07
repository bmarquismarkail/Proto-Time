#include <cassert>
#include <cstdlib>
#include <memory>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/SdlFrontendPlugin.hpp"

int main()
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

    auto frontendPlugin = std::make_unique<BMMQ::SdlFrontendPlugin>(config);
    auto* frontend = frontendPlugin.get();
    machine.pluginManager().add(std::move(frontendPlugin));

    machine.pluginManager().initialize(machine.view());
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

    frontend->pressButton(BMMQ::SdlFrontendButton::Right);
    frontend->pressButton(BMMQ::SdlFrontendButton::Up);
    frontend->pressButton(BMMQ::SdlFrontendButton::A);
    assert(frontend->queuedDigitalInputMask().has_value());
    assert(*frontend->queuedDigitalInputMask() == 0x15u);
    assert(frontend->isButtonPressed(BMMQ::SdlFrontendButton::A));

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
        assert(stats.audioQueueWrites >= 1);
        assert(stats.audioSamplesQueued >= frontend->lastAudioPreview()->sampleCount());
        const auto queueWritesBefore = stats.audioQueueWrites;
        const auto samplesQueuedBefore = stats.audioSamplesQueued;
        assert(frontend->serviceFrontend());
        assert(stats.audioQueueWrites == queueWritesBefore);
        assert(stats.audioSamplesQueued == samplesQueuedBefore);
    }
    assert(frontend->lastInputState().has_value());
    assert(frontend->lastInputState()->pressedMask == 0x15u);
    assert(frontend->lastFrame().has_value());
    assert(frontend->lastFrame()->width == 32);
    assert(frontend->lastFrame()->height == 24);
    assert(frontend->lastFrame()->pixelCount() == 32u * 24u);
    const auto shade1 = 0xFF88C070u;
    const auto shade3 = 0xFF081820u;
    assert(frontend->lastFrame()->pixels[0] == shade1);
    assert(frontend->lastFrame()->pixels[1] == shade1);
    assert(frontend->lastFrame()->pixels[7] == shade1);
    assert(frontend->lastFrame()->pixels[8] == shade1);
    assert(frontend->lastFrame()->pixels[16 * 32 + 8] == shade3);
    assert(frontend->lastFrame()->pixels[16 * 32 + 15] == shade3);
    assert(!frontend->lastRenderSummary().empty());
    assert(frontend->windowVisible());

    frontend->releaseButton(BMMQ::SdlFrontendButton::Up);
    assert(frontend->queuedDigitalInputMask().has_value());
    assert(*frontend->queuedDigitalInputMask() == 0x11u);

    assert(frontend->handleHostEvent({BMMQ::SdlFrontendHostEventType::KeyDown, BMMQ::SdlFrontendHostKey::Return, false}));
    assert(frontend->isButtonPressed(BMMQ::SdlFrontendButton::Start));
    assert(frontend->handleHostEvent({BMMQ::SdlFrontendHostEventType::KeyDown, BMMQ::SdlFrontendHostKey::X, false}));
    assert(frontend->isButtonPressed(BMMQ::SdlFrontendButton::B));
    assert(frontend->handleHostEvent({BMMQ::SdlFrontendHostEventType::KeyUp, BMMQ::SdlFrontendHostKey::X, false}));
    assert(!frontend->isButtonPressed(BMMQ::SdlFrontendButton::B));
    assert(frontend->stats().hostEventsHandled >= 3);
    assert(frontend->stats().keyEventsHandled >= 3);
    assert(!frontend->lastHostEventSummary().empty());

    assert(frontend->handleHostEvent({BMMQ::SdlFrontendHostEventType::Quit, BMMQ::SdlFrontendHostKey::Unknown, false}));
    assert(frontend->quitRequested());
    frontend->clearQuitRequest();
    assert(!frontend->quitRequested());

    frontend->clearQueuedDigitalInputMask();
    assert(!frontend->queuedDigitalInputMask().has_value());

    machine.pluginManager().shutdown(machine.view());
    assert(frontend->stats().detachCount == 1);

    return 0;
}
