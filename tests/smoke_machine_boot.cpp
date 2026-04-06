#include <array>
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
        uint8_t readRegister8(std::string_view) const override { return static_cast<uint8_t>(regValue & 0x00FFu); }
        void writeRegister8(std::string_view, uint8_t value) override { regValue = value; }
        uint16_t readRegister16(std::string_view) const override { return regValue; }
        void writeRegister16(std::string_view, uint16_t value) override { regValue = value; }
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
        BMMQ::PluginManager manager;
        std::array<BMMQ::IoRegionDescriptor, 1> regions{{
            {BMMQ::PluginCategory::System, 0x0000u, 0x0000u, "fake", true, false}
        }};

        void loadRom(const std::vector<uint8_t>&) override {}
        BMMQ::RuntimeContext& runtimeContext() override { return context; }
        const BMMQ::RuntimeContext& runtimeContext() const override { return context; }
        BMMQ::PluginManager& pluginManager() override { return manager; }
        const BMMQ::PluginManager& pluginManager() const override { return manager; }
        std::span<const BMMQ::IoRegionDescriptor> describeIoRegions() const override { return regions; }
        uint16_t readRegisterPair(std::string_view) const override { return 0; }
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
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    cartridgeRom[0x0100] = 0x3E;
    cartridgeRom[0x0101] = 0x12;
    cartridgeRom[0x0102] = 0x00;
    host.loadRom(cartridgeRom);
    assert(host.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);
    assert(host.runtimeContext().attachedPolicyMetadata() != nullptr);
    assert(host.runtimeContext().attachedPolicyMetadata()->id == "bmmq.executor.policy.default-step");
    assert(!host.runtimeContext().capabilityProfile().interception);
    assert(host.runtimeContext().interceptionCapability() == nullptr);
    assert(host.runtimeContext().translationCapability() == nullptr);
    assert(host.runtimeContext().invalidationCapability() == nullptr);
    assert(host.runtimeContext().optimizationMetadataCapability() == nullptr);
    assert(host.runtimeContext().read8(0x0100) == 0x3E);
    assert(host.runtimeContext().read16(0x0101) == 0x0012);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0100);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::SP) == 0xFFFE);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::AF) == 0x01B0);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::BC) == 0x0013);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::DE) == 0x00D8);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::HL) == 0x014D);
    assert(host.runtimeContext().read8(0xFF00) == 0xCF);
    assert(host.runtimeContext().read8(0xFF40) == 0x91);
    assert(host.runtimeContext().read8(0xFF47) == 0xFC);
    host.runtimeContext().write8(0xC000, 0xAA);
    assert(host.runtimeContext().read8(0xC000) == 0xAA);
    assert(host.runtimeContext().read8(0xE000) == 0xAA);
    host.runtimeContext().write8(0xE000, 0xBB);
    assert(host.runtimeContext().read8(0xC000) == 0xBB);
    host.runtimeContext().write16(0xC100, 0x3456);
    assert(host.runtimeContext().read16(0xC100) == 0x3456);
    assert(host.runtimeContext().read16(0xE100) == 0x3456);
    host.runtimeContext().write8(0xFF68, 0x91);
    assert(host.runtimeContext().read8(0xFF68) == 0x91);
    host.step();
    assert(host.readRegisterPair(GB::RegisterId::AF) == static_cast<uint16_t>(0x12B0));
    assert(host.runtimeContext().readRegister16(GB::RegisterId::AF) == static_cast<uint16_t>(0x12B0));
    host.runtimeContext().writeRegister16(GB::RegisterId::BC, 0xBEEF);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::BC) == 0xBEEF);
    host.runtimeContext().writeRegister16(GB::RegisterId::SP, 0xC123);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::SP) == 0xC123);
    host.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0042);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0042);
    host.runtimeContext().commitVisibleState();
    assert(host.runtimeContext().getLastFeedback().pcBefore == 0x0100);
    assert(host.runtimeContext().getLastFeedback().pcAfter == 0x0102);

    host.loadRom(cartridgeRom);
    for (int i = 0; i < 256; ++i) {
        host.step();
    }
    assert(host.runtimeContext().read8(0xFF44) != 0x00);
    host.runtimeContext().write8(0xFF40, 0x00);
    host.step();
    assert(host.runtimeContext().read8(0xFF40) == 0x00);
    assert(host.runtimeContext().read8(0xFF44) == 0x00);

    std::vector<uint8_t> mappedIoRom(0x8000, 0x00);
    mappedIoRom[0x0000] = 0x99;
    mappedIoRom[0x0042] = 0x77;
    mappedIoRom[0x0100] = 0x3E;
    mappedIoRom[0x0101] = 0x77;
    mappedIoRom[0x0102] = 0xEA;
    mappedIoRom[0x0103] = 0x00;
    mappedIoRom[0x0104] = 0xE0;
    mappedIoRom[0x0105] = 0x3E;
    mappedIoRom[0x0106] = 0x91;
    mappedIoRom[0x0107] = 0xE0;
    mappedIoRom[0x0108] = 0x4F;
    mappedIoRom[0x0109] = 0x00;
    host.loadRom(mappedIoRom);
    assert(host.runtimeContext().read8(0x0000) == 0x99);
    assert(host.runtimeContext().read8(0x0042) == 0x77);
    assert(host.runtimeContext().read8(0xFF50) == 0x01);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0100);

    std::vector<uint8_t> explicitBootRom(0x100, 0x00);
    explicitBootRom[0x00] = 0x31;
    explicitBootRom[0x01] = 0xFE;
    explicitBootRom[0x02] = 0xFF;
    explicitBootRom[0x03] = 0x3E;
    explicitBootRom[0x04] = 0x01;
    explicitBootRom[0x05] = 0xE0;
    explicitBootRom[0x06] = 0x50;
    explicitBootRom[0x07] = 0xC3;
    explicitBootRom[0x08] = 0x00;
    explicitBootRom[0x09] = 0x01;
    explicitBootRom[0x42] = 0x3E;
    explicitBootRom[0x43] = 0x91;
    explicitBootRom[0x44] = 0xE0;
    explicitBootRom[0x45] = 0x40;
    machine.loadBootRom(explicitBootRom);
    assert(host.runtimeContext().read8(0x0000) == 0x31);
    assert(host.runtimeContext().read8(0x0042) == 0x3E);
    assert(host.runtimeContext().read8(0xFF50) == 0x00);
    assert(host.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0000);

    host.step();
    assert(host.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0003);
    host.step();
    assert(host.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0005);
    host.step();
    assert(host.runtimeContext().read8(0xFF50) == 0x01);
    assert(host.runtimeContext().read8(0x0000) == 0x99);
    host.step();
    assert(host.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0100);

    host.runtimeContext().write8(0xFF50, 0x00);
    assert(host.runtimeContext().read8(0x0000) == 0x99);
    assert(host.runtimeContext().read8(0x0042) == 0x77);
    host.step();
    host.step();
    assert(host.runtimeContext().read8(0xC000) == 0x77);
    host.step();
    host.step();
    assert(host.runtimeContext().read8(0xFF4F) == 0x91);

    std::vector<uint8_t> customBootRom(0x100, 0x00);
    customBootRom[0x0000] = 0xC3;
    customBootRom[0x0001] = 0x00;
    customBootRom[0x0002] = 0x01;
    customBootRom[0x0042] = 0xAB;
    machine.loadBootRom(customBootRom);
    assert(host.runtimeContext().read8(0x0000) == 0xC3);
    assert(host.runtimeContext().read8(0x0042) == 0xAB);
    assert(host.runtimeContext().read8(0xFF50) == 0x00);
    host.runtimeContext().write8(0xFF50, 0x01);
    assert(host.runtimeContext().read8(0x0000) == 0x99);

    bool invalidBootRomThrew = false;
    try {
        machine.loadBootRom(std::vector<uint8_t>(0xFF, 0x00));
    } catch (const std::invalid_argument&) {
        invalidBootRomThrew = true;
    }
    assert(invalidBootRomThrew);

    std::vector<uint8_t> inaccessibleTailRom(0x8000, 0x00);
    inaccessibleTailRom[0x0100] = 0x3E;
    inaccessibleTailRom[0x0101] = 0x42;
    inaccessibleTailRom[0x0102] = 0xEA;
    inaccessibleTailRom[0x0103] = 0xA0;
    inaccessibleTailRom[0x0104] = 0xFE;
    inaccessibleTailRom[0x0105] = 0xFA;
    inaccessibleTailRom[0x0106] = 0xA0;
    inaccessibleTailRom[0x0107] = 0xFE;
    inaccessibleTailRom[0x0108] = 0x00;
    host.loadRom(inaccessibleTailRom);
    assert(host.runtimeContext().read8(0xFEA0) == 0xFF);
    host.step();
    host.step();
    assert(host.runtimeContext().read8(0xFEA0) == 0xFF);
    host.step();
    assert(host.readRegisterPair(GB::RegisterId::AF) == static_cast<uint16_t>(0xFFB0));

    std::vector<uint8_t> mbc1Rom(0x10000, 0x00);
    mbc1Rom[0x0147] = 0x01;
    mbc1Rom[0x0148] = 0x01;
    mbc1Rom[0x0000] = 0x11;
    mbc1Rom[0x4000] = 0x22;
    mbc1Rom[0x8000] = 0x33;
    host.loadRom(mbc1Rom);
    assert(host.runtimeContext().read8(0x0000) == 0x11);
    assert(host.runtimeContext().read8(0x4000) == 0x22);
    host.runtimeContext().write8(0x2000, 0x02);
    assert(host.runtimeContext().read8(0x4000) == 0x33);

    host.attachExecutorPolicy(experimentalPolicy);
    assert(host.guarantee() == BMMQ::ExecutionGuarantee::Experimental);
    assert(host.runtimeContext().attachedPolicyMetadata()->id == "bmmq.executor.policy.experimental");
    assert(host.runtimeContext().attachedExecutorPolicy().metadata().id == "bmmq.executor.policy.experimental");

    LR3592_DMG core;
    assert(core.getMemory().file.findRegister("mdr") == nullptr);
    assert(core.getMemory().file.findRegister("mar") == nullptr);

    static_assert(!HasStepBaseline<GameBoyMachine>::value);
    static_assert(!HasHasCpu<GameBoyMachine>::value);
    static_assert(!HasHasMemoryMap<GameBoyMachine>::value);
    return 0;
}
