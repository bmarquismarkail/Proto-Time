#ifndef BMMQ_IR_EXECUTION_SERVICE_HPP
#define BMMQ_IR_EXECUTION_SERVICE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "BlockTranslator.hpp"
#include "IntermediateRepresentation.hpp"
#include "IntermediateRepresentationInterpreter.hpp"

namespace BMMQ::IR {

inline constexpr std::uint32_t kIrAbiVersion = 1u;
inline constexpr std::uint32_t kAnyArchitecture = 0u;

struct SourceInstruction {
    std::uint64_t address = 0u;
    std::array<std::uint8_t, 4> bytes{};
    std::uint8_t length = 0u;
};

struct LoweringRequest {
    std::span<const SourceInstruction> instructions{};
    std::uint64_t mappingGeneration = 0u;
    std::uint64_t executionState = 0u;
};

// Validates the current single-profile core guard contract against copied
// source instructions. Core adapters supply only architecture-owned constants.
[[nodiscard]] ValidationResult validateRequiredGuardProfile(
    const LoweringRequest& request,
    const Block& block,
    std::uint64_t helperAbiVersion,
    std::uint64_t executionStateMask,
    std::uint64_t maximumGuestAddress);

class IIrCoreAdapter {
public:
    IIrCoreAdapter() = default;
    IIrCoreAdapter(const IIrCoreAdapter&) = delete;
    IIrCoreAdapter& operator=(const IIrCoreAdapter&) = delete;
    IIrCoreAdapter(IIrCoreAdapter&&) = delete;
    IIrCoreAdapter& operator=(IIrCoreAdapter&&) = delete;
    virtual ~IIrCoreAdapter() = default;
    [[nodiscard]] virtual std::uint32_t architectureId() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t irAbiVersion() const noexcept = 0;
    [[nodiscard]] virtual BlockPtr lower(const LoweringRequest& request,
                                         std::string* error) = 0;
    [[nodiscard]] virtual ValidationResult validateBlock(const Block& block) const = 0;
    // Host adapters override this to validate selected-adapter output against
    // the copied lowering request. Dynamic adapter validation is supplemental.
    [[nodiscard]] virtual ValidationResult validateLoweredBlock(
        const LoweringRequest& request,
        const Block& block) const
    {
        (void)request;
        return validateBlock(block);
    }
    [[nodiscard]] virtual std::optional<std::string>
    validateExecutionState(const Block& block) const = 0;
};

class IIrExecutionBackend {
public:
    IIrExecutionBackend() = default;
    IIrExecutionBackend(const IIrExecutionBackend&) = delete;
    IIrExecutionBackend& operator=(const IIrExecutionBackend&) = delete;
    IIrExecutionBackend(IIrExecutionBackend&&) = delete;
    IIrExecutionBackend& operator=(IIrExecutionBackend&&) = delete;
    virtual ~IIrExecutionBackend() = default;
    [[nodiscard]] virtual bool supports(std::uint32_t architectureId,
                                        std::uint32_t irAbiVersion) const noexcept = 0;
    [[nodiscard]] virtual BlockBackendArtifactPtr compile(const BlockPtr& block,
                                                          std::string* error) = 0;
    virtual bool execute(const BlockBackendArtifact& artifact,
                         std::size_t instructionIndex,
                         InterpreterHost& host,
                         InterpreterResult* result) = 0;
};

class PortableIrExecutionBackend final : public IIrExecutionBackend {
public:
    [[nodiscard]] bool supports(std::uint32_t architectureId,
                                std::uint32_t irAbiVersion) const noexcept override;
    [[nodiscard]] BlockBackendArtifactPtr compile(const BlockPtr& block,
                                                  std::string* error) override;
    bool execute(const BlockBackendArtifact& artifact,
                 std::size_t instructionIndex,
                 InterpreterHost& host,
                 InterpreterResult* result) override;

private:
    Interpreter interpreter_{};
};

class IrExecutionService {
public:
    struct Limits {
        static constexpr std::size_t kMaxInstructions = 16u;
        static constexpr std::size_t kMaxGuestBytes = 48u;
        static constexpr std::size_t kMaxGuards = 16u;
        static constexpr std::size_t kMaxOpsPerInstruction = 64u;
        static constexpr std::size_t kMaxOperandsPerOp = 8u;
        static constexpr std::size_t kMaxValues = 64u;
        static constexpr std::uint32_t kMaxCyclesPerInstruction = 1'000'000u;
    };

    struct PrepareResult {
        bool prepared = false;
        bool fallback = false;
        std::string message{};
        BlockBackendArtifactPtr artifact{};
    };

    explicit IrExecutionService(IIrCoreAdapter& adapter,
                                IIrExecutionBackend& backend,
                                IIrCoreAdapter& hostValidator) noexcept;

    // The copied lowering request remains authoritative through preparation.
    // hostValidator is a built-in core adapter even when adapter is dynamic.
    [[nodiscard]] PrepareResult prepare(const LoweringRequest& request,
                                        const BlockPtr& block,
                                        std::string* error = nullptr) const;
    // Returns false only when a pre-execution state guard rejects the prepared
    // block. Once backend execution begins, failure is fatal and is never
    // converted into an interpreter retry.
    [[nodiscard]] bool tryExecute(const Block& block,
                                  const BlockBackendArtifact& artifact,
                                  std::size_t instructionIndex,
                                  InterpreterHost& host,
                                  InterpreterResult* out = nullptr) const;

private:
    [[nodiscard]] static bool checkLimits(const Block& block) noexcept;
    [[nodiscard]] static PrepareResult fallback(std::string message,
                                                std::string* error);

    IIrCoreAdapter& adapter_;
    IIrExecutionBackend& backend_;
    IIrCoreAdapter& hostValidator_;
};

} // namespace BMMQ::IR

#endif
