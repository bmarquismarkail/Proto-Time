#include <cassert>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/IrExecutionService.hpp"
#include "inst_cycle/executor/ExecutorPolicyRegistry.hpp"
#include "machine/plugins/DynamicPluginModule.hpp"

namespace {

class Host final : public BMMQ::IR::InterpreterHost {
public:
    std::uint64_t readRegister(std::uint32_t, BMMQ::IR::ValueType) override { return 0u; }
    void writeRegister(std::uint32_t, BMMQ::IR::ValueType, std::uint64_t) override {}
    std::uint64_t loadMemory(std::uint64_t, BMMQ::IR::ValueType,
                             BMMQ::IR::MemoryClass) override { return 0u; }
    void storeMemory(std::uint64_t, BMMQ::IR::ValueType,
                     BMMQ::IR::MemoryClass, std::uint64_t) override {}
    std::uint64_t callHelper(std::uint32_t, BMMQ::IR::ValueType,
                             std::span<const std::uint64_t>) override { return 0u; }
    void setProgramCounter(std::uint64_t address) override { pc = address; }
    std::uint64_t pc = 0u;
};

std::unique_ptr<BMMQ::Plugin::IExecutorPolicyPlugin> portablePolicy()
{
    return BMMQ::Plugin::ExecutorPolicyRegistry::builtins().create(
        BMMQ::Plugin::executorPolicyIdForLegacyMode("ir"));
}

void exerciseMachineAttachment(const std::filesystem::path& adapterPluginPath,
                               const std::filesystem::path& backendPluginPath)
{
    const std::vector<std::uint8_t> rom(0x8000u, 0u);

    {
        auto components = BMMQ::Plugin::DynamicPluginModule::load(adapterPluginPath);
        BMMQ::GameGearMachine machine;
        auto policy = portablePolicy();
        machine.attachExecutorPolicy(*policy);
        machine.setIrComponents(
            components.createIrCoreAdapter("test.ir-adapter.gamegear-nop"), nullptr);
        components = {};
        machine.loadRom(rom);
        machine.step();
        assert(machine.runtimeContext().getLastFeedback().executionPath ==
               BMMQ::ExecutionPathHint::PortableIr);
        assert(machine.irStats().executions == 1u);
    }

    {
        auto components = BMMQ::Plugin::DynamicPluginModule::load(backendPluginPath);
        BMMQ::GameGearMachine machine;
        auto policy = portablePolicy();
        machine.attachExecutorPolicy(*policy);
        machine.setIrComponents(
            nullptr,
            components.createIrExecutionBackend("test.ir-backend.host-callback"));
        components = {};
        machine.loadRom(rom);
        machine.step();
        assert(machine.runtimeContext().getLastFeedback().executionPath ==
               BMMQ::ExecutionPathHint::PortableIr);
        assert(machine.irStats().executions == 1u);
    }

    {
        auto adapterModule = BMMQ::Plugin::DynamicPluginModule::load(adapterPluginPath);
        auto backendModule = BMMQ::Plugin::DynamicPluginModule::load(backendPluginPath);
        BMMQ::GameGearMachine machine;
        auto policy = portablePolicy();
        machine.attachExecutorPolicy(*policy);
        machine.setIrComponents(
            adapterModule.createIrCoreAdapter("test.ir-adapter.gamegear-nop"),
            backendModule.createIrExecutionBackend("test.ir-backend.host-callback"));
        adapterModule = {};
        backendModule = {};
        machine.loadRom(rom);
        machine.step();
        assert(machine.runtimeContext().getLastFeedback().executionPath ==
               BMMQ::ExecutionPathHint::PortableIr);
        assert(machine.irStats().executions == 1u);
    }
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 6);
    assert(argv != nullptr);
    auto module = BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[1]));
    assert(module.irCoreAdapterIds() ==
           std::vector<std::string>{"test.ir-adapter.gamegear-nop"});
    assert(module.irExecutionBackendIds() ==
           std::vector<std::string>{"test.ir-backend.host-callback"});
    auto adapter = module.createIrCoreAdapter("test.ir-adapter.gamegear-nop");
    auto backend = module.createIrExecutionBackend("test.ir-backend.host-callback");
    assert(adapter && backend && backend->supports(adapter->architectureId(),
                                                  adapter->irAbiVersion()));
    module = {};

    const std::vector<BMMQ::IR::SourceInstruction> source{
        {.address = 0x100u, .bytes = {0x00u, 0u, 0u, 0u}, .length = 1u}};
    std::string error;
    const auto block = adapter->lower({.instructions = source,
                                       .mappingGeneration = 7u,
                                       .executionState = 0u}, &error);
    assert(block && error.empty());
    {
        BMMQ::IR::IrExecutionService service(*adapter, *backend);
        const auto prepared = service.prepare(block, &error);
        assert(prepared.prepared && prepared.artifact && error.empty());
        Host host;
        BMMQ::IR::InterpreterResult result;
        assert(service.tryExecute(*block, *prepared.artifact, 0u, host, &result));
        assert(host.pc == 0x101u && result.retirementReached);
    }

    adapter.reset();
    backend.reset();

    exerciseMachineAttachment(std::filesystem::path(argv[4]),
                              std::filesystem::path(argv[5]));

    bool malformedRejected = false;
    try {
        (void)BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[2]));
    } catch (const std::runtime_error&) {
        malformedRejected = true;
    }
    assert(malformedRejected);

    bool incompatibleAbiRejected = false;
    try {
        (void)BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[3]));
    } catch (const std::runtime_error&) {
        incompatibleAbiRejected = true;
    }
    assert(incompatibleAbiRejected);
    return 0;
}
