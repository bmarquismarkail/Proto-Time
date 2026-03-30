#include <cassert>
#include <cstdint>
#include <type_traits>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/gameboy.hpp"
#include "inst_cycle/executor/Executor.hpp"

int main()
{
    using AddressType = uint16_t;
    using DataType = uint8_t;

    auto splitOnControl = [](
        const BMMQ::fetchBlock<AddressType, DataType>&,
        const BMMQ::CpuFeedback& feedback) {
        return feedback.segmentBoundaryHint || feedback.isControlFlow;
    };

    GameBoyMachine machine;
    machine.loadRom({0x3E, 0x12, 0x00});
    BMMQ::Machine& host = machine;

    BMMQ::Executor<AddressType, DataType> recorder(splitOnControl);
    static_assert(!std::is_invocable_v<decltype(&BMMQ::Executor<AddressType, DataType>::step),
                                       BMMQ::Executor<AddressType, DataType>&,
                                       LR3592_DMG&>);
    const auto stepResultRecord = recorder.step(host.runtimeContext());
    assert(stepResultRecord.executed);
    assert(stepResultRecord.feedback.isControlFlow);
    assert(stepResultRecord.feedback.segmentBoundaryHint);
    assert(recorder.recordedBlocks().size() == 1);
    assert(machine.readRegisterPair("AF") == static_cast<uint16_t>(0x1200));
    return 0;
}
