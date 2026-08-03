#include <cassert>
#include <string>
#include <vector>

#include "cores/gamegear/GameGearIrExecution.hpp"

int main()
{
    BMMQ::GameGearIR::CoreAdapter adapter;
    assert(adapter.architectureId() == BMMQ::GameGearIR::kArchitectureId);
    assert(adapter.irAbiVersion() == BMMQ::IR::kIrAbiVersion);

    const std::vector<BMMQ::IR::SourceInstruction> source{
        {.address = 0x0100u, .bytes = {0x06u, 0x7Fu, 0u, 0u}, .length = 2u},
        {.address = 0x0102u, .bytes = {0x04u, 0u, 0u, 0u}, .length = 1u},
        {.address = 0x0103u, .bytes = {0x80u, 0u, 0u, 0u}, .length = 1u},
        {.address = 0x0104u, .bytes = {0x18u, 0xFAu, 0u, 0u}, .length = 2u},
    };
    std::string error;
    const auto block = adapter.lower({.instructions = source,
                                      .mappingGeneration = 9u,
                                      .executionState = 0u},
                                     &error);
    assert(block && error.empty());
    assert(BMMQ::IR::validate(*block));
    assert(adapter.validateBlock(*block));
    assert(block->instructions.size() == source.size());
    assert(block->exit == BMMQ::IR::BlockExit::ControlFlow);
    assert(block->guards.size() == 4u);
    assert(block->guards.back().bytes ==
           std::vector<std::uint8_t>({0x06u, 0x7Fu, 0x04u, 0x80u, 0x18u, 0xFAu}));

    const std::vector<BMMQ::IR::SourceInstruction> unsupported{
        {.address = 0x0200u, .bytes = {0x76u, 0u, 0u, 0u}, .length = 1u},
    };
    assert(!adapter.lower({.instructions = unsupported}, &error));
    assert(!error.empty());

    BMMQ::IR::Block malformed = *block;
    for (auto& operation : malformed.instructions.front().operations) {
        if (operation.opcode == BMMQ::IR::Opcode::WriteRegister) {
            operation.operands.clear();
            break;
        }
    }
    assert(!adapter.validateBlock(malformed));

    auto invalidRegister = *block;
    bool registerMutated = false;
    for (auto& instruction : invalidRegister.instructions) {
        for (auto& operation : instruction.operations) {
            if (operation.opcode == BMMQ::IR::Opcode::WriteRegister) {
                operation.operands[0].payload =
                    static_cast<std::uint32_t>(BMMQ::GameGearIR::Register::PC) + 1u;
                registerMutated = true;
                break;
            }
        }
        if (registerMutated) break;
    }
    assert(registerMutated);
    assert(adapter.validateBlock(invalidRegister).message ==
           "Game Gear IR contains an unknown register");

    auto invalidHelper = *block;
    bool helperMutated = false;
    for (auto& instruction : invalidHelper.instructions) {
        for (auto& operation : instruction.operations) {
            if (operation.opcode == BMMQ::IR::Opcode::CallHelper) {
                operation.operands[0].payload =
                    static_cast<std::uint32_t>(BMMQ::GameGearIR::Helper::ExecuteAlu8) + 1u;
                helperMutated = true;
                break;
            }
        }
        if (helperMutated) break;
    }
    assert(helperMutated);
    assert(adapter.validateBlock(invalidHelper).message ==
           "Game Gear IR contains an unknown helper");

    const std::vector<BMMQ::IR::SourceInstruction> memorySource{
        {.address = 0x0300u, .bytes = {0x70u, 0u, 0u, 0u}, .length = 1u},
    };
    error.clear();
    const auto memoryBlock = adapter.lower({.instructions = memorySource}, &error);
    assert(memoryBlock && error.empty());
    auto invalidMemoryType = *memoryBlock;
    bool memoryMutated = false;
    for (auto& instruction : invalidMemoryType.instructions) {
        for (auto& operation : instruction.operations) {
            if (operation.opcode != BMMQ::IR::Opcode::StoreMemory) continue;
            const auto storedValue = operation.operands[1].payload;
            operation.operands[1].type = BMMQ::IR::ValueType::I16;
            for (auto& producer : instruction.operations) {
                if (producer.result == storedValue) {
                    producer.resultType = BMMQ::IR::ValueType::I16;
                    producer.operands[0].type = BMMQ::IR::ValueType::I16;
                }
            }
            memoryMutated = true;
            break;
        }
    }
    assert(memoryMutated);
    assert(adapter.validateBlock(invalidMemoryType).message ==
           "Game Gear IR supports only I8 memory operations");
    return 0;
}
