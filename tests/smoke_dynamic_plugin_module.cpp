#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/plugins/DynamicPluginModule.hpp"

int main(int argc, char** argv)
{
    assert(argc == 4);
    auto module = BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[1]));
    assert(module.id() == "test.module.c-executor");
    assert(module.displayName() == "C Executor Test Module");
    assert(module.executorPolicyIds() == std::vector<std::string>{"test.executor.c-portable-ir"});

    auto policy = module.createExecutorPolicy("test.executor.c-portable-ir");
    assert(policy->metadata().id == "test.executor.c-portable-ir");
    assert(policy->backend() == BMMQ::ExecutionBackend::PortableIr);
    assert(policy->guarantee() == BMMQ::ExecutionGuarantee::VisibleStatePreserving);
    assert(policy->requiredCapabilities().translation);
    assert(policy->requiredCapabilities().invalidation);

    BMMQ::Plugin::FetchBlock block;
    block.setbaseAddress(0x1234u);
    BMMQ::CpuFeedback feedback;
    assert(policy->shouldRecord(block, feedback));
    feedback.segmentBoundaryHint = true;
    assert(policy->shouldSegment(block, feedback));
    auto clone = policy->clone();
    assert(clone->metadata().id == policy->metadata().id);

    GB::GameBoyMachine machine;
    std::vector<std::uint8_t> rom(0x8000u, 0u);
    machine.loadRom(rom);
    machine.attachExecutorPolicy(*policy);
    policy.reset();
    clone.reset();
    module = {};
    assert(machine.attachedExecutorPolicy().metadata().id == "test.executor.c-portable-ir");
    assert(machine.portableIrEnabled());
    machine.step();

    BMMQ::GameGearMachine gameGear;
    auto secondModule = BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[1]));
    auto unsupported = secondModule.createExecutorPolicy("test.executor.c-portable-ir");
    bool rejected = false;
    try {
        gameGear.attachExecutorPolicy(*unsupported);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);

    bool unknownRejected = false;
    try {
        (void)secondModule.createExecutorPolicy("missing");
    } catch (const std::invalid_argument&) {
        unknownRejected = true;
    }
    assert(unknownRejected);

    bool missingEntrypointRejected = false;
    try {
        (void)BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[2]));
    } catch (const std::runtime_error&) {
        missingEntrypointRejected = true;
    }
    assert(missingEntrypointRejected);

    bool malformedDescriptorRejected = false;
    try {
        (void)BMMQ::Plugin::DynamicPluginModule::load(std::filesystem::path(argv[3]));
    } catch (const std::runtime_error&) {
        malformedDescriptorRejected = true;
    }
    assert(malformedDescriptorRejected);
}
