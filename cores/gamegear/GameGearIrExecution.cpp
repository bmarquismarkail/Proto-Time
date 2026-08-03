#include "GameGearIrExecution.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace BMMQ::GameGearIR {
namespace {

using IR::BlockBuilder;
using IR::BlockExit;
using IR::MemoryClass;
using IR::Opcode;
using IR::Operand;
using IR::SourceInstruction;
using IR::ValueId;
using IR::ValueType;

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

ValueId readR8(BlockBuilder& builder, std::uint8_t code)
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

void writeR8(BlockBuilder& builder, std::uint8_t code, ValueId value)
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

void beginInstruction(BlockBuilder& builder,
                      const SourceInstruction& instruction,
                      std::uint32_t cycles,
                      bool controlFlow = false)
{
    builder.beginInstruction(instruction.address, instruction.length, cycles, controlFlow);
    builder.emit(Opcode::CallHelper,
                 {Operand::helper(static_cast<std::uint32_t>(Helper::IncrementRefresh))});
}

void nextPc(BlockBuilder& builder, std::uint16_t address)
{
    builder.emit(Opcode::SetProgramCounter, {Operand::immediate(address, ValueType::I16)});
}

bool lowerInstruction(BlockBuilder& builder, const SourceInstruction& instruction)
{
    if (instruction.length == 0u || instruction.length > instruction.bytes.size()) return false;
    const auto opcode = instruction.bytes[0];
    const auto next = static_cast<std::uint16_t>(instruction.address + instruction.length);

    if (opcode == 0x00u) {
        beginInstruction(builder, instruction, 4u);
        nextPc(builder, next);
        builder.endInstruction();
        return true;
    }
    if (opcode >= 0x40u && opcode <= 0x7Fu && opcode != 0x76u) {
        const auto source = static_cast<std::uint8_t>(opcode & 0x07u);
        const auto destination = static_cast<std::uint8_t>((opcode >> 3u) & 0x07u);
        beginInstruction(builder, instruction,
                         source == 6u || destination == 6u ? 7u : 4u);
        const auto value = readR8(builder, source);
        writeR8(builder, destination, value);
        nextPc(builder, next);
        builder.endInstruction();
        return true;
    }
    if ((opcode & 0xC7u) == 0x06u && instruction.length >= 2u) {
        const auto destination = static_cast<std::uint8_t>((opcode >> 3u) & 0x07u);
        beginInstruction(builder, instruction, destination == 6u ? 10u : 7u);
        const auto value = builder.emitValue(
            Opcode::Constant, ValueType::I8,
            {Operand::immediate(instruction.bytes[1], ValueType::I8)});
        writeR8(builder, destination, value);
        nextPc(builder, next);
        builder.endInstruction();
        return true;
    }
    if ((opcode & 0xC7u) == 0x04u || (opcode & 0xC7u) == 0x05u) {
        const bool decrement = (opcode & 0xC7u) == 0x05u;
        const auto target = static_cast<std::uint8_t>((opcode >> 3u) & 0x07u);
        beginInstruction(builder, instruction, target == 6u ? 11u : 4u);
        const auto before = readR8(builder, target);
        const auto one = builder.emitValue(
            Opcode::Constant, ValueType::I8, {Operand::immediate(1u, ValueType::I8)});
        const auto after = builder.emitValue(
            decrement ? Opcode::Subtract : Opcode::Add, ValueType::I8,
            {Operand::value(before, ValueType::I8), Operand::value(one, ValueType::I8)});
        builder.emit(Opcode::CallHelper,
                     {Operand::helper(static_cast<std::uint32_t>(
                          decrement ? Helper::UpdateDecrementFlags
                                    : Helper::UpdateIncrementFlags)),
                      Operand::value(before, ValueType::I8),
                      Operand::value(after, ValueType::I8)});
        writeR8(builder, target, after);
        nextPc(builder, next);
        builder.endInstruction();
        return true;
    }
    if (opcode >= 0x80u && opcode <= 0xBFu) {
        const auto source = static_cast<std::uint8_t>(opcode & 0x07u);
        const auto operation = static_cast<AluOperation>((opcode >> 3u) & 0x07u);
        beginInstruction(builder, instruction, source == 6u ? 7u : 4u);
        const auto lhs = builder.emitValue(
            Opcode::ReadRegister, ValueType::I8,
            {Operand::guestRegister(static_cast<std::uint32_t>(Register::A), ValueType::I8)});
        const auto rhs = readR8(builder, source);
        const auto result = builder.emitValue(
            Opcode::CallHelper, ValueType::I8,
            {Operand::helper(static_cast<std::uint32_t>(Helper::ExecuteAlu8)),
             Operand::immediate(static_cast<std::uint8_t>(operation), ValueType::I8),
             Operand::value(lhs, ValueType::I8), Operand::value(rhs, ValueType::I8)});
        if (operation != AluOperation::Compare) writeR8(builder, 7u, result);
        nextPc(builder, next);
        builder.endInstruction();
        return true;
    }
    if (opcode == 0x18u && instruction.length >= 2u) {
        const auto displacement = static_cast<std::int8_t>(instruction.bytes[1]);
        const auto target = static_cast<std::uint16_t>(next + displacement);
        beginInstruction(builder, instruction, 12u, true);
        builder.emit(Opcode::Branch, {Operand::blockTarget(target)});
        builder.endInstruction();
        return true;
    }
    return false;
}

bool validRegister(std::uint64_t id) noexcept
{
    return id <= static_cast<std::uint32_t>(Register::PC);
}

} // namespace

