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
    return 0;
}
