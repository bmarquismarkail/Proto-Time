#include <cassert>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/gameboy.hpp"
#include "inst_cycle/executor/Executor.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/RegisterId.hpp"

int main()
{
    using AddressType = uint16_t;
    using DataType = uint8_t;

    struct CountingRuntimeContext final : BMMQ::RuntimeContext {
        struct CountingPolicy final : BMMQ::Plugin::IExecutorPolicyPlugin {
            const BMMQ::Plugin::PluginMetadata& metadata() const override {
                static const BMMQ::Plugin::PluginMetadata meta{
                    sizeof(BMMQ::Plugin::PluginMetadata),
                    "bmmq.executor.policy.counting",
                    "Counting Policy",
                    BMMQ::Plugin::PluginKind::ExecutorPolicy,
                    BMMQ::Plugin::kHostAbiVersion
                };
                return meta;
            }
            BMMQ::ExecutionGuarantee guarantee() const override {
                return BMMQ::ExecutionGuarantee::BaselineFaithful;
            }
            bool shouldRecord(const BMMQ::Plugin::FetchBlock&, const BMMQ::CpuFeedback&) const override { return true; }
            bool shouldSegment(const BMMQ::Plugin::FetchBlock&, const BMMQ::CpuFeedback&) const override { return false; }
        };

        int fetchCalls = 0;
        BMMQ::CpuFeedback feedback{};
        CountingPolicy policy{};

        FetchBlock fetch() override {
            FetchBlock block;
            block.setbaseAddress(static_cast<AddressType>(0x100 + fetchCalls));
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
        uint16_t readRegister16(BMMQ::RegisterId) const override { return 0; }
        void writeRegister16(BMMQ::RegisterId, uint16_t) override {}
        const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::BaselineFaithful;
        }
        const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override { return &policy.metadata(); }
        const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override { return policy; }
    };

    auto splitOnControl = [](
        const BMMQ::fetchBlock<AddressType, DataType>&,
        const BMMQ::CpuFeedback& feedback) {
        return feedback.segmentBoundaryHint || feedback.isControlFlow;
    };

    BMMQ::Executor<AddressType, DataType> singleFetchRecorder;
    CountingRuntimeContext countingContext;
    const auto countedResult = singleFetchRecorder.step(countingContext);
    assert(countedResult.executed);
    assert(countingContext.fetchCalls == 1);
    assert(singleFetchRecorder.recordedBlocks().size() == 1);
    assert(singleFetchRecorder.recordedBlocks()[0].getbaseAddress() == 0x100);

    GameBoyMachine machine;
    BMMQ::Machine& host = machine;
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    cartridgeRom[0x0100] = 0x3E;
    cartridgeRom[0x0101] = 0x12;
    cartridgeRom[0x0102] = 0x00;
    host.loadRom(cartridgeRom);

    BMMQ::Executor<AddressType, DataType> recorder(splitOnControl);
    static_assert(!std::is_invocable_v<decltype(&BMMQ::Executor<AddressType, DataType>::step),
                                       BMMQ::Executor<AddressType, DataType>&,
                                       LR3592_DMG&>);
    const auto stepResultRecord = recorder.step(host.runtimeContext());
    assert(stepResultRecord.executed);
    assert(stepResultRecord.guarantee == BMMQ::ExecutionGuarantee::BaselineFaithful);
    assert(!stepResultRecord.feedback.isControlFlow);
    assert(!stepResultRecord.feedback.segmentBoundaryHint);
    assert(recorder.recordedBlocks().size() == 1);
    assert(host.readRegisterPair(BMMQ::RegisterId::AF) == static_cast<uint16_t>(0x12B0));

    namespace fs = std::filesystem;
    const fs::path scriptPath = fs::temp_directory_path() / "time_executor_smoke.blocks";
    recorder.saveScript(scriptPath.string());

    recorder.loadScript(scriptPath.string());
    assert(recorder.recordedBlocks().empty());

    CountingRuntimeContext playbackContext;
    BMMQ::Executor<AddressType, DataType> player(splitOnControl);
    player.loadScript(scriptPath.string());

    const auto playbackResult = player.step(playbackContext);
    assert(playbackResult.executed);
    assert(playbackResult.usedScript);
    assert(playbackContext.fetchCalls == 0);

    bool missingLoadThrew = false;
    try {
        player.loadScript((fs::temp_directory_path() / "time_executor_missing.blocks").string());
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
