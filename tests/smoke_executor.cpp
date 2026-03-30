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

    struct CountingRuntimeContext final : BMMQ::RuntimeContext {
        int fetchCalls = 0;
        BMMQ::CpuFeedback feedback{};

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
        const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::BaselineFaithful;
        }
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
    host.loadRom({0x3E, 0x12, 0x00});

    BMMQ::Executor<AddressType, DataType> recorder(splitOnControl);
    static_assert(!std::is_invocable_v<decltype(&BMMQ::Executor<AddressType, DataType>::step),
                                       BMMQ::Executor<AddressType, DataType>&,
                                       LR3592_DMG&>);
    const auto stepResultRecord = recorder.step(host.runtimeContext());
    assert(stepResultRecord.executed);
    assert(stepResultRecord.guarantee == BMMQ::ExecutionGuarantee::BaselineFaithful);
    assert(stepResultRecord.feedback.isControlFlow);
    assert(stepResultRecord.feedback.segmentBoundaryHint);
    assert(recorder.recordedBlocks().size() == 1);
    assert(host.readRegisterPair("AF") == static_cast<uint16_t>(0x1200));
    return 0;
}
