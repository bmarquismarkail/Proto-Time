#include <cassert>
#include <cstdint>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "inst_cycle/executor/PluginExecutor.hpp"

int main()
{
    GameBoyMachine machine;
    machine.loadRom({0x3E, 0x12, 0x00});
    BMMQ::Machine& host = machine;

    BMMQ::Plugin::DefaultStepPolicy policy;
    BMMQ::Plugin::PluginExecutor executor(policy);

    const auto result = executor.step(host.runtimeContext());
    assert(result.executed);
    assert(result.guarantee == BMMQ::ExecutionGuarantee::BaselineFaithful);
    assert(result.feedback.isControlFlow);
    assert(result.feedback.segmentBoundaryHint);

    const auto& blocks = executor.recordedBlocks();
    assert(blocks.size() == 1);

    assert(machine.readRegisterPair("AF") == static_cast<uint16_t>(0x1200));

    return 0;
}
