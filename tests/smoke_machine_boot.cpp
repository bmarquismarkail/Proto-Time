#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/RegisterId.hpp"

template<typename T, typename = void>
struct HasStepBaseline : std::false_type {};

template<typename T>
struct HasStepBaseline<T, std::void_t<decltype(&T::stepBaseline)>> : std::true_type {};

template<typename T, typename = void>
struct HasHasCpu : std::false_type {};

template<typename T>
struct HasHasCpu<T, std::void_t<decltype(&T::hasCpu)>> : std::true_type {};

template<typename T, typename = void>
struct HasHasMemoryMap : std::false_type {};

template<typename T>
struct HasHasMemoryMap<T, std::void_t<decltype(&T::hasMemoryMap)>> : std::true_type {};

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
        const BMMQ::RuntimeContext& runtimeContext() const override { return context; }
        uint16_t readRegisterPair(BMMQ::RegisterId) const override { return 0; }
    };

    FakeMachine fake;
    assert(fake.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);
    fake.step();
    assert(fake.context.executed);

    GameBoyMachine machine;
    BMMQ::Machine& host = machine;
    host.loadRom({0x3E, 0x12, 0x00});
    assert(host.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);
    host.step();
    assert(host.readRegisterPair(BMMQ::RegisterId::AF) == static_cast<uint16_t>(0x1200));
    bool threw = false;
    try {
        (void)host.readRegisterPair(BMMQ::RegisterId::MDR);
    } catch (const std::exception&) {
        threw = true;
    }
    assert(threw);
    static_assert(!HasStepBaseline<GameBoyMachine>::value);
    static_assert(!HasHasCpu<GameBoyMachine>::value);
    static_assert(!HasHasMemoryMap<GameBoyMachine>::value);
    return 0;
}
