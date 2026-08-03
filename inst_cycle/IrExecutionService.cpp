#include "IrExecutionService.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace BMMQ::IR {
namespace {

class PortableBlockArtifact final : public BlockBackendArtifact {
public:
    explicit PortableBlockArtifact(BlockPtr source) : block(std::move(source)) {}
    BlockPtr block;
};

class PreparedBlockArtifact final : public BlockBackendArtifact {
public:
    PreparedBlockArtifact(BlockPtr source, BlockBackendArtifactPtr compiled)
        : block(std::move(source)), backendArtifact(std::move(compiled)) {}
    BlockPtr block;
    BlockBackendArtifactPtr backendArtifact;
};

} // namespace

bool PortableIrExecutionBackend::supports(std::uint32_t architectureId,
                                          std::uint32_t irAbiVersion) const noexcept
{
    return architectureId != kAnyArchitecture && irAbiVersion == kIrAbiVersion;
}

BlockBackendArtifactPtr PortableIrExecutionBackend::compile(const BlockPtr& block,
                                                            std::string* error)
{
    if (!block) {
        if (error != nullptr) *error = "portable IR backend received no block";
        return {};
    }
    const auto validation = validate(*block);
    if (!validation) {
        if (error != nullptr) *error = validation.message;
        return {};
    }
    return std::make_shared<const PortableBlockArtifact>(block);
}

bool PortableIrExecutionBackend::execute(const BlockBackendArtifact& artifact,
                                         std::size_t instructionIndex,
                                         InterpreterHost& host,
                                         InterpreterResult* result)
{
    const auto* portable = dynamic_cast<const PortableBlockArtifact*>(&artifact);
    if (portable == nullptr || !portable->block ||
        instructionIndex >= portable->block->instructions.size()) {
        return false;
    }
    const auto executed = interpreter_.execute(portable->block->instructions[instructionIndex],
                                               host);
    if (result != nullptr) *result = executed;
    return true;
}

IrExecutionService::IrExecutionService(IIrCoreAdapter& adapter,
                                       IIrExecutionBackend& backend) noexcept
    : adapter_(adapter), backend_(backend)
{
}

bool IrExecutionService::checkLimits(const Block& block) noexcept
{
    if (block.instructions.empty() || block.guards.size() > Limits::kMaxGuards ||
        block.instructions.size() > Limits::kMaxInstructions) {
        return false;
    }
    std::size_t guardBytes = 0u;
    for (const auto& guard : block.guards) {
        if (guard.bytes.size() > Limits::kMaxGuestBytes - guardBytes) return false;
        guardBytes += guard.bytes.size();
    }

    std::size_t guestBytes = 0u;
    for (const auto& instruction : block.instructions) {
        if (instruction.length > Limits::kMaxGuestBytes - guestBytes) return false;
        if (instruction.cyclesNotTaken > Limits::kMaxCyclesPerInstruction ||
            instruction.cyclesTaken > Limits::kMaxCyclesPerInstruction) return false;
        guestBytes += instruction.length;
        if (instruction.operations.size() > Limits::kMaxOpsPerInstruction) return false;

        ValueId maxValue = 0u;
        for (const auto& operation : instruction.operations) {
            if (operation.operands.size() > Limits::kMaxOperandsPerOp) return false;
            if (operation.result.has_value()) maxValue = std::max(maxValue, *operation.result);
            for (const auto& operand : operation.operands) {
                if (operand.kind == OperandKind::Value) {
                    maxValue = std::max(maxValue, static_cast<ValueId>(operand.payload));
                }
            }
        }
        if (maxValue > Limits::kMaxValues) return false;
    }
    return guestBytes <= Limits::kMaxGuestBytes;
}

IrExecutionService::PrepareResult IrExecutionService::fallback(std::string message,
                                                               std::string* error)
{
    if (error != nullptr) *error = message;
    return {.prepared = false,
            .fallback = true,
            .message = std::move(message),
            .artifact = {}};
}

IrExecutionService::PrepareResult IrExecutionService::prepare(const BlockPtr& block,
                                                              std::string* error) const
{
    if (!block) return fallback("IR adapter produced no block", error);
    if (adapter_.irAbiVersion() != kIrAbiVersion) {
        return fallback("IR adapter ABI version is incompatible with the host", error);
    }
    if (!backend_.supports(adapter_.architectureId(), adapter_.irAbiVersion())) {
        return fallback("IR backend does not support the selected adapter architecture and ABI",
                        error);
    }
    if (!checkLimits(*block)) return fallback("IR block exceeds host limits", error);

    BlockBackendArtifactPtr artifact;
    try {
        const auto hostValidation = validate(*block);
        if (!hostValidation) return fallback(hostValidation.message, error);

        const auto adapterValidation = adapter_.validateBlock(*block);
        if (!adapterValidation) return fallback(adapterValidation.message, error);

        if (const auto stateIssue = adapter_.validateExecutionState(*block);
            stateIssue.has_value()) {
            return fallback(*stateIssue, error);
        }

        std::string compileError;
        artifact = backend_.compile(block, &compileError);
        if (!artifact) {
            return fallback(compileError.empty() ? "IR backend declined the block" : compileError,
                            error);
        }
    } catch (const std::exception& exception) {
        return fallback(std::string("IR preparation failed: ") + exception.what(), error);
    } catch (...) {
        return fallback("IR preparation failed with an unknown error", error);
    }

    return {.prepared = true,
            .fallback = false,
            .message = {},
            .artifact = std::make_shared<const PreparedBlockArtifact>(
                block, std::move(artifact))};
}

bool IrExecutionService::tryExecute(const Block& block,
                                    const BlockBackendArtifact& artifact,
                                    std::size_t instructionIndex,
                                    InterpreterHost& host,
                                    InterpreterResult* out) const
{
    const auto* prepared = dynamic_cast<const PreparedBlockArtifact*>(&artifact);
    if (prepared == nullptr || !prepared->block || !prepared->backendArtifact) {
        throw std::logic_error("IR execution service received an unprepared artifact");
    }
    if (prepared->block.get() != &block) {
        throw std::logic_error("IR execution block does not match its prepared artifact");
    }
    if (instructionIndex >= prepared->block->instructions.size()) {
        throw std::logic_error("IR execution service instruction index out of range");
    }

    try {
        if (adapter_.validateExecutionState(*prepared->block).has_value()) return false;
    } catch (...) {
        return false;
    }

    InterpreterResult result{};
    bool executed = false;
    try {
        executed = backend_.execute(
            *prepared->backendArtifact, instructionIndex, host, &result);
    } catch (const std::exception& exception) {
        throw std::runtime_error(
            std::string("IR backend threw after execution began: ") + exception.what());
    } catch (...) {
        throw std::runtime_error(
            "IR backend threw a non-standard exception after execution began");
    }
    if (!executed) {
        throw std::runtime_error("IR backend failed after execution began");
    }
    if (!result.retirementReached) {
        throw std::runtime_error("IR backend returned without instruction retirement");
    }

    if (out != nullptr) *out = result;
    return true;
}

} // namespace BMMQ::IR
