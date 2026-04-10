#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>

#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/MemoryMap.hpp"
#include "machine/RuntimeContext.hpp"
#include "memory/reg_base.hpp"

namespace {

struct DescriptorRuntimeContext final : BMMQ::RuntimeContext {
    struct DescriptorPolicy final : BMMQ::Plugin::IExecutorPolicyPlugin {
        const BMMQ::Plugin::PluginMetadata& metadata() const override;
        BMMQ::ExecutionGuarantee guarantee() const override
        {
            return BMMQ::ExecutionGuarantee::BaselineFaithful;
        }
        bool shouldRecord(const BMMQ::Plugin::FetchBlock&, const BMMQ::CpuFeedback&) const override { return true; }
        bool shouldSegment(const BMMQ::Plugin::FetchBlock&, const BMMQ::CpuFeedback&) const override { return false; }
    };

    BMMQ::MemoryMap memory;
    BMMQ::RegisterFile<uint16_t> registers;
    BMMQ::CpuFeedback feedback {};
    DescriptorPolicy policy {};

    DescriptorRuntimeContext()
    {
        memory.mapRam(0xC000, 0x2000);
        memory.mapRam(0xFF00, 0x0080);
        registers.addRegister({"PC", BMMQ::RegisterWidth::Word16, BMMQ::RegisterStorage::RegisterFile, std::nullopt, false});
        registers.addRegister({"AF", BMMQ::RegisterWidth::Word16, BMMQ::RegisterStorage::RegisterFile, std::nullopt, true});
        registers.registerDescriptor({"IO_TMP", BMMQ::RegisterWidth::Byte8, BMMQ::RegisterStorage::AddressMapped, 0xFF10, false});
    }

