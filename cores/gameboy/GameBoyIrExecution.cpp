#include "GameBoyIrExecution.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <vector>

namespace GB::IRExecution {
namespace {

using BMMQ::IR::BlockBuilder;
using BMMQ::IR::BlockExit;
using BMMQ::IR::MemoryClass;
using BMMQ::IR::Opcode;
using BMMQ::IR::Operand;
using BMMQ::IR::ValueId;
using BMMQ::IR::ValueType;

class AbiHost final : public BMMQ::IR::InterpreterHost {
public:
    explicit AbiHost(const ExecutionAbiV1& abi) : abi_(abi) {}

    std::uint64_t readRegister(std::uint32_t id, ValueType) override
    {
        return abi_.readRegister(abi_.opaque, static_cast<Register>(id));
    }

    void writeRegister(std::uint32_t id, ValueType, std::uint64_t value) override
    {
        abi_.writeRegister(abi_.opaque, static_cast<Register>(id), value);
    }

    std::uint64_t loadMemory(std::uint64_t address, ValueType type, MemoryClass) override
    {
        if (type != ValueType::I8) {
            throw std::invalid_argument("Game Boy Phase 11A supports only 8-bit IR memory loads");
        }
        return abi_.readMemory8(abi_.opaque, static_cast<std::uint16_t>(address));
    }

    void storeMemory(std::uint64_t address, ValueType type, MemoryClass,
                     std::uint64_t value) override
    {
        if (type != ValueType::I8) {
            throw std::invalid_argument("Game Boy Phase 11A supports only 8-bit IR memory stores");
        }
        abi_.writeMemory8(abi_.opaque, static_cast<std::uint16_t>(address),
                          static_cast<std::uint8_t>(value));
    }

    std::uint64_t callHelper(std::uint32_t id, ValueType,
                             std::span<const std::uint64_t> arguments) override
    {
        return abi_.callHelper(abi_.opaque, static_cast<Helper>(id),
                               arguments.data(), arguments.size());
    }

