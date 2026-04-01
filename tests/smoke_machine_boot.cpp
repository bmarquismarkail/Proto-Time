#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
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
        struct FakePolicy final : BMMQ::Plugin::IExecutorPolicyPlugin {
            const BMMQ::Plugin::PluginMetadata& metadata() const override {
                static const BMMQ::Plugin::PluginMetadata meta{
                    sizeof(BMMQ::Plugin::PluginMetadata),
                    "bmmq.executor.policy.fake",
                    "Fake Policy",
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

        bool executed = false;
        BMMQ::CpuFeedback feedback{};
        uint8_t memory[4] {0x34, 0x12, 0x00, 0x00};
        uint16_t regValue = 0;
        FakePolicy policy;

        FetchBlock fetch() override { return {}; }
        ExecutionBlock decode(FetchBlock&) override { return {}; }
        void execute(const ExecutionBlock&, FetchBlock&) override { executed = true; }
        uint8_t read8(AddressType address) const override { return memory[address % 4]; }
        void write8(AddressType address, DataType value) override { memory[address % 4] = value; }
        uint16_t readRegister16(BMMQ::RegisterId) const override { return regValue; }
        void writeRegister16(BMMQ::RegisterId, uint16_t value) override { regValue = value; }
        const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
        BMMQ::ExecutionGuarantee guarantee() const override {
            return BMMQ::ExecutionGuarantee::BaselineFaithful;
        }
        const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override { return &policy.metadata(); }
        const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override { return policy; }
    };

    struct ExperimentalPolicy final : BMMQ::Plugin::IExecutorPolicyPlugin {
        using PluginFetchBlock = BMMQ::Plugin::FetchBlock;

        const BMMQ::Plugin::PluginMetadata& metadata() const override {
            static const BMMQ::Plugin::PluginMetadata meta{
                sizeof(BMMQ::Plugin::PluginMetadata),
                "bmmq.executor.policy.experimental",
                "Experimental Policy",
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

    struct FakeMachine final : BMMQ::Machine {
        FakeRuntimeContext context;
        ExperimentalPolicy policy;

        void loadRom(const std::vector<uint8_t>&) override {}
        BMMQ::RuntimeContext& runtimeContext() override { return context; }
        const BMMQ::RuntimeContext& runtimeContext() const override { return context; }
        uint16_t readRegisterPair(BMMQ::RegisterId) const override { return 0; }
        void attachExecutorPolicy(BMMQ::Plugin::IExecutorPolicyPlugin&) override {}
        const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override { return policy; }
    };

    FakeMachine fake;
    assert(fake.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);
    fake.step();
    assert(fake.context.executed);

    GameBoyMachine machine;
    BMMQ::Machine& host = machine;
    ExperimentalPolicy experimentalPolicy;
    bool missingRomThrew = false;
    try {
        host.step();
    } catch (const std::runtime_error&) {
        missingRomThrew = true;
    }
    assert(missingRomThrew);

    host.loadRom({0x3E, 0x12, 0x00});
    assert(host.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);
    assert(host.runtimeContext().attachedPolicyMetadata() != nullptr);
    assert(host.runtimeContext().attachedPolicyMetadata()->id == "bmmq.executor.policy.default-step");
    assert(!host.runtimeContext().capabilityProfile().interception);
    assert(host.runtimeContext().interceptionCapability() == nullptr);
    assert(host.runtimeContext().translationCapability() == nullptr);
    assert(host.runtimeContext().invalidationCapability() == nullptr);
    assert(host.runtimeContext().optimizationMetadataCapability() == nullptr);
    assert(host.runtimeContext().read8(0x0000) == 0x3E);
    assert(host.runtimeContext().read16(0x0001) == 0x0012);
    host.runtimeContext().write8(0xC000, 0xAA);
    assert(host.runtimeContext().read8(0xC000) == 0xAA);
    host.runtimeContext().write16(0xC100, 0x3456);
    assert(host.runtimeContext().read16(0xC100) == 0x3456);
    host.step();
    assert(host.readRegisterPair(BMMQ::RegisterId::AF) == static_cast<uint16_t>(0x1200));
    assert(host.runtimeContext().readRegister16(BMMQ::RegisterId::AF) == static_cast<uint16_t>(0x1200));
    host.runtimeContext().writeRegister16(BMMQ::RegisterId::BC, 0xBEEF);
    assert(host.runtimeContext().readRegister16(BMMQ::RegisterId::BC) == 0xBEEF);
    host.runtimeContext().writeRegister16(BMMQ::RegisterId::SP, 0xC123);
    assert(host.runtimeContext().readRegister16(BMMQ::RegisterId::SP) == 0xC123);
    host.runtimeContext().writeRegister16(BMMQ::RegisterId::PC, 0x0042);
    assert(host.runtimeContext().readRegister16(BMMQ::RegisterId::PC) == 0x0042);
    host.runtimeContext().commitVisibleState();
    assert(host.runtimeContext().getLastFeedback().pcBefore == 0);
    assert(host.runtimeContext().getLastFeedback().pcAfter == 3);
    host.attachExecutorPolicy(experimentalPolicy);
    assert(host.guarantee() == BMMQ::ExecutionGuarantee::Experimental);
    assert(host.runtimeContext().attachedPolicyMetadata()->id == "bmmq.executor.policy.experimental");
    assert(host.runtimeContext().attachedExecutorPolicy().metadata().id == "bmmq.executor.policy.experimental");
    bool threw = false;
    try {
        (void)host.runtimeContext().readRegister16(BMMQ::RegisterId::MDR);
    } catch (const std::exception&) {
        threw = true;
    }
    assert(threw);
    static_assert(!HasStepBaseline<GameBoyMachine>::value);
    static_assert(!HasHasCpu<GameBoyMachine>::value);
    static_assert(!HasHasMemoryMap<GameBoyMachine>::value);
    return 0;
}
