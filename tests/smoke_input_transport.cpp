#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/SdlFrontendPlugin.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"

int main(int argc, char** argv)
{
#if defined(_WIN32)
    (void)_putenv_s("SDL_AUDIODRIVER", "dummy");
    (void)_putenv_s("SDL_VIDEODRIVER", "dummy");
#elif defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif

    BMMQ::SdlFrontendConfig config;
    config.enableVideo = false;
    config.enableAudio = false;
    config.enableInput = true;
    config.autoInitializeBackend = false;
    config.pumpBackendEventsOnInputSample = false;

    GameBoyMachine machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    machine.loadRom(cartridgeRom);

    const auto executablePath = (argc > 0 && argv != nullptr)
        ? std::filesystem::path(argv[0])
        : std::filesystem::path("time-smoke-input-transport");
    auto frontendPlugin = BMMQ::loadSdlFrontendPlugin(
        BMMQ::defaultSdlFrontendPluginPath(executablePath),
        config);
    auto* frontend = frontendPlugin.get();
    machine.pluginManager().add(std::move(frontendPlugin));
    machine.pluginManager().initialize(machine.mutableView());

    const auto rightMask = BMMQ::inputButtonMask(BMMQ::InputButton::Right);
    const auto button1Mask = BMMQ::inputButtonMask(BMMQ::InputButton::Button1);
    const auto rightAndButton1Mask = static_cast<uint32_t>(rightMask | button1Mask);

    assert(machine.inputService().state() == BMMQ::InputLifecycleState::Active);
    assert(!machine.currentDigitalInputMask().has_value());

    frontend->pressButton(BMMQ::InputButton::Right);
    frontend->pressButton(BMMQ::InputButton::Button1);
    assert(frontend->queuedDigitalInputMask().has_value());
    assert(*frontend->queuedDigitalInputMask() == rightAndButton1Mask);
    assert(!machine.currentDigitalInputMask().has_value());

    machine.step();
    assert(machine.currentDigitalInputMask().has_value());
    assert(*machine.currentDigitalInputMask() == rightAndButton1Mask);
    assert(frontend->stats().inputPolls >= 1u);
    assert(frontend->stats().inputSamplesProvided >= 1u);
    assert(frontend->lastInputState().has_value());
    assert(frontend->lastInputState()->pressedMask == rightAndButton1Mask);

    frontend->releaseButton(BMMQ::InputButton::Button1);
    assert(machine.currentDigitalInputMask().has_value());
    assert(*machine.currentDigitalInputMask() == rightAndButton1Mask);

    machine.step();
    assert(machine.currentDigitalInputMask().has_value());
    assert(*machine.currentDigitalInputMask() == rightMask);
    assert(frontend->lastInputState().has_value());
    assert(frontend->lastInputState()->pressedMask == rightMask);

    frontend->clearQueuedDigitalInputMask();
    assert(!frontend->queuedDigitalInputMask().has_value());
    machine.step();
    assert(machine.currentDigitalInputMask().has_value());
    assert(*machine.currentDigitalInputMask() == 0x00u);

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}