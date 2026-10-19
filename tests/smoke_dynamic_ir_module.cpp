#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"
#include "cores/gamegear/GameGearIrExecution.hpp"
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
    void setProgramCounter(std::uint64_t address) override {
        pc = address;
        if (throwNonStandard) throw 42;
        if (throwPc) throw std::runtime_error("host PC callback failed");
    }
    bool throwPc = false;
    bool throwNonStandard = false;
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

void exerciseMissingGuardFallback(const std::filesystem::path& pluginPath)
{
    auto module = BMMQ::Plugin::DynamicPluginModule::load(pluginPath);
    BMMQ::GameGearMachine machine;
    auto policy = portablePolicy();
    machine.attachExecutorPolicy(*policy);
    machine.setIrComponents(
        module.createIrCoreAdapter("test.ir-adapter.gamegear-nop"), nullptr);
    module = {};
    machine.loadRom(std::vector<std::uint8_t>(0x8000u, 0u));
    machine.step();

    assert(machine.runtimeContext().getLastFeedback().executionPath ==
           BMMQ::ExecutionPathHint::CanonicalFetchDecodeExecute);
    const auto stats = machine.irStats();
    assert(stats.fallbacks == 1u);
    assert(stats.translations == 0u);
    assert(stats.executions == 0u);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 8);
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
    const BMMQ::IR::LoweringRequest request{.instructions = source,
                                             .mappingGeneration = 7u,
                                             .executionState = 0u};
    const auto block = adapter->lower(request, &error);
    assert(block && error.empty());
    {
        BMMQ::GameGearIR::CoreAdapter hostValidator;
        BMMQ::IR::IrExecutionService service(*adapter, *backend, hostValidator);
        const auto prepared = service.prepare(request, block, &error);
        assert(prepared.prepared && prepared.artifact && error.empty());
        Host host;
        BMMQ::IR::InterpreterResult result;
        assert(service.tryExecute(*block, *prepared.artifact, 0u, host, &result));
        assert(host.pc == 0x101u && result.retirementReached);
        // The C callback must contain exceptions until dispatch returns, then
        // the service must fail fatally with the cause, even after a mutation.
        for (bool nonStandard : {false, true}) {
            host.pc = 0u;
            host.throwPc = true;
            host.throwNonStandard = nonStandard;
            result = {};
            bool rejected = false;
            try {
                (void)service.tryExecute(*block, *prepared.artifact, 0u, host, &result);
            } catch (const std::runtime_error& failure) {
                const std::string message = failure.what();
                rejected = message.find(nonStandard ? "non-standard exception" :
                                                       "host PC callback failed") != std::string::npos;
            }
            assert(rejected && host.pc == 0x101u && !result.retirementReached);
        }

    }

    {
        auto throwingModule = BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[7]));
        auto throwingBackend = throwingModule.createIrExecutionBackend("test.ir-backend.host-callback");
        BMMQ::GameGearIR::CoreAdapter hostValidator;
        BMMQ::IR::IrExecutionService service(*adapter, *throwingBackend, hostValidator);
        const auto prepared = service.prepare(request, block);
        assert(prepared.prepared && prepared.artifact);
        Host host;
        bool rejected = false;
        try {
            (void)service.tryExecute(*block, *prepared.artifact, 0u, host);
        } catch (const std::runtime_error& failure) {
            rejected = std::string(failure.what()).find("fixture IR execution exception") != std::string::npos;
        }
        assert(rejected && host.pc == 0x101u);
    }

    adapter.reset();
    backend.reset();

    exerciseMachineAttachment(std::filesystem::path(argv[4]),
                              std::filesystem::path(argv[5]));
    exerciseMissingGuardFallback(std::filesystem::path(argv[6]));

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
