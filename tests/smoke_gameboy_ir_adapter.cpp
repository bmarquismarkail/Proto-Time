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
}

void testNoBoundAbiDoesNotWeakenRuntimeGuardPath()
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
                mutated = true;
            }
        }
    }
    assert(mutated && "no LoadMemory found to mutate");

    const auto validation = adapter.validateBlock(malformed);
    assert(!validation);
}

void testLowerBlockRejectsInvalidSourceInstruction()
{
    const auto result = GB::IRExecution::lowerBlock(
        std::span<const BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>>{}, 7u, 0u);
    assert(!result);
}

} // namespace

int main()
{
    testAdapterIdentity();
    testAdapterLoweringParity();
    testNoBoundAbiDoesNotWeakenRuntimeGuardPath();
    testMalformedBlockRejection();
    testInvalidRegisterIdRejection();
    testI16MemoryOperationRejection();
    testLowerBlockRejectsInvalidSourceInstruction();
    return 0;
}
