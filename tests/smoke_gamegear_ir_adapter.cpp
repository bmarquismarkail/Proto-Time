#include <cassert>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cores/gamegear/GameGearIrExecution.hpp"

namespace {

struct CountingArtifact final : BMMQ::BlockBackendArtifact {};

class SupplementalAdapter final : public BMMQ::IR::IIrCoreAdapter {
public:
    std::uint32_t architectureId() const noexcept override
    {
        return BMMQ::GameGearIR::kArchitectureId;
    }
    std::uint32_t irAbiVersion() const noexcept override
    {
        return BMMQ::IR::kIrAbiVersion;
    }
    BMMQ::IR::BlockPtr lower(const BMMQ::IR::LoweringRequest&,
                             std::string*) override
    {
        return {};
    }
    BMMQ::IR::ValidationResult validateBlock(const BMMQ::IR::Block&) const override
    {
        return {};
    }
    std::optional<std::string> validateExecutionState(
        const BMMQ::IR::Block&) const override
    {
        return {};
    }
};

class CountingBackend final : public BMMQ::IR::IIrExecutionBackend {
public:
    bool supports(std::uint32_t architectureId,
                  std::uint32_t abiVersion) const noexcept override
    {
        return architectureId == BMMQ::GameGearIR::kArchitectureId &&
               abiVersion == BMMQ::IR::kIrAbiVersion;
    }
    BMMQ::BlockBackendArtifactPtr compile(const BMMQ::IR::BlockPtr&,
                                          std::string*) override
    {
        ++compileCalls;
        return std::make_shared<CountingArtifact>();
    }
    bool execute(const BMMQ::BlockBackendArtifact&,
                 std::size_t,
                 BMMQ::IR::InterpreterHost&,
                 BMMQ::IR::InterpreterResult*) override
    {
        ++executeCalls;
        return true;
    }

    std::size_t compileCalls = 0u;
    std::size_t executeCalls = 0u;
};

BMMQ::IR::Guard& findGuard(BMMQ::IR::Block& block, BMMQ::IR::GuardKind kind)
{
    for (auto& guard : block.guards) {
        if (guard.kind == kind) return guard;
    }
    assert(false && "required guard not found");
    return block.guards.front();
}

void testHostGuardProfileRejectsBeforeDynamicCompile()
{
    BMMQ::GameGearIR::CoreAdapter hostValidator;
    SupplementalAdapter selectedAdapter;
    const std::vector<BMMQ::IR::SourceInstruction> source{
        {.address = 0x1000u, .bytes = {0x06u, 0x7Fu, 0u, 0u}, .length = 2u},
    };
    const BMMQ::IR::LoweringRequest request{.instructions = source,
                                             .mappingGeneration = 9u,
                                             .executionState = 0u};
    std::string error;
    const auto valid = hostValidator.lower(request, &error);
    assert(valid && error.empty());

    const auto reject = [&](auto mutate) {
        auto malformed = std::make_shared<BMMQ::IR::Block>(*valid);
        mutate(*malformed);
        CountingBackend backend;
        BMMQ::IR::IrExecutionService service(
            selectedAdapter, backend, hostValidator);
        const auto prepared = service.prepare(request, malformed, &error);
        assert(!prepared.prepared && prepared.fallback && !prepared.artifact);
        assert(backend.compileCalls == 0u);
        assert(backend.executeCalls == 0u);
    };

    for (const auto kind : {BMMQ::IR::GuardKind::MappingGeneration,
                            BMMQ::IR::GuardKind::CodeBytes,
                            BMMQ::IR::GuardKind::ExecutionState,
                            BMMQ::IR::GuardKind::HelperAbi}) {
        reject([&](BMMQ::IR::Block& block) {
            std::erase_if(block.guards,
                          [&](const auto& guard) { return guard.kind == kind; });
        });
    }
    reject([](BMMQ::IR::Block& block) { block.guards.push_back(block.guards.front()); });
    reject([](BMMQ::IR::Block& block) {
        block.guards.front().kind = static_cast<BMMQ::IR::GuardKind>(0xFFu);
    });
    reject([](BMMQ::IR::Block& block) {
        auto& guard = findGuard(block, BMMQ::IR::GuardKind::CodeBytes);
        guard.subject = UINT64_MAX;
        guard.bytes = {0x00u, 0x00u};
    });
    reject([](BMMQ::IR::Block& block) {
        findGuard(block, BMMQ::IR::GuardKind::MappingGeneration).bytes = {0x00u};
    });
    reject([](BMMQ::IR::Block& block) {
        ++findGuard(block, BMMQ::IR::GuardKind::MappingGeneration).expected;
    });
    reject([](BMMQ::IR::Block& block) {
        ++findGuard(block, BMMQ::IR::GuardKind::HelperAbi).expected;
    });
    reject([](BMMQ::IR::Block& block) {
        ++findGuard(block, BMMQ::IR::GuardKind::ExecutionState).expected;
    });
    reject([](BMMQ::IR::Block& block) {
        findGuard(block, BMMQ::IR::GuardKind::ExecutionState).mask = 0u;
    });
    reject([](BMMQ::IR::Block& block) {
        ++findGuard(block, BMMQ::IR::GuardKind::CodeBytes).subject;
    });
    reject([](BMMQ::IR::Block& block) {
        findGuard(block, BMMQ::IR::GuardKind::CodeBytes).bytes.front() ^= 0xFFu;
    });

    CountingBackend backend;
    BMMQ::IR::IrExecutionService service(hostValidator, backend, hostValidator);
    error.clear();
    const auto prepared = service.prepare(request, valid, &error);
    assert(prepared.prepared && prepared.artifact && error.empty());
    assert(backend.compileCalls == 1u && backend.executeCalls == 0u);
}

} // namespace

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
    testHostGuardProfileRejectsBeforeDynamicCompile();
    return 0;
}