std::uint32_t CoreAdapter::architectureId() const noexcept { return kArchitectureId; }
std::uint32_t CoreAdapter::irAbiVersion() const noexcept { return IR::kIrAbiVersion; }

IR::BlockPtr CoreAdapter::lower(const IR::LoweringRequest& request, std::string* error)
{
    if (request.instructions.empty()) {
        if (error != nullptr) *error = "Game Gear IR lowering request is empty";
        return {};
    }

    BlockBuilder builder(request.instructions.front().address, request.mappingGeneration);
    builder.addGuard({.kind = IR::GuardKind::MappingGeneration,
                      .expected = request.mappingGeneration});
    builder.addGuard({.kind = IR::GuardKind::HelperAbi,
                      .expected = kHelperAbiVersion});
    builder.addGuard({.kind = IR::GuardKind::ExecutionState,
                      .expected = request.executionState,
                      .mask = Halted | InterruptPending | DeferredInterruptEnable});

    std::vector<std::uint8_t> codeBytes;
    auto exit = BlockExit::Sequential;
    std::size_t lowered = 0u;
    for (const auto& instruction : request.instructions) {
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
    if (lowered == 0u) {
        if (error != nullptr) *error = "Game Gear opcode is not supported by IR";
        return {};
    }
    builder.addGuard({.kind = IR::GuardKind::CodeBytes,
                      .subject = request.instructions.front().address,
                      .bytes = std::move(codeBytes)});
    return builder.finish(exit);
}

IR::ValidationResult CoreAdapter::validateBlock(const IR::Block& block) const
{
    const auto hostValidation = IR::validate(block);
    if (!hostValidation) return hostValidation;

    for (std::size_t instructionIndex = 0u;
         instructionIndex < block.instructions.size(); ++instructionIndex) {
        const auto& instruction = block.instructions[instructionIndex];
        for (std::size_t operationIndex = 0u;
             operationIndex < instruction.operations.size(); ++operationIndex) {
            const auto& operation = instruction.operations[operationIndex];
            if ((operation.opcode == IR::Opcode::LoadMemory ||
                 operation.opcode == IR::Opcode::StoreMemory) &&
                ((operation.opcode == IR::Opcode::LoadMemory &&
                  operation.resultType != IR::ValueType::I8) ||
                 (operation.opcode == IR::Opcode::StoreMemory &&
                  operation.operands[1].type != IR::ValueType::I8))) {
                return {.valid = false,
                        .instructionIndex = instructionIndex,
                        .operationIndex = operationIndex,
                        .message = "Game Gear IR supports only I8 memory operations"};
            }
            for (const auto& operand : operation.operands) {
                if (operand.kind == IR::OperandKind::GuestRegister &&
                    !validRegister(operand.payload)) {
                    return {.valid = false,
                            .instructionIndex = instructionIndex,
                            .operationIndex = operationIndex,
                            .message = "Game Gear IR contains an unknown register"};
                }
                if (operand.kind == IR::OperandKind::Helper &&
                    operand.payload > static_cast<std::uint32_t>(Helper::ExecuteAlu8)) {
                    return {.valid = false,
                            .instructionIndex = instructionIndex,
                            .operationIndex = operationIndex,
                            .message = "Game Gear IR contains an unknown helper"};
                }
            }
        }
    }
    return {};
}

std::optional<std::string> CoreAdapter::validateExecutionState(const IR::Block& block) const
{
    if (opaque_ == nullptr || executionState_ == nullptr) return {};
    const auto current = executionState_(opaque_);
    for (const auto& guard : block.guards) {
        if (guard.kind == IR::GuardKind::ExecutionState &&
            (current & guard.mask) != (guard.expected & guard.mask)) {
            return "Game Gear execution state guard changed";
        }
    }
    return {};
}

} // namespace BMMQ::GameGearIR
