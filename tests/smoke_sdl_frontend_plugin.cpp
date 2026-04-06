#include <cassert>
#include <memory>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/SdlFrontendPlugin.hpp"

int main()
{
    BMMQ::SdlFrontendConfig config;
    config.windowTitle = "Proto-Time SDL Smoke";
    config.windowScale = 3;
    config.frameWidth = 32;
    config.frameHeight = 24;
    config.autoInitializeBackend = false;

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

    frontend->pressButton(BMMQ::SdlFrontendButton::Right);
    frontend->pressButton(BMMQ::SdlFrontendButton::Up);
    frontend->pressButton(BMMQ::SdlFrontendButton::A);
    assert(frontend->queuedDigitalInputMask().has_value());
    assert(*frontend->queuedDigitalInputMask() == 0x15u);
    assert(frontend->isButtonPressed(BMMQ::SdlFrontendButton::A));

    machine.runtimeContext().write8(0x8000, 0x42u);
    machine.runtimeContext().write8(0xFF40, 0x91u);
    machine.runtimeContext().write8(0xFF12, 0xF3u);
    machine.step();

    const auto& stats = frontend->stats();
    assert(stats.videoEvents >= 1);
    assert(stats.audioEvents >= 1);
    assert(stats.inputEvents >= 1);
    assert(stats.inputPolls >= 1);
    assert(stats.inputSamplesProvided >= 1);
    assert(stats.framesPrepared >= 1);
    assert(stats.buttonTransitions >= 3);
    assert(!frontend->diagnostics().empty());
    assert(frontend->lastVideoState().has_value());
    assert(frontend->lastVideoState()->lcdc == 0x91u);
    assert(frontend->lastInputState().has_value());
    assert(frontend->lastInputState()->pressedMask == 0x15u);
    assert(frontend->lastFrame().has_value());
    assert(frontend->lastFrame()->width == 32);
    assert(frontend->lastFrame()->height == 24);
    assert(frontend->lastFrame()->pixelCount() == 32u * 24u);

    frontend->releaseButton(BMMQ::SdlFrontendButton::Up);
    assert(frontend->queuedDigitalInputMask().has_value());
    assert(*frontend->queuedDigitalInputMask() == 0x11u);

    frontend->requestQuit();
    assert(frontend->quitRequested());
    frontend->clearQuitRequest();
    assert(!frontend->quitRequested());

    frontend->clearQueuedDigitalInputMask();
    assert(!frontend->queuedDigitalInputMask().has_value());

    machine.pluginManager().shutdown(machine.view());
    assert(frontend->stats().detachCount == 1);

    return 0;
}
