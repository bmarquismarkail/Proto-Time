#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "inst_cycle/executor/PluginExecutor.hpp"

int main()
{
    struct CountingRuntimeContext final : BMMQ::RuntimeContext {
        int fetchCalls = 0;
        BMMQ::CpuFeedback feedback{};

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
        const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::BaselineFaithful;
        }
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

    BMMQ::Plugin::DefaultStepPolicy policy;
    BMMQ::Plugin::PluginExecutor executor(policy);

    const auto result = executor.step(host.runtimeContext());
    assert(result.executed);
    assert(result.guarantee == BMMQ::ExecutionGuarantee::BaselineFaithful);
    assert(result.feedback.isControlFlow);
    assert(result.feedback.segmentBoundaryHint);

    const auto& blocks = executor.recordedBlocks();
    assert(blocks.size() == 1);

    assert(host.readRegisterPair("AF") == static_cast<uint16_t>(0x1200));

    namespace fs = std::filesystem;
    const fs::path scriptPath = fs::temp_directory_path() / "time_plugin_executor_smoke.blocks";
    std::string saveError;
    const bool saved = executor.saveScript(scriptPath.string(), &saveError);
    assert(saved);
    assert(saveError.empty());

    CountingRuntimeContext playbackContext;
    BMMQ::Plugin::PluginExecutor player(policy);
    std::string loadError;
    const bool loaded = player.loadScript(scriptPath.string(), &loadError);
    assert(loaded);
    assert(loadError.empty());

    const auto playbackResult = player.step(playbackContext);
    assert(playbackResult.executed);
    assert(playbackResult.usedScript);
    assert(playbackContext.fetchCalls == 0);

    std::string missingError;
    const bool missingLoad = player.loadScript(
        (fs::temp_directory_path() / "time_plugin_executor_missing.blocks").string(),
        &missingError);
    assert(!missingLoad);
    assert(!missingError.empty());

    CountingRuntimeContext failedReloadContext;
    const auto liveAfterFailedLoad = player.step(failedReloadContext);
    assert(liveAfterFailedLoad.executed);
    assert(!liveAfterFailedLoad.usedScript);
    assert(failedReloadContext.fetchCalls == 1);

    fs::remove(scriptPath);

    return 0;
}