    FetchBlock fetch() override { return {}; }
    ExecutionBlock decode(FetchBlock&) override { return {}; }
    void execute(const ExecutionBlock&, FetchBlock&) override {}
    uint8_t read8(AddressType address) const override { return memory.read8(address); }
    void write8(AddressType address, DataType value) override { memory.write8(address, value); }
    uint8_t readRegister8(std::string_view id) const override;
    void writeRegister8(std::string_view id, uint8_t value) override;
    uint16_t readRegister16(std::string_view id) const override;
    void writeRegister16(std::string_view id, uint16_t value) override;
    uint16_t readRegisterPair(std::string_view id) const override;
    void writeRegisterPair(std::string_view id, uint16_t value) override;
    const BMMQ::CpuFeedback& getLastFeedback() const override { return feedback; }
    uint32_t clockHz() const override { return 1000000u; }
    BMMQ::ExecutionGuarantee guarantee() const override { return BMMQ::ExecutionGuarantee::BaselineFaithful; }
    const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override { return &policy.metadata(); }
    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override { return policy; }
};

const BMMQ::Plugin::PluginMetadata& DescriptorRuntimeContext::DescriptorPolicy::metadata() const
{
    static const BMMQ::Plugin::PluginMetadata meta{
        sizeof(BMMQ::Plugin::PluginMetadata),
        "bmmq.executor.policy.descriptor",
        "Descriptor Policy",
        BMMQ::Plugin::PluginKind::ExecutorPolicy,
        BMMQ::Plugin::kHostAbiVersion
    };
    return meta;
}

uint8_t DescriptorRuntimeContext::readRegister8(std::string_view id) const
{
    const auto* descriptor = registers.findDescriptor(id);
    if (descriptor == nullptr) {
        throw std::invalid_argument("register not found");
    }
    if (descriptor->width != BMMQ::RegisterWidth::Byte8) {
        throw std::invalid_argument("register width mismatch");
    }
    if (descriptor->storage == BMMQ::RegisterStorage::AddressMapped) {
        if (!descriptor->mappedAddress.has_value()) {
            throw std::invalid_argument("address-backed register missing address");
        }
        return read8(*descriptor->mappedAddress);
    }

    const auto* entry = registers.findRegister(id);
    if (entry == nullptr || entry->reg == nullptr) {
        throw std::invalid_argument("register not found");
    }
    return static_cast<uint8_t>(entry->reg->value & 0x00FFu);
}

void DescriptorRuntimeContext::writeRegister8(std::string_view id, uint8_t value)
{
    const auto* descriptor = registers.findDescriptor(id);
    if (descriptor == nullptr) {
        throw std::invalid_argument("register not found");
    }
    if (descriptor->width != BMMQ::RegisterWidth::Byte8) {
        throw std::invalid_argument("register width mismatch");
    }
    if (descriptor->storage == BMMQ::RegisterStorage::AddressMapped) {
        if (!descriptor->mappedAddress.has_value()) {
            throw std::invalid_argument("address-backed register missing address");
        }
        write8(*descriptor->mappedAddress, value);
        return;
    }

    auto* entry = registers.findRegister(id);
    if (entry == nullptr || entry->reg == nullptr) {
        throw std::invalid_argument("register not found");
    }
    entry->reg->value = value;
}

uint16_t DescriptorRuntimeContext::readRegister16(std::string_view id) const
{
    const auto* descriptor = registers.findDescriptor(id);
    if (descriptor == nullptr) {
        throw std::invalid_argument("register not found");
    }
    if (descriptor->width != BMMQ::RegisterWidth::Word16) {
        throw std::invalid_argument("register width mismatch");
    }
    if (descriptor->storage == BMMQ::RegisterStorage::AddressMapped) {
        if (!descriptor->mappedAddress.has_value()) {
            throw std::invalid_argument("address-backed register missing address");
        }
        return read16(*descriptor->mappedAddress);
    }

    const auto* entry = registers.findRegister(id);
    if (entry == nullptr || entry->reg == nullptr) {
        throw std::invalid_argument("register not found");
    }
    return entry->reg->value;
}

void DescriptorRuntimeContext::writeRegister16(std::string_view id, uint16_t value)
{
    const auto* descriptor = registers.findDescriptor(id);
    if (descriptor == nullptr) {
        throw std::invalid_argument("register not found");
    }
    if (descriptor->width != BMMQ::RegisterWidth::Word16) {
        throw std::invalid_argument("register width mismatch");
    }
    if (descriptor->storage == BMMQ::RegisterStorage::AddressMapped) {
        if (!descriptor->mappedAddress.has_value()) {
            throw std::invalid_argument("address-backed register missing address");
        }
        write16(*descriptor->mappedAddress, value);
        return;
    }

    auto* entry = registers.findRegister(id);
    if (entry == nullptr || entry->reg == nullptr) {
        throw std::invalid_argument("register not found");
    }
    entry->reg->value = value;
}

uint16_t DescriptorRuntimeContext::readRegisterPair(std::string_view id) const
{
    const auto* descriptor = registers.findDescriptor(id);
    if (descriptor == nullptr || !descriptor->isPair) {
        throw std::invalid_argument("register is not a pair");
    }
    return readRegister16(id);
}

void DescriptorRuntimeContext::writeRegisterPair(std::string_view id, uint16_t value)
{
    const auto* descriptor = registers.findDescriptor(id);
    if (descriptor == nullptr || !descriptor->isPair) {
        throw std::invalid_argument("register is not a pair");
    }
    writeRegister16(id, value);
}

} // namespace

int main()
{
    DescriptorRuntimeContext context;

    const auto* pc = context.registers.findDescriptor("PC");
    assert(pc != nullptr);
    assert(pc->width == BMMQ::RegisterWidth::Word16);
    assert(pc->storage == BMMQ::RegisterStorage::RegisterFile);
    assert(!pc->mappedAddress.has_value());

    const auto* io = context.registers.findDescriptor("IO_TMP");
    assert(io != nullptr);
    assert(io->width == BMMQ::RegisterWidth::Byte8);
    assert(io->storage == BMMQ::RegisterStorage::AddressMapped);
    assert(io->mappedAddress.has_value());
    assert(*io->mappedAddress == 0xFF10);

    context.writeRegister16("PC", 0x1234);
    assert(context.readRegister16("PC") == 0x1234);

    context.writeRegister8("IO_TMP", 0x5A);
    assert(context.read8(0xFF10) == 0x5A);
    context.write8(0xFF10, 0xA5);
    assert(context.readRegister8("IO_TMP") == 0xA5);

    bool widthMismatchThrew = false;
    try {
        (void)context.readRegister16("IO_TMP");
    } catch (const std::invalid_argument&) {
        widthMismatchThrew = true;
    }
    assert(widthMismatchThrew);

    bool missingMappingThrew = false;
    try {
        context.writeRegister8("UNDECLARED", 0x00);
    } catch (const std::invalid_argument&) {
        missingMappingThrew = true;
    }
    assert(missingMappingThrew);

    return 0;
}
