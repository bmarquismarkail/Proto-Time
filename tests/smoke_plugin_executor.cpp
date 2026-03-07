#include <cassert>

#include "gameboy/gameboy_plugin_runtime.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "inst_cycle/executor/PluginExecutor.hpp"

int main()
{
    LR3592_PluginRuntime runtime;
    BMMQ::Plugin::DefaultStepPolicy policy;
    BMMQ::Plugin::PluginExecutor executor(policy);

    const auto result = executor.step(runtime);
    assert(result.executed);
    assert(result.feedback.isControlFlow);
    assert(result.feedback.segmentBoundaryHint);

    const auto& blocks = executor.recordedBlocks();
    assert(blocks.size() == 1);

    auto* afEntry = runtime.cpu().getMemory().file.findRegister("AF");
    assert(afEntry != nullptr && afEntry->second != nullptr);
    auto* af = dynamic_cast<BMMQ::CPU_RegisterPair<uint16_t>*>(afEntry->second);
    assert(af != nullptr);
    assert(af->hi == 0x12);

    return 0;
}
