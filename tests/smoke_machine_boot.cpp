#include <cassert>
#include <cstdint>

#include "cores/gameboy/GameBoyMachine.hpp"

int main() {
    struct FakeRuntimeContext final : BMMQ::RuntimeContext {
        bool executed = false;
        BMMQ::CpuFeedback feedback{};

        FetchBlock fetch() override { return {}; }
        ExecutionBlock decode(FetchBlock&) override { return {}; }
        void execute(const ExecutionBlock&, FetchBlock&) override { executed = true; }
        const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::BaselineFaithful;
        }
    };

    struct FakeMachine final : BMMQ::Machine {
        FakeRuntimeContext context;

        void loadRom(const std::vector<uint8_t>&) override {}
        BMMQ::RuntimeContext& runtimeContext() override { return context; }
        uint16_t readRegisterPair(std::string_view) override { return 0; }
    };

    FakeMachine fake;
    fake.runtimeContext().step();
    assert(fake.context.executed);

    GameBoyMachine machine;
    BMMQ::Machine& host = machine;
    host.loadRom({0x3E, 0x12, 0x00});
    assert(host.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);
    host.step();
    assert(host.readRegisterPair("AF") == static_cast<uint16_t>(0x1200));
    return 0;
}
