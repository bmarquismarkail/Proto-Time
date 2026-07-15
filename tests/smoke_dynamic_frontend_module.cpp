#include <cassert>
#include <filesystem>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/DynamicPluginModule.hpp"

int main(int argc, char** argv)
{
    assert(argc == 2);
    auto module = BMMQ::Plugin::DynamicPluginModule::load(argv[1]);
    assert(module.frontendIds() == std::vector<std::string>{"test.frontend.pure-c"});

    BMMQ::FrontendConfig config;
    config.enableVideo = false;
    config.enableAudio = false;
    config.enableInput = true;
    config.autoInitializeBackend = true;
    auto frontend = module.createFrontend("test.frontend.pure-c", config);
    assert(frontend->id() == "test.frontend.pure-c");
    assert(frontend->backendName() == "pure-c-test-frontend");

    GB::GameBoyMachine machine;
    machine.loadRom(std::vector<std::uint8_t>(0x8000u, 0u));
    auto* observer = frontend.get();
    machine.pluginManager().add(std::move(frontend));
    machine.pluginManager().initialize(machine.mutableView());
    assert(machine.inputService().state() == BMMQ::InputLifecycleState::Active);
    assert(observer->serviceFrontend());
    machine.serviceInput();
    assert(machine.currentDigitalInputMask() == BMMQ::inputButtonMask(BMMQ::InputButton::Right));
    assert(observer->quitRequested());
    assert(observer->stats().inputPolls >= 1u);
    machine.pluginManager().shutdown(machine.mutableView());
}
