#include <cassert>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "inst_cycle/executor/PluginExecutor.hpp"
#include "machine/RegisterId.hpp"

int main()
{
    struct CountingRuntimeContext final : BMMQ::RuntimeContext {
        int fetchCalls = 0;
        BMMQ::CpuFeedback feedback{};
        BMMQ::RuntimeCapabilityProfile profile{};

        FetchBlock fetch() override {
            FetchBlock block;
            block.setbaseAddress(static_cast<uint16_t>(0x200 + fetchCalls));
            ++fetchCalls;
            return block;
        }

        ExecutionBlock decode(FetchBlock&) override { return {}; }
        void execute(const ExecutionBlock&, FetchBlock&) override {
            feedback.segmentBoundaryHint = false;
            feedback.isControlFlow = false;
        }
        uint8_t read8(AddressType) const override { return 0; }
        void write8(AddressType, DataType) override {}
        uint16_t readRegisterPair(BMMQ::RegisterId) const override { return 0; }
        void writeRegisterPair(BMMQ::RegisterId, uint16_t) override {}
        const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::BaselineFaithful;
        }
        const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override { return nullptr; }
        BMMQ::RuntimeCapabilityProfile capabilityProfile() const override { return profile; }
    };

    struct ExperimentalMachinePolicy final : BMMQ::Plugin::IExecutorPolicyPlugin {
        using PluginFetchBlock = BMMQ::Plugin::FetchBlock;

        const BMMQ::Plugin::PluginMetadata& metadata() const override {
            static const BMMQ::Plugin::PluginMetadata meta{
                sizeof(BMMQ::Plugin::PluginMetadata),
                "bmmq.executor.policy.machine-experimental",
                "Machine Experimental Policy",
                BMMQ::Plugin::PluginKind::ExecutorPolicy,
                BMMQ::Plugin::kHostAbiVersion
            };
            return meta;
        }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::Experimental;
        }
        bool shouldRecord(const PluginFetchBlock&, const BMMQ::CpuFeedback&) const override { return true; }
        bool shouldSegment(const PluginFetchBlock&, const BMMQ::CpuFeedback&) const override { return false; }
    };

    BMMQ::Plugin::DefaultStepPolicy singleFetchPolicy;
    BMMQ::Plugin::PluginExecutor singleFetchExecutor(singleFetchPolicy);
    CountingRuntimeContext countingContext;
    const auto countedResult = singleFetchExecutor.step(countingContext);
    assert(countedResult.executed);
    assert(countingContext.fetchCalls == 1);
    assert(singleFetchExecutor.recordedBlocks().size() == 1);
    assert(singleFetchExecutor.recordedBlocks()[0].getbaseAddress() == 0x200);

    GameBoyMachine machine;
    BMMQ::Machine& host = machine;
    host.loadRom({0x3E, 0x12, 0x00});
    ExperimentalMachinePolicy machinePolicy;
    host.attachExecutorPolicy(machinePolicy);

    BMMQ::Plugin::DefaultStepPolicy policy;
    BMMQ::Plugin::PluginExecutor executor(policy);

    const auto result = executor.step(host.runtimeContext());
    assert(result.executed);
    assert(result.guarantee == BMMQ::ExecutionGuarantee::Experimental);
    assert(result.feedback.isControlFlow);
    assert(result.feedback.segmentBoundaryHint);
    assert(host.runtimeContext().attachedPolicyMetadata() != nullptr);
    assert(host.runtimeContext().attachedPolicyMetadata()->id == "bmmq.executor.policy.machine-experimental");

    const auto& blocks = executor.recordedBlocks();
    assert(blocks.size() == 1);
    const auto& recordedSegments = executor.recordedSegments();
    assert(recordedSegments.size() == 2);
    assert(recordedSegments[0].blocks.size() == 1);
    assert(recordedSegments[1].blocks.empty());

    assert(host.readRegisterPair(BMMQ::RegisterId::AF) == static_cast<uint16_t>(0x1200));

    namespace fs = std::filesystem;
    const fs::path scriptPath = fs::temp_directory_path() / "time_plugin_executor_smoke.blocks";
    executor.saveScript(scriptPath.string());

    CountingRuntimeContext playbackContext;
    BMMQ::Plugin::PluginExecutor player(policy);
    player.loadScript(scriptPath.string());

    const auto playbackResult = player.step(playbackContext);
    assert(playbackResult.executed);
    assert(playbackResult.usedScript);
    assert(playbackContext.fetchCalls == 0);

    bool missingLoadThrew = false;
    try {
        player.loadScript((fs::temp_directory_path() / "time_plugin_executor_missing.blocks").string());
    } catch (const std::runtime_error&) {
        missingLoadThrew = true;
    }
    assert(missingLoadThrew);

    CountingRuntimeContext failedReloadContext;
    const auto liveAfterFailedLoad = player.step(failedReloadContext);
    assert(liveAfterFailedLoad.executed);
    assert(!liveAfterFailedLoad.usedScript);
    assert(failedReloadContext.fetchCalls == 1);

    fs::remove(scriptPath);

    return 0;
}
