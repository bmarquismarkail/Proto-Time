#include <cassert>
#include <cstdint>
#include <filesystem>

#include "gameboy/gameboy.hpp"
#include "inst_cycle/executor/Executor.hpp"

int main()
{
    namespace fs = std::filesystem;
    using AddressType = uint16_t;
    using DataType = uint8_t;

    auto splitOnControl = [](
        const BMMQ::fetchBlock<AddressType, DataType>&,
        const BMMQ::CpuFeedback& feedback) {
        return feedback.segmentBoundaryHint || feedback.isControlFlow;
    };

    LR3592_DMG cpuRecord;
    BMMQ::Executor<AddressType, DataType> recorder(splitOnControl);
    const auto stepResultRecord = recorder.step(cpuRecord);
    assert(stepResultRecord.executed);
    assert(!stepResultRecord.usedScript);
    assert(stepResultRecord.feedback.isControlFlow);
    assert(stepResultRecord.feedback.segmentBoundaryHint);
    assert(recorder.recordedBlocks().size() == 1);

    const fs::path scriptPath = fs::temp_directory_path() / "time_executor_smoke.blocks";
    std::string saveError;
    const bool saved = recorder.saveScript(scriptPath.string(), &saveError);
    assert(saved);
    assert(saveError.empty());

    LR3592_DMG cpuReplay;
    BMMQ::Executor<AddressType, DataType> player(splitOnControl);
    std::string loadError;
    const bool loaded = player.loadScript(scriptPath.string(), &loadError);
    assert(loaded);
    assert(loadError.empty());

    const auto stepResultReplay = player.step(cpuReplay);
    assert(stepResultReplay.executed);
    assert(stepResultReplay.usedScript);
    assert(stepResultReplay.feedback.isControlFlow);

    auto* afEntry = cpuReplay.getMemory().file.findRegister("AF");
    assert(afEntry != nullptr && afEntry->second != nullptr);
    auto* af = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(afEntry->second);
    assert(af != nullptr);
    assert(af->hi == 0x12);

    fs::remove(scriptPath);
    return 0;
}
