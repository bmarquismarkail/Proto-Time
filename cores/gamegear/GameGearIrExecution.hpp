#ifndef BMMQ_GAMEGEAR_IR_EXECUTION_HPP
#define BMMQ_GAMEGEAR_IR_EXECUTION_HPP

#include <cstdint>
#include <optional>
#include <string>

#include "inst_cycle/IrExecutionService.hpp"

namespace BMMQ::GameGearIR {

inline constexpr std::uint32_t kArchitectureId = 0x5A800001u;
inline constexpr std::uint32_t kHelperAbiVersion = 1u;

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
    IncrementRefresh = 0u,
    UpdateIncrementFlags,
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
    Halted = 1u << 0u,
    InterruptPending = 1u << 1u,
    DeferredInterruptEnable = 1u << 2u,
};

using ExecutionStateCallback = std::uint64_t (*)(const void*) noexcept;

class CoreAdapter final : public IR::IIrCoreAdapter {
public:
    CoreAdapter() = default;
    CoreAdapter(const void* opaque, ExecutionStateCallback executionState) noexcept
        : opaque_(opaque), executionState_(executionState)
    {
    }

    [[nodiscard]] std::uint32_t architectureId() const noexcept override;
    [[nodiscard]] std::uint32_t irAbiVersion() const noexcept override;
    [[nodiscard]] IR::BlockPtr lower(const IR::LoweringRequest& request,
                                     std::string* error) override;
    [[nodiscard]] IR::ValidationResult validateBlock(const IR::Block& block) const override;
    [[nodiscard]] std::optional<std::string>
    validateExecutionState(const IR::Block& block) const override;

private:
    const void* opaque_ = nullptr;
    ExecutionStateCallback executionState_ = nullptr;
};

} // namespace BMMQ::GameGearIR

#endif
