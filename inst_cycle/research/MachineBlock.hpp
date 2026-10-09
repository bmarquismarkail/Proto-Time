#pragma once
#include "WholeBlock.hpp"
#include "machine/ExecutionSlice.hpp"
#include <array>

namespace BMMQ::IR::Research {
inline constexpr std::uint32_t kMachineBlockBindingVersion = 1;
struct OwnerIdentity final {};
using Owner = std::shared_ptr<const OwnerIdentity>;

// Internal, read on the paused machine lane. Research harnesses use this to
// account for CPU side exits and mapping changes without masking guard failures.
struct MachineBlockState {
  std::uint64_t mappingGeneration = 0;
  std::uint64_t executionState = 0;
};

// Contains no CPU pointers or borrowed execution state. Construct on the paused
// control lane; execute and fault on the single machine lane only.
class BoundBlock {
public:
  BoundBlock(CompiledBlock code, Owner owner, std::uint64_t generation)
      : code_(std::move(code)), owner_(std::move(owner)),
        generation_(generation) {
    // A range fact, not a generic immutability claim. Both admitted cores map
    // only ROM/BIOS below $8000; their mapping guards protect those windows.
    std::array<bool, 4> seen{};
    scalarValid_ = true;
    for (const auto &guard : code_.block().guards) {
      const auto kind = static_cast<std::size_t>(guard.kind);
      if (kind >= seen.size() || seen[kind]) {
        scalarValid_ = false;
        continue;
      }
      seen[kind] = true;
      switch (guard.kind) {
      case GuardKind::MappingGeneration:
        mapping_ = guard.expected;
        if (mapping_ != code_.block().mappingGeneration) scalarValid_ = false;
        break;
      case GuardKind::HelperAbi: helperAbi_ = guard.expected; break;
      case GuardKind::ExecutionState:
        stateMask_ = guard.mask;
        stateExpected_ = guard.expected & guard.mask;
        break;
      case GuardKind::CodeBytes:
        lowRomCode_ = !guard.bytes.empty() && guard.subject < 0x8000 &&
                      guard.bytes.size() <= 0x8000 - guard.subject;
        break;
      }
    }
    for (bool present : seen) scalarValid_ = scalarValid_ && present;
  }
  const CompiledBlock &code() const noexcept { return code_; }
  bool hasLowRomCode() const noexcept { return lowRomCode_; }
  // Derived only from this binding's owned, immutable guard metadata. Entry
  // still validates bytes; writable/observed continuations use full checks.
  bool scalarGuardsMatch(std::uint64_t mapping, std::uint64_t state,
                         std::uint64_t helperAbi) const noexcept {
    return scalarValid_ && mapping == mapping_ && helperAbi == helperAbi_ &&
           (state & stateMask_) == stateExpected_;
  }
  bool matches(const Owner &owner, std::uint64_t generation) const noexcept {
    return !faulted_ && owner_ == owner && generation_ == generation;
  }
  void fault() noexcept { faulted_ = true; }

private:
  CompiledBlock code_;
  Owner owner_;
  std::uint64_t generation_;
  bool faulted_ = false;
  bool lowRomCode_ = false;
  bool scalarValid_ = false;
  std::uint64_t mapping_ = 0, helperAbi_ = 0, stateMask_ = 0, stateExpected_ = 0;
};
// Core validators re-lower the copied code and require exact canonical IR. The
// emitted entry remains paired with those instructions; only mapping guards are
// rebound to the actual machine. Unknown/cross-core architecture is rejected.
BoundBlock bindMachineBlock(const CompiledBlock &, IIrCoreAdapter &,
                            const Owner &, std::uint64_t generation,
                            std::uint64_t mapping);

template <class Check, class CpuRetire, class MachineRetire, class Fault>
ExecutionSliceResult executeMachineBlock(
    BoundBlock &binding, InterpreterHost &host, const ExecutionBudget &budget,
    InstructionRetirementSink *observer, Check check, CpuRetire cpuRetire,
    MachineRetire machineRetire, Fault fault) {
  struct State {
    Check &check;
    CpuRetire &cpuRetire;
    MachineRetire &machineRetire;
    const ExecutionBudget &budget;
    InstructionRetirementSink *observer;
    ExecutionSliceResult slice{};
    static bool before(void *opaque, const Block &block, std::size_t index) {
      return static_cast<State *>(opaque)->check(block, index);
    }
    static bool retire(void *opaque, const GuestInstruction &instruction,
                       const InterpreterResult &result,
                       const Progress &progress) {
      auto &s = *static_cast<State *>(opaque);
      s.slice.lastFeedback = s.cpuRetire(instruction, result);
      s.slice.progress = {progress.instructions, progress.cycles};
      auto machine = s.machineRetire(s.slice.lastFeedback, s.slice.progress);
      auto observer = s.observer
                          ? s.observer->retireInstruction(s.slice.lastFeedback,
                                                          s.slice.progress)
                          : InstructionRetirementDecision::continueSlice();
      if (!machine.continueExecution) {
        s.slice.exitReason = machine.exitReason;
        return false;
      }
      if (!observer.continueExecution) {
        s.slice.exitReason = observer.exitReason;
        return false;
      }
      if (s.budget.stopOnSegmentBoundary &&
          s.slice.lastFeedback.segmentBoundaryHint) {
        s.slice.exitReason = ExecutionSliceExitReason::SegmentBoundary;
        return false;
      }
      if (progress.cycles >= s.budget.maxCycles) {
        s.slice.exitReason = ExecutionSliceExitReason::CycleBudget;
        return false;
      }
      if (progress.instructions >= s.budget.maxInstructions) {
        s.slice.exitReason = ExecutionSliceExitReason::InstructionBudget;
        return false;
      }
      return true;
    }
  } state{check, cpuRetire, machineRetire, budget, observer};
  state.slice.exitReason = ExecutionSliceExitReason::MachineBoundary;
  try {
    const auto progress =
        binding.code().execute(host, {&state, &State::before, &State::retire},
                               budget.maxInstructions, budget.maxCycles);
    if (progress.exit == Exit::InstructionBudget)
      state.slice.exitReason = ExecutionSliceExitReason::InstructionBudget;
    if (progress.exit == Exit::CycleBudget)
      state.slice.exitReason = ExecutionSliceExitReason::CycleBudget;
    return state.slice;
  } catch (...) {
    binding.fault();
    fault();
    throw;
  }
}
} // namespace BMMQ::IR::Research
