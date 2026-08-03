#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "cores/gameboy/GameBoyIrExecution.hpp"
#include "inst_cycle/IntermediateRepresentation.hpp"

namespace {

using BMMQ::IR::BlockBuilder;
using BMMQ::IR::BlockExit;
using BMMQ::IR::MemoryClass;
using BMMQ::IR::Opcode;
using BMMQ::IR::Operand;
using GB::IRExecution::GameBoyCoreAdapter;
using GB::IRExecution::Helper;

std::vector<BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>> makeInstructions()
{
    return {
        BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>{
            .address = 0xC000u,
            .bytes = {0x00u, 0x00u, 0x00u},
            .length = 1u,
        },
        BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>{
            .address = 0xC001u,
            .bytes = {0x06u, 0x12u, 0x00u},
            .length = 2u,
        },
    };
}

void testAdapterIdentity()
{
    const GameBoyCoreAdapter adapter;
    assert(adapter.architectureId() == GameBoyCoreAdapter::kArchitectureId);
    assert(adapter.irAbiVersion() == BMMQ::IR::kIrAbiVersion);
}

void testAdapterLoweringParity()
{
    const auto instructions = makeInstructions();
    const auto legacy = GB::IRExecution::lowerBlock(instructions, 7u, 0u);
    assert(legacy);

    GameBoyCoreAdapter adapter;
    std::vector<BMMQ::IR::SourceInstruction> copied;
    copied.reserve(instructions.size());
    for (const auto& instruction : instructions) {
        copied.push_back({
            .address = instruction.address,
            .bytes = {},
            .length = instruction.length,
        });
        std::copy(instruction.bytes.begin(),
                  instruction.bytes.begin() + instruction.length,
                  copied.back().bytes.begin());
    }
    BMMQ::IR::LoweringRequest request{};
    request.instructions = {copied.data(), copied.size()};
    request.mappingGeneration = 7u;
    request.executionState = 0u;
    std::string error;
    const auto modern = adapter.lower(request, &error);
    assert(modern);
    assert(error.empty());
    assert(modern->guestStart == legacy->guestStart);
    assert(modern->guestEnd == legacy->guestEnd);
    assert(modern->instructions.size() == legacy->instructions.size());
    assert(modern->exit == legacy->exit);
    assert(modern->guards.size() == legacy->guards.size());
    for (std::size_t index = 0u; index < modern->guards.size(); ++index) {
        assert(modern->guards[index].kind == legacy->guards[index].kind);
        if (modern->guards[index].kind == BMMQ::IR::GuardKind::MappingGeneration) {
            assert(modern->guards[index].expected == legacy->guards[index].expected);
        }
    }
}

void testExecutionStateRuntimeGuardPath()
{
    GameBoyCoreAdapter adapter;
    std::vector<BMMQ::IR::SourceInstruction> copied;
    copied.push_back({.address = 0xC000u, .bytes = {0x00u, 0u, 0u}, .length = 1u});
    BMMQ::IR::LoweringRequest request{};
    request.instructions = {copied.data(), copied.size()};
    request.mappingGeneration = 7u;
    request.executionState = 0u;
    std::string error;
    const auto block = adapter.lower(request, &error);
    assert(block);
    assert(adapter.validateExecutionState(*block) == std::nullopt);
    assert(GB::IRExecution::validateContinuationGuards(*block, 7u, 1u) ==
           GB::IRExecution::GuardFailure::ExecutionState);
}

void testMalformedBlockRejection()
{
    std::vector<BMMQ::IR::SourceInstruction> copied;
    copied.push_back({.address = 0xC000u, .bytes = {0x80u, 0u, 0u}, .length = 1u});
    BMMQ::IR::LoweringRequest request{};
    request.instructions = {copied.data(), copied.size()};
    request.mappingGeneration = 7u;
    request.executionState = 0u;
    std::string error;
    GameBoyCoreAdapter adapter;
    const auto validBlock = adapter.lower(request, &error);
    assert(validBlock);
    assert(error.empty());

    BMMQ::IR::Block malformed = *validBlock;
    bool mutated = false;
    for (auto& instruction : malformed.instructions) {
        for (auto& operation : instruction.operations) {
            if (operation.opcode == BMMQ::IR::Opcode::CallHelper) {
                operation.operands[0].payload = 99u;
                mutated = true;
            }
        }
    }
    assert(mutated && "no CallHelper found to mutate");

    const auto validation = adapter.validateBlock(malformed);
    assert(!validation);
    assert(validation.message == "Game Boy IR block uses an invalid helper ID");
}

void testInvalidRegisterIdRejection()
{
    std::vector<BMMQ::IR::SourceInstruction> copied;
    copied.push_back({.address = 0xC000u, .bytes = {0x7Cu, 0u, 0u}, .length = 1u});
    BMMQ::IR::LoweringRequest request{};
    request.instructions = {copied.data(), copied.size()};
    request.mappingGeneration = 7u;
    request.executionState = 0u;
    std::string error;
    GameBoyCoreAdapter adapter;
    const auto validBlock = adapter.lower(request, &error);
    assert(validBlock);
    assert(error.empty());

    BMMQ::IR::Block malformed = *validBlock;
    bool mutated = false;
    for (auto& instruction : malformed.instructions) {
        for (auto& operation : instruction.operations) {
            if (operation.opcode == BMMQ::IR::Opcode::ReadRegister) {
                operation.operands[0].payload = 99u;
                mutated = true;
            }
        }
    }
    assert(mutated && "no ReadRegister found to mutate");

    const auto validation = adapter.validateBlock(malformed);
    assert(!validation);
    assert(validation.message == "Game Boy IR block uses an invalid register ID");
}

void testI16MemoryOperationRejection()
{
    std::vector<BMMQ::IR::SourceInstruction> copied;
    copied.push_back({.address = 0xC000u, .bytes = {0x7Eu, 0u, 0u}, .length = 1u});
    BMMQ::IR::LoweringRequest request{};
    request.instructions = {copied.data(), copied.size()};
    request.mappingGeneration = 7u;
    request.executionState = 0u;
    std::string error;
    GameBoyCoreAdapter adapter;
    const auto validBlock = adapter.lower(request, &error);
    assert(validBlock);
    assert(error.empty());

    BMMQ::IR::Block malformed = *validBlock;
    bool mutated = false;
    for (auto& instruction : malformed.instructions) {
        for (auto& operation : instruction.operations) {
            if (operation.opcode == BMMQ::IR::Opcode::LoadMemory) {
                operation.resultType = BMMQ::IR::ValueType::I16;
                const auto result = *operation.result;
                for (auto& consumer : instruction.operations) {
                    bool consumesResult = false;
                    for (auto& operand : consumer.operands) {
                        if (operand.kind == BMMQ::IR::OperandKind::Value &&
                            operand.payload == result) {
                            operand.type = BMMQ::IR::ValueType::I16;
                            consumesResult = true;
                        }
                    }
                    if (consumesResult &&
                        consumer.opcode == BMMQ::IR::Opcode::WriteRegister) {
                        consumer.operands[0].type = BMMQ::IR::ValueType::I16;
                    }
                }
                mutated = true;
            }
        }
    }
    assert(mutated && "no LoadMemory found to mutate");

    const auto validation = adapter.validateBlock(malformed);
    assert(!validation);
    assert(validation.message == "Game Boy IR block only supports I8 memory operations");
}

void testLoweringRejectsEmptyAndInvalidLengthInputs()
{
    const auto emptyResult = GB::IRExecution::lowerBlock(
        std::span<const BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>>{}, 7u, 0u);
    assert(!emptyResult);

    GameBoyCoreAdapter adapter;
    std::string error;
    BMMQ::IR::LoweringRequest emptyRequest{};
    assert(!adapter.lower(emptyRequest, &error));
    assert(error == "Game Boy adapter received no source instructions");

    for (const auto length : {std::uint8_t{0u}, std::uint8_t{4u}}) {
        const std::vector<BMMQ::IR::SourceInstruction> source{
            {.address = 0xC000u, .bytes = {0x00u, 0u, 0u, 0u}, .length = length}};
        BMMQ::IR::LoweringRequest request{};
        request.instructions = source;
        error.clear();
        assert(!adapter.lower(request, &error));
        assert(error == "Game Boy adapter received an invalid source instruction");

        const BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t> instruction{
            .address = 0xC000u,
            .bytes = {0x00u, 0u, 0u},
            .length = length,
        };
        assert(!GB::IRExecution::lowerBlock(
            std::span<const BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>>{
                &instruction, 1u},
            7u, 0u));
    }
}

} // namespace

int main()
{
    testAdapterIdentity();
    testAdapterLoweringParity();
    testExecutionStateRuntimeGuardPath();
    testMalformedBlockRejection();
    testInvalidRegisterIdRejection();
    testI16MemoryOperationRejection();
    testLoweringRejectsEmptyAndInvalidLengthInputs();
    return 0;
}
