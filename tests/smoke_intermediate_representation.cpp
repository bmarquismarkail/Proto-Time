#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "inst_cycle/IntermediateRepresentation.hpp"

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

} // namespace

int main()
{
    testValidatedBlockShape();
    testRejectsMissingRetirement();
    testRejectsCrossInstructionTemporary();
    testBuilderRejectsMalformedConditionalCycles();
    return 0;
}
