#ifndef BMMQ_EXECUTION_SLICE_HPP
#define BMMQ_EXECUTION_SLICE_HPP

#include <cstdint>
#include <limits>

#include "CPU.hpp"

namespace BMMQ {

// Instructions are atomic. maxCycles is therefore a soft ceiling checked after
// each instruction retires; a single instruction may take the total past it.
struct ExecutionBudget {
    std::uint64_t maxInstructions = 1u;
    std::uint64_t maxCycles = std::numeric_limits<std::uint64_t>::max();
    bool stopOnSegmentBoundary = true;
};

enum class ExecutionSliceExitReason : std::uint8_t {
    InstructionBudget,
    CycleBudget,
    SegmentBoundary,
    RetirementRequested,
    MachineBoundary,
};

struct ExecutionSliceProgress {
    std::uint64_t retiredInstructions = 0u;
    std::uint64_t retiredCycles = 0u;
};

struct ExecutionSliceResult {
    ExecutionSliceProgress progress{};
    CpuFeedback lastFeedback{};
    ExecutionSliceExitReason exitReason = ExecutionSliceExitReason::InstructionBudget;
};

struct InstructionRetirementDecision {
    bool continueExecution = true;
    ExecutionSliceExitReason exitReason = ExecutionSliceExitReason::RetirementRequested;

    [[nodiscard]] static constexpr InstructionRetirementDecision continueSlice() noexcept {
        return {};
    }

    [[nodiscard]] static constexpr InstructionRetirementDecision exitSlice(
        ExecutionSliceExitReason reason = ExecutionSliceExitReason::RetirementRequested) noexcept
    {
        return {.continueExecution = false, .exitReason = reason};
    }
};

class InstructionRetirementSink {
public:
    virtual ~InstructionRetirementSink() = default;

    // Called synchronously on the emulation lane after every guest instruction
    // and before another instruction may begin.
    virtual InstructionRetirementDecision retireInstruction(
        const CpuFeedback& feedback,
        const ExecutionSliceProgress& progress) = 0;
};

} // namespace BMMQ

#endif // BMMQ_EXECUTION_SLICE_HPP
