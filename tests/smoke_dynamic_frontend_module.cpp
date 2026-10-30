#include <cassert>
#include <filesystem>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/DynamicPluginModule.hpp"

static void testAttachCycle(BMMQ::Plugin::DynamicPluginModule& module, bool enableVideo)
{
    BMMQ::FrontendConfig config;
    config.enableVideo = enableVideo;
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
    assert(machine.videoService().diagnostics().state == (enableVideo
        ? BMMQ::VideoLifecycleState::Active : BMMQ::VideoLifecycleState::Headless));
    assert(machine.inputService().state() == BMMQ::InputLifecycleState::Active);
    assert(observer->serviceFrontend());
    machine.serviceInput();
    assert(machine.currentDigitalInputMask() == BMMQ::inputButtonMask(BMMQ::InputButton::Right));
    assert(observer->quitRequested());
    assert(observer->stats().inputPolls >= 1u);
    const auto generation = machine.inputService().currentGeneration();
    machine.pluginManager().shutdown(machine.mutableView());
    assert(machine.inputService().currentGeneration() == generation);
    machine.pluginManager().initialize(machine.mutableView());
    assert(machine.videoService().diagnostics().state == (enableVideo
        ? BMMQ::VideoLifecycleState::Active : BMMQ::VideoLifecycleState::Headless));
    assert(machine.inputService().state() == BMMQ::InputLifecycleState::Active);
    assert(observer->serviceFrontend());
    machine.serviceInput();
    assert(machine.currentDigitalInputMask() == BMMQ::inputButtonMask(BMMQ::InputButton::Right));
    machine.pluginManager().shutdown(machine.mutableView());
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    auto module = BMMQ::Plugin::DynamicPluginModule::load(argv[1]);
    assert(module.frontendIds() == std::vector<std::string>{"test.frontend.pure-c"});
    testAttachCycle(module, false);
    testAttachCycle(module, true);

    BMMQ::FrontendConfig config;
    config.enableVideo = true;
    config.enableInput = true;
    config.autoInitializeBackend = true;
    config.windowTitle = "fail-first-initialize";
    auto retry = module.createFrontend("test.frontend.pure-c", config);
    auto* retryObserver = retry.get();
    GB::GameBoyMachine retryMachine;
    auto& manager = retryMachine.pluginManager();
    manager.add(std::move(retry));
    manager.initialize(retryMachine.mutableView());
    auto status = manager.statusFor("test.frontend.pure-c");
    assert(status && status->disabled && !status->attached);
    assert(!status->lastError.empty());
    assert(retryMachine.videoService().diagnostics().state == BMMQ::VideoLifecycleState::Headless);
    assert(retryMachine.inputService().state() != BMMQ::InputLifecycleState::Active);
    assert(manager.reenable("test.frontend.pure-c"));
    manager.initialize(retryMachine.mutableView());
    status = manager.statusFor("test.frontend.pure-c");
    assert(status && status->attached && !status->disabled);
    assert(retryMachine.videoService().diagnostics().state == BMMQ::VideoLifecycleState::Active);
    assert(retryMachine.inputService().state() == BMMQ::InputLifecycleState::Active);
    assert(retryObserver->serviceFrontend());
    retryMachine.serviceInput();
    assert(retryMachine.currentDigitalInputMask() == BMMQ::inputButtonMask(BMMQ::InputButton::Right));
    manager.shutdown(retryMachine.mutableView());
}
