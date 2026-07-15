#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"

int main(int argc, char** argv)
{
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif

    BMMQ::SdlFrontendConfig config;
    config.windowTitle = "Proto-Time SDL ABI Smoke";
    config.frameWidth = 32;
    config.frameHeight = 24;
    config.windowScale = 2u;
    config.audioBackend = "dummy";
    config.autoInitializeBackend = false;
    config.createHiddenWindowOnInitialize = true;
    config.showWindowOnPresent = false;
    config.retainLastPresentedFrame = true;

    const auto executable = argc > 0
        ? std::filesystem::path(argv[0])
        : std::filesystem::path("time-smoke-sdl-frontend-plugin");
    const auto modulePath = BMMQ::defaultSdlFrontendPluginPath(executable);
    auto plugin = BMMQ::loadSdlFrontendPlugin(modulePath, config);
    auto* frontend = plugin.get();
    assert(frontend->id() == "bmmq.frontend.sdl");
    assert(frontend->displayName() == "SDL Frontend Plugin");
    assert(frontend->config().windowTitle == config.windowTitle);
    assert(!frontend->backendReady());

    GB::GameBoyMachine machine;
    std::vector<std::uint8_t> rom(0x8000u, 0u);
    machine.loadRom(rom);
    machine.pluginManager().add(std::move(plugin));
    machine.pluginManager().initialize(machine.mutableView());
    assert(frontend->stats().attachCount == 1u);

    const bool initialized = frontend->tryInitializeBackend();
    assert(frontend->stats().backendInitAttempts == 1u);
    assert(!frontend->backendName().empty());
    assert(!frontend->backendStatusSummary().empty());
    if (initialized) assert(frontend->backendReady());

    frontend->pressButton(BMMQ::InputButton::Right);
    frontend->pressButton(BMMQ::InputButton::Button1);
    assert(frontend->queuedDigitalInputMask() == 0x11u);
    assert(frontend->isButtonPressed(BMMQ::InputButton::Right));
    frontend->releaseButton(BMMQ::InputButton::Right);
    assert(frontend->queuedDigitalInputMask() == 0x10u);

    assert(frontend->handleHostEvent({
        BMMQ::SdlFrontendHostEventType::KeyDown,
        BMMQ::SdlFrontendHostKey::X,
        false}));
    assert(frontend->isButtonPressed(BMMQ::InputButton::Button2));
    assert(frontend->handleHostEvent({
        BMMQ::SdlFrontendHostEventType::KeyUp,
        BMMQ::SdlFrontendHostKey::X,
        false}));
    assert(!frontend->isButtonPressed(BMMQ::InputButton::Button2));

    frontend->onVideoEvent({
        BMMQ::MachineEventType::VBlank,
        BMMQ::PluginCategory::Video,
        1u,
        0xFF44u,
        144u,
        nullptr,
        "ABI video publication"}, machine.view());
    assert(frontend->stats().videoRealtimePacketsAccepted == 1u);
    assert(frontend->serviceFrontend());
    if (initialized) {
        assert(frontend->lastFrame().has_value());
        assert(frontend->lastFrame()->width == config.frameWidth);
        assert(frontend->lastFrame()->height == config.frameHeight);
    }

    assert(frontend->handleHostEvent({
        BMMQ::SdlFrontendHostEventType::Quit,
        BMMQ::SdlFrontendHostKey::Unknown,
        false}));
    assert(frontend->quitRequested());
    frontend->clearQuitRequest();
    assert(!frontend->quitRequested());

    machine.pluginManager().shutdown(machine.mutableView());
    assert(frontend->stats().detachCount == 1u);

    bool missingRejected = false;
    try {
        (void)BMMQ::loadSdlFrontendPlugin(
            executable.parent_path() / "missing-time-frontend-plugin.so", config);
    } catch (const std::runtime_error&) {
        missingRejected = true;
    }
    assert(missingRejected);
}
