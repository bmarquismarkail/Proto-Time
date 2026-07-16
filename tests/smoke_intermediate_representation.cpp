#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "inst_cycle/IntermediateRepresentation.hpp"
#include "inst_cycle/IntermediateRepresentationInterpreter.hpp"

namespace {

using namespace BMMQ::IR;

BlockPtr makeConditionalBlock()
{
    BlockBuilder builder(0x0200u, 7u);
    builder.addGuard({.kind = GuardKind::HelperAbi, .expected = 1u});
    builder.beginInstruction(0x0200u, 2u, 8u, 12u, std::nullopt, true);
    const auto flags = builder.emitValue(
        Opcode::ReadRegister, ValueType::I8,
        {Operand::guestRegister(1u, ValueType::I8)});
    const auto zero = builder.emitValue(
        Opcode::Constant, ValueType::I8,
        {Operand::immediate(0u, ValueType::I8)});
    const auto taken = builder.emitValue(
        Opcode::CompareNotEqual, ValueType::Bool,
        {Operand::value(flags, ValueType::I8), Operand::value(zero, ValueType::I8)});
    builder.setTakenCondition(taken);
    builder.emit(Opcode::BranchIf,
                 {Operand::value(taken, ValueType::Bool), Operand::blockTarget(0x01FFu)});
    builder.endInstruction();
    return builder.finish(BlockExit::ControlFlow);
}

void testValidatedBlockShape()
{
    const auto block = makeConditionalBlock();
    assert(block);
    assert(validate(*block));
    assert(block->guestStart == 0x0200u);
    assert(block->guestEnd == 0x0201u);
    assert(block->mappingGeneration == 7u);
    assert(block->instructions.size() == 1u);
    assert(block->instructions.front().operations.back().opcode == Opcode::RetireInstruction);
    assert(block->instructions.front().takenCondition.has_value());
}

void testRejectsMissingRetirement()
{
    Block block;
    block.guestStart = 0x1000u;
    block.guestEnd = 0x1000u;
    block.instructions.push_back({
        .address = 0x1000u,
        .length = 1u,
        .cyclesNotTaken = 4u,
        .cyclesTaken = 4u,
        .operations = {{.opcode = Opcode::Exit}},
    });
    const auto result = validate(block);
    assert(!result);
}

void testRejectsCrossInstructionTemporary()
{
    Block block;
    block.guestStart = 0x2000u;
    block.guestEnd = 0x2001u;
    block.instructions.push_back({
        .address = 0x2000u,
        .length = 1u,
        .cyclesNotTaken = 4u,
        .cyclesTaken = 4u,
        .operations = {
            {.opcode = Opcode::Constant,
             .result = 1u,
             .resultType = ValueType::I8,
             .operands = {Operand::immediate(1u, ValueType::I8)}},
            {.opcode = Opcode::RetireInstruction},
        },
    });
    block.instructions.push_back({
        .address = 0x2001u,
        .length = 1u,
        .cyclesNotTaken = 4u,
        .cyclesTaken = 4u,
        .operations = {
            {.opcode = Opcode::WriteRegister,
             .operands = {Operand::guestRegister(0u, ValueType::I8),
                          Operand::value(1u, ValueType::I8)}},
            {.opcode = Opcode::RetireInstruction},
        },
    });
    const auto result = validate(block);
    assert(!result);
}

void testBuilderRejectsMalformedConditionalCycles()
{
    bool threw = false;
    try {
        BlockBuilder builder(0x3000u);
        builder.beginInstruction(0x3000u, 1u, 4u, 8u, std::nullopt);
        builder.endInstruction();
        (void)builder.finish();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void testPortableInterpreterUsesHostAbiAndReportsBranch()
{
    struct Host final : InterpreterHost {
        std::uint64_t readRegister(std::uint32_t, ValueType) override { return reg; }
        void writeRegister(std::uint32_t, ValueType, std::uint64_t value) override { reg = value; }
        std::uint64_t loadMemory(std::uint64_t, ValueType, MemoryClass) override { return memory; }
        void storeMemory(std::uint64_t, ValueType, MemoryClass, std::uint64_t value) override {
            memory = value;
        }
        std::uint64_t callHelper(std::uint32_t, ValueType,
                                 std::span<const std::uint64_t> arguments) override {
            return arguments.empty() ? 0u : arguments.front();
        }
        void setProgramCounter(std::uint64_t address) override { pc = address; }

        std::uint64_t reg = 1u;
        std::uint64_t memory = 0u;
        std::uint64_t pc = 0u;
    } host;

    const auto block = makeConditionalBlock();
    Interpreter interpreter;
    const auto taken = interpreter.execute(block->instructions.front(), host);
    assert(taken.branchTaken);
    assert(taken.cycleCondition);
    assert(host.pc == 0x01FFu);

    host.reg = 0u;
    host.pc = 0u;
    const auto notTaken = interpreter.execute(block->instructions.front(), host);
    assert(!notTaken.branchTaken);
    assert(!notTaken.cycleCondition);
    assert(host.pc == 0u);
    assert(notTaken.retirementReached);
}

void testPortableSignedAndWideShiftSemantics()
{
    struct Host final : InterpreterHost {
        std::uint64_t readRegister(std::uint32_t id, ValueType) override { return regs[id]; }
        void writeRegister(std::uint32_t id, ValueType, std::uint64_t value) override {
            regs[id] = value;
        }
        std::uint64_t loadMemory(std::uint64_t, ValueType, MemoryClass) override { return 0u; }
        void storeMemory(std::uint64_t, ValueType, MemoryClass, std::uint64_t) override {}
        std::uint64_t callHelper(std::uint32_t, ValueType,
                                 std::span<const std::uint64_t>) override { return 0u; }
        void setProgramCounter(std::uint64_t) override {}
        std::uint64_t regs[4]{};
    } host;

    BlockBuilder builder(0x4000u);
    builder.beginInstruction(0x4000u, 1u, 4u);
    const auto negative = builder.emitValue(
        Opcode::Constant, ValueType::I8,
        {Operand::immediate(0x80u, ValueType::I8)});
    const auto one = builder.emitValue(
        Opcode::Constant, ValueType::I8,
        {Operand::immediate(1u, ValueType::I8)});
    const auto width = builder.emitValue(
        Opcode::Constant, ValueType::I8,
        {Operand::immediate(8u, ValueType::I8)});
    const auto positive = builder.emitValue(
        Opcode::Constant, ValueType::I8,
        {Operand::immediate(0x7Fu, ValueType::I8)});
    const auto arithmetic = builder.emitValue(
        Opcode::ShiftRightArithmetic, ValueType::I8,
        {Operand::value(negative, ValueType::I8), Operand::value(one, ValueType::I8)});
    const auto signFill = builder.emitValue(
        Opcode::ShiftRightArithmetic, ValueType::I8,
        {Operand::value(negative, ValueType::I8), Operand::value(width, ValueType::I8)});
    const auto logical = builder.emitValue(
        Opcode::ShiftRightLogical, ValueType::I8,
        {Operand::value(negative, ValueType::I8), Operand::value(width, ValueType::I8)});
    const auto signedLess = builder.emitValue(
        Opcode::CompareSignedLess, ValueType::Bool,
        {Operand::value(negative, ValueType::I8), Operand::value(positive, ValueType::I8)});
    builder.emit(Opcode::WriteRegister,
                 {Operand::guestRegister(0u, ValueType::I8),
                  Operand::value(arithmetic, ValueType::I8)});
    builder.emit(Opcode::WriteRegister,
                 {Operand::guestRegister(1u, ValueType::I8),
                  Operand::value(signFill, ValueType::I8)});
    builder.emit(Opcode::WriteRegister,
                 {Operand::guestRegister(2u, ValueType::I8),
                  Operand::value(logical, ValueType::I8)});
    builder.emit(Opcode::WriteRegister,
                 {Operand::guestRegister(3u, ValueType::Bool),
                  Operand::value(signedLess, ValueType::Bool)});
    builder.endInstruction();
    const auto block = builder.finish();

    Interpreter interpreter;
    const auto result = interpreter.execute(block->instructions.front(), host);
    assert(result.retirementReached);
    assert(host.regs[0] == 0xC0u);
    assert(host.regs[1] == 0xFFu);
    assert(host.regs[2] == 0x00u);
    assert(host.regs[3] == 0x01u);
}

void testExitTerminatesAtRetirementBoundary()
{
    struct Host final : InterpreterHost {
        std::uint64_t readRegister(std::uint32_t, ValueType) override { return 0u; }
        void writeRegister(std::uint32_t, ValueType, std::uint64_t) override {}
        std::uint64_t loadMemory(std::uint64_t, ValueType, MemoryClass) override { return 0u; }
        void storeMemory(std::uint64_t, ValueType, MemoryClass, std::uint64_t) override {}
        std::uint64_t callHelper(std::uint32_t, ValueType,
                                 std::span<const std::uint64_t>) override { return 0u; }
        void setProgramCounter(std::uint64_t) override {}
    } host;

    BlockBuilder builder(0x5000u);
    builder.beginInstruction(0x5000u, 1u, 4u);
    builder.emit(Opcode::Exit);
    builder.endInstruction();
    const auto block = builder.finish();
    Interpreter interpreter;
    const auto result = interpreter.execute(block->instructions.front(), host);
    assert(result.exitRequested);
    assert(result.retirementReached);

    auto malformed = *block;
    malformed.instructions.front().operations.insert(
        malformed.instructions.front().operations.begin(),
        Operation{.opcode = Opcode::Exit});
    assert(!validate(malformed));
}

} // namespace

int main()
{
    testValidatedBlockShape();
    testRejectsMissingRetirement();
    testRejectsCrossInstructionTemporary();
    testBuilderRejectsMalformedConditionalCycles();
    testPortableInterpreterUsesHostAbiAndReportsBranch();
    testPortableSignedAndWideShiftSemantics();
    testExitTerminatesAtRetirementBoundary();
    return 0;
}