    void setProgramCounter(std::uint64_t address) override
    {
        abi_.writeRegister(abi_.opaque, Register::PC, address);
    }

private:
    const ExecutionAbiV1& abi_;
};

constexpr std::optional<Register> registerForCode(std::uint8_t code) noexcept
{
    switch (code & 0x07u) {
    case 0u: return Register::B;
    case 1u: return Register::C;
    case 2u: return Register::D;
    case 3u: return Register::E;
    case 4u: return Register::H;
    case 5u: return Register::L;
    case 6u: return std::nullopt;
    case 7u: return Register::A;
    }
    return std::nullopt;
}

ValueId emitReadR8(BlockBuilder& builder, std::uint8_t code)
{
    if (const auto reg = registerForCode(code); reg.has_value()) {
        return builder.emitValue(Opcode::ReadRegister, ValueType::I8,
                                 {Operand::guestRegister(static_cast<std::uint32_t>(*reg),
                                                         ValueType::I8)});
    }
    const auto address = builder.emitValue(
        Opcode::ReadRegister, ValueType::I16,
        {Operand::guestRegister(static_cast<std::uint32_t>(Register::HL), ValueType::I16)});
    return builder.emitValue(Opcode::LoadMemory, ValueType::I8,
                             {Operand::value(address, ValueType::I16)}, MemoryClass::Generic);
}

void emitWriteR8(BlockBuilder& builder, std::uint8_t code, ValueId value)
{
    if (const auto reg = registerForCode(code); reg.has_value()) {
        builder.emit(Opcode::WriteRegister,
                     {Operand::guestRegister(static_cast<std::uint32_t>(*reg), ValueType::I8),
                      Operand::value(value, ValueType::I8)});
        return;
    }
    const auto address = builder.emitValue(
        Opcode::ReadRegister, ValueType::I16,
        {Operand::guestRegister(static_cast<std::uint32_t>(Register::HL), ValueType::I16)});
    builder.emit(Opcode::StoreMemory,
                 {Operand::value(address, ValueType::I16), Operand::value(value, ValueType::I8)},
                 MemoryClass::Generic);
}

void emitNextPc(BlockBuilder& builder, std::uint16_t address)
{
    builder.emit(Opcode::SetProgramCounter,
                 {Operand::immediate(address, ValueType::I16)});
}

bool lowerInstruction(BlockBuilder& builder,
                      const BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>& instruction)
{
    if (instruction.length == 0u) return false;
    const auto opcode = instruction.bytes[0];
    const auto next = static_cast<std::uint16_t>(instruction.address + instruction.length);

    if (opcode == 0x00u) {
        builder.beginInstruction(instruction.address, instruction.length, 4u);
        emitNextPc(builder, next);
        builder.endInstruction();
        return true;
    }

    if (opcode >= 0x40u && opcode <= 0x7Fu && opcode != 0x76u) {
        builder.beginInstruction(instruction.address, instruction.length,
                                 (opcode & 0x07u) == 6u || ((opcode >> 3u) & 0x07u) == 6u ? 8u : 4u);
        const auto value = emitReadR8(builder, static_cast<std::uint8_t>(opcode & 0x07u));
        emitWriteR8(builder, static_cast<std::uint8_t>((opcode >> 3u) & 0x07u), value);
        emitNextPc(builder, next);
        builder.endInstruction();
        return true;
    }

    if ((opcode & 0xC7u) == 0x06u && instruction.length >= 2u) {
        const auto destination = static_cast<std::uint8_t>((opcode >> 3u) & 0x07u);
        builder.beginInstruction(instruction.address, instruction.length,
                                 destination == 6u ? 12u : 8u);
        const auto value = builder.emitValue(
            Opcode::Constant, ValueType::I8,
            {Operand::immediate(instruction.bytes[1], ValueType::I8)});
        emitWriteR8(builder, destination, value);
        emitNextPc(builder, next);
        builder.endInstruction();
        return true;
    }

    if ((opcode & 0xC7u) == 0x04u || (opcode & 0xC7u) == 0x05u) {
        const bool decrement = (opcode & 0xC7u) == 0x05u;
        const auto target = static_cast<std::uint8_t>((opcode >> 3u) & 0x07u);
        builder.beginInstruction(instruction.address, instruction.length, target == 6u ? 12u : 4u);
        const auto oldValue = emitReadR8(builder, target);
        const auto one = builder.emitValue(
            Opcode::Constant, ValueType::I8, {Operand::immediate(1u, ValueType::I8)});
        const auto newValue = builder.emitValue(
            decrement ? Opcode::Subtract : Opcode::Add, ValueType::I8,
            {Operand::value(oldValue, ValueType::I8), Operand::value(one, ValueType::I8)});
        builder.emit(Opcode::CallHelper,
                     {Operand::helper(static_cast<std::uint32_t>(
                          decrement ? Helper::UpdateDecrementFlags : Helper::UpdateIncrementFlags)),
                      Operand::value(oldValue, ValueType::I8),
                      Operand::value(newValue, ValueType::I8)});
        emitWriteR8(builder, target, newValue);
        emitNextPc(builder, next);
        builder.endInstruction();
        return true;
    }

    if (opcode >= 0x80u && opcode <= 0xBFu) {
        const auto source = static_cast<std::uint8_t>(opcode & 0x07u);
        const auto operation = static_cast<AluOperation>((opcode >> 3u) & 0x07u);
        builder.beginInstruction(instruction.address, instruction.length, source == 6u ? 8u : 4u);
        const auto lhs = builder.emitValue(
            Opcode::ReadRegister, ValueType::I8,
            {Operand::guestRegister(static_cast<std::uint32_t>(Register::A), ValueType::I8)});
        const auto rhs = emitReadR8(builder, source);
        const auto value = builder.emitValue(
            Opcode::CallHelper, ValueType::I8,
            {Operand::helper(static_cast<std::uint32_t>(Helper::ExecuteAlu8)),
             Operand::immediate(static_cast<std::uint8_t>(operation), ValueType::I8),
             Operand::value(lhs, ValueType::I8), Operand::value(rhs, ValueType::I8)});
        if (operation != AluOperation::Compare) {
            builder.emit(Opcode::WriteRegister,
                         {Operand::guestRegister(static_cast<std::uint32_t>(Register::A), ValueType::I8),
                          Operand::value(value, ValueType::I8)});
        }
        emitNextPc(builder, next);
        builder.endInstruction();
        return true;
    }

    if (opcode == 0x18u && instruction.length >= 2u) {
        const auto displacement = static_cast<std::int8_t>(instruction.bytes[1]);
        const auto target = static_cast<std::uint16_t>(next + displacement);
        builder.beginInstruction(instruction.address, instruction.length, 12u, true);
        builder.emit(Opcode::Branch, {Operand::blockTarget(target)});
        builder.endInstruction();
        return true;
    }

    return false;
}

} // namespace

bool valid(const ExecutionAbiV1& abi) noexcept
{
    return abi.structSize == sizeof(ExecutionAbiV1) && abi.version == kAbiVersion &&
           abi.opaque != nullptr && abi.readRegister != nullptr && abi.writeRegister != nullptr &&
           abi.readMemory8 != nullptr && abi.writeMemory8 != nullptr &&
           abi.callHelper != nullptr && abi.retireCpuCycles != nullptr &&
           abi.executionState != nullptr;
}

GuardFailure validateGuards(const BMMQ::IR::Block& block,
                            const GuardContext& context) noexcept
{
    for (const auto& guard : block.guards) {
        switch (guard.kind) {
        case BMMQ::IR::GuardKind::MappingGeneration:
            if (guard.expected != context.mappingGeneration ||
                block.mappingGeneration != context.mappingGeneration) {
                return GuardFailure::MappingGeneration;
            }
            break;
        case BMMQ::IR::GuardKind::HelperAbi:
            if (guard.expected != kAbiVersion) return GuardFailure::HelperAbi;
            break;
        case BMMQ::IR::GuardKind::ExecutionState:
            if ((context.executionState & guard.mask) !=
                (guard.expected & guard.mask)) {
                return GuardFailure::ExecutionState;
            }
            break;
        case BMMQ::IR::GuardKind::CodeBytes:
            if (context.opaque == nullptr || context.peekCodeByte == nullptr) {
                return GuardFailure::IneligibleCode;
            }
            for (std::size_t index = 0u; index < guard.bytes.size(); ++index) {
                const auto rawAddress = guard.subject + index;
                if (rawAddress > 0xFFFFu) return GuardFailure::IneligibleCode;
                std::uint8_t byte = 0u;
                if (!context.peekCodeByte(
                        context.opaque, static_cast<std::uint16_t>(rawAddress), byte)) {
                    return GuardFailure::IneligibleCode;
                }
                if (byte != guard.bytes[index]) return GuardFailure::CodeBytes;
            }
            break;
        }
    }
    return GuardFailure::None;
}

InstructionResult PortableExecutor::execute(const BMMQ::IR::GuestInstruction& instruction,
                                            const ExecutionAbiV1& abi)
{
    if (!valid(abi)) throw std::invalid_argument("invalid Game Boy portable IR ABI");
    AbiHost host(abi);
    const auto result = interpreter_.execute(instruction, host);
    return {
        .branchTaken = result.branchTaken,
        .exitRequested = result.exitRequested,
        .cycleCondition = result.cycleCondition,
        .retirementReached = result.retirementReached,
    };
}

BMMQ::IR::BlockPtr lowerBlock(
    std::span<const BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>> instructions,
    std::uint64_t mappingGeneration,
    std::uint64_t executionState)
{
    if (instructions.empty()) return {};
    BlockBuilder builder(instructions.front().address, mappingGeneration);
    builder.addGuard({
        .kind = BMMQ::IR::GuardKind::MappingGeneration,
        .expected = mappingGeneration,
    });
    builder.addGuard({
        .kind = BMMQ::IR::GuardKind::HelperAbi,
        .expected = kAbiVersion,
    });
    builder.addGuard({
        .kind = BMMQ::IR::GuardKind::ExecutionState,
        .expected = executionState,
        .mask = Stop | Halt | DmaRestricted | InterruptPending |
                HaltBugPending | PendingCycleCharge,
    });

    std::vector<std::uint8_t> codeBytes;
    BMMQ::IR::BlockExit exit = BlockExit::Sequential;
    std::size_t lowered = 0u;
    for (const auto& instruction : instructions) {
        if (!lowerInstruction(builder, instruction)) {
            exit = BlockExit::Unsupported;
            break;
        }
        codeBytes.insert(codeBytes.end(), instruction.bytes.begin(),
                         instruction.bytes.begin() + instruction.length);
        ++lowered;
        if (instruction.bytes[0] == 0x18u) {
            exit = BlockExit::ControlFlow;
            break;
        }
    }
    if (lowered == 0u) return {};
    builder.addGuard({
        .kind = BMMQ::IR::GuardKind::CodeBytes,
        .subject = instructions.front().address,
        .bytes = std::move(codeBytes),
    });
    return builder.finish(exit);
}

} // namespace GB::IRExecution
