#ifndef BMMQ_GAMEBOY_IR_EXECUTION_HPP
#define BMMQ_GAMEBOY_IR_EXECUTION_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "../../inst_cycle/BlockTranslator.hpp"
#include "../../inst_cycle/IntermediateRepresentation.hpp"
#include "../../inst_cycle/IntermediateRepresentationInterpreter.hpp"
#include "../../inst_cycle/IrExecutionService.hpp"

namespace GB::IRExecution {

inline constexpr std::uint32_t kAbiVersion = 1u;

// Numeric values are part of the portable IR ABI. Never derive them from C++
// object layout or the host architecture's register numbering.
enum class Register : std::uint32_t {
    A = 0u,
    F,
    B,
    C,
    D,
    E,
    H,
    L,
    AF,
    BC,
    DE,
    HL,
    SP,
    PC,
};

enum class Helper : std::uint32_t {
    UpdateIncrementFlags = 0u,
    UpdateDecrementFlags,
    ExecuteAlu8,
};

enum class AluOperation : std::uint8_t {
    Add = 0u,
    AddCarry,
    Subtract,
    SubtractCarry,
    And,
    Xor,
    Or,
    Compare,
};

enum ExecutionState : std::uint64_t {
    Stop = 1u << 0u,
    Halt = 1u << 1u,
    DmaRestricted = 1u << 2u,
    InterruptPending = 1u << 3u,
    HaltBugPending = 1u << 4u,
    PendingCycleCharge = 1u << 5u,
};

inline constexpr std::uint64_t kAllExecutionStateBits =
    Stop | Halt | DmaRestricted | InterruptPending | HaltBugPending |
    PendingCycleCharge;

enum class GuardFailure : std::uint8_t {
    None,
    MappingGeneration,
    HelperAbi,
    ExecutionState,
    CodeBytes,
    IneligibleCode,
};

struct GuardContext {
    std::uint64_t mappingGeneration = 0u;
    std::uint64_t executionState = 0u;
    const void* opaque = nullptr;
    bool (*peekCodeByte)(const void*, std::uint16_t, std::uint8_t&) noexcept = nullptr;
};

[[nodiscard]] GuardFailure validateGuards(
    const BMMQ::IR::Block& block,
    const GuardContext& context) noexcept;
[[nodiscard]] GuardFailure validateContinuationGuards(
    const BMMQ::IR::Block& block,
    std::uint64_t mappingGeneration,
    std::uint64_t executionState) noexcept;

// Versioned helper table shared by the portable interpreter and future native
// backends. All callbacks operate on the emulation lane and on opaque CPU state.
struct ExecutionAbiV1 {
    std::uint32_t structSize = sizeof(ExecutionAbiV1);
    std::uint32_t version = kAbiVersion;
    void* opaque = nullptr;
    std::uint64_t (*readRegister)(void*, Register) = nullptr;
    void (*writeRegister)(void*, Register, std::uint64_t) = nullptr;
    std::uint8_t (*readMemory8)(void*, std::uint16_t) = nullptr;
    void (*writeMemory8)(void*, std::uint16_t, std::uint8_t) = nullptr;
    std::uint64_t (*callHelper)(void*, Helper, const std::uint64_t*, std::size_t) = nullptr;
    void (*retireCpuCycles)(void*, std::uint32_t) = nullptr;
    std::uint64_t (*executionState)(void*) = nullptr;
};

[[nodiscard]] bool valid(const ExecutionAbiV1& abi) noexcept;

struct InstructionResult {
    bool branchTaken = false;
    bool exitRequested = false;
    bool cycleCondition = false;
    bool retirementReached = false;
};

// Adapts the stable Game Boy execution ABI to the shared backend host without
// exposing the CPU implementation or C++ object layout to a backend.
[[nodiscard]] bool executePreparedInstruction(
    const BMMQ::IR::IrExecutionService& service,
    const BMMQ::IR::Block& block,
    const BMMQ::BlockBackendArtifact& artifact,
    std::size_t instructionIndex,
    const ExecutionAbiV1& abi,
    InstructionResult* result);

class GameBoyCoreAdapter final : public BMMQ::IR::IIrCoreAdapter {
public:
    static constexpr std::uint32_t kArchitectureId = 0x47420001u;

    [[nodiscard]] std::uint32_t architectureId() const noexcept override { return kArchitectureId; }
    [[nodiscard]] std::uint32_t irAbiVersion() const noexcept override { return BMMQ::IR::kIrAbiVersion; }

    [[nodiscard]] BMMQ::IR::BlockPtr lower(const BMMQ::IR::LoweringRequest& request,
                                           std::string* error) override;
    [[nodiscard]] BMMQ::IR::ValidationResult validateBlock(const BMMQ::IR::Block& block) const override;
    [[nodiscard]] BMMQ::IR::ValidationResult validateLoweredBlock(
        const BMMQ::IR::LoweringRequest& request,
        const BMMQ::IR::Block& block) const override;
    [[nodiscard]] std::optional<std::string>
    validateExecutionState(const BMMQ::IR::Block& block) const override;

private:
    [[nodiscard]] static bool registerIdIsValid(std::uint32_t id) noexcept;
    [[nodiscard]] static bool helperIdIsValid(std::uint32_t id) noexcept;
    [[nodiscard]] static bool memoryOpIsI8(const BMMQ::IR::Operation& operation) noexcept;
};

// Compatibility wrapper kept for existing production and test call sites. It
// shares the adapter's lowering implementation so semantics cannot diverge.
[[nodiscard]] BMMQ::IR::BlockPtr lowerBlock(
    std::span<const BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t>> instructions,
    std::uint64_t mappingGeneration,
    std::uint64_t executionState);

} // namespace GB::IRExecution

#endif // BMMQ_GAMEBOY_IR_EXECUTION_HPP
