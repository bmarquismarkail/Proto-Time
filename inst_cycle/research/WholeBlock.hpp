#pragma once

// Internal research contract. The external indexed IR ABI remains version 1.
#include "inst_cycle/IrExecutionService.hpp"
#include <cstdint>
#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include "PhaseDiagnostics.hpp"

namespace BMMQ::IR::Research {
inline constexpr std::uint32_t kWholeBlockVersion = 1;
enum class Exit {
  Completed,
  Guard,
  InstructionBudget,
  CycleBudget,
  Observer,
  ControlFlow
};
struct Progress {
  std::uint64_t instructions = 0;
  std::uint64_t cycles = 0;
  Exit exit = Exit::Completed;
};
struct Hooks {
  void *opaque = nullptr;
  // Must inspect PC, code, mapping, execution state and lifecycle generation.
  // False means no effects of this instruction have begun; no implicit retry.
  bool (*before)(void *, const Block &, std::size_t) = nullptr;
  // The owner advances hardware and publishes ordered effects here, exactly
  // once, before the next guard. False ends the invocation at this retirement.
  bool (*retire)(void *, const GuestInstruction &, const InterpreterResult &,
                 const Progress &) = nullptr;
  bool valid() const noexcept { return before && retire; }
  bool callBefore(const Block& block, std::size_t index) const {
    return before(opaque, block, index);
  }
  bool callRetire(const GuestInstruction& instruction,
                  const InterpreterResult& result, const Progress& progress) const {
    return retire(opaque, instruction, result, progress);
  }
};

// Invocation-local typed hooks. The state remains owned by the machine lane;
// no callable or state pointer is stored in a compiled artifact or binding.
template<class State> struct TypedHooks {
  State& state;
  bool valid() const noexcept { return true; }
  bool callBefore(const Block& block, std::size_t index) const {
    return State::before(&state, block, index);
  }
  bool callRetire(const GuestInstruction& instruction,
                  const InterpreterResult& result, const Progress& progress) const {
    return State::retire(&state, instruction, result, progress);
  }
};

// Invocation-local register view for the two admitted cores' shared numeric
// register envelope. Never retained by a binding; state is published on every
// operation, including before helpers/retirement/observers. No endian assumptions.
struct DirectRegisters {
  std::array<std::uint16_t*, 6> words{}; // AF BC DE HL SP PC
  std::uint8_t flagsMask = 0xff;
  bool valid() const noexcept { for (auto* p : words) if (!p) return false; return true; }
  std::uint64_t read(std::uint32_t id) const {
    TIME_RESEARCH_SCOPE(registerPhase, Phase::RegisterRead);
    if (id < 8) return (*words[id / 2] >> ((id & 1) ? 0 : 8)) & 255;
    if (id < 14) return *words[id - 8];
    throw std::invalid_argument("unknown direct register");
  }
  void write(std::uint32_t id, std::uint64_t value) const {
    TIME_RESEARCH_SCOPE(registerPhase, Phase::RegisterWrite);
    if (id < 8) {
      auto& word = *words[id / 2];
      auto byte = static_cast<std::uint8_t>(value);
      if (id == 1) byte &= flagsMask;
      word = (id & 1) ? (word & 0xff00) | byte : (word & 255) | (std::uint16_t(byte) << 8);
    } else if (id < 14) {
      *words[id - 8] = static_cast<std::uint16_t>(id == 8 ? value & (0xff00 | flagsMask) : value);
    } else throw std::invalid_argument("unknown direct register");
  }
};

template<class HookSet, class Host = InterpreterHost> class BasicInvocation {
public:
  BasicInvocation(const Block &block, Host &host, HookSet hooks,
             std::uint64_t instructions, std::uint64_t cycles, DirectRegisters* registers = nullptr)
      : block_(block), host_(host), hooks_(hooks),
        maxInstructions_(instructions), maxCycles_(cycles), registers_(registers) {
    if (!hooks.valid())
      throw std::invalid_argument("whole-block hooks are required");
  }
  Host &host() noexcept { return host_; }
  std::uint64_t callHelper(std::uint32_t id, ValueType type, std::span<const std::uint64_t> args) {
    TIME_RESEARCH_SCOPE(helperPhase, id == 0 ? Phase::Helper0 : Phase::Helper);
    return host_.callHelper(id, type, args);
  }
  DirectRegisters& registers() { if (!registers_) throw std::logic_error("missing direct registers"); return *registers_; }
  const Progress &progress() const noexcept { return progress_; }
  bool pendingRetirement() const noexcept { return begun_; }
  bool begin(std::size_t index) {
    if (stopped_ || begun_ || index != progress_.instructions ||
        index >= block_.instructions.size())
      throw std::logic_error("whole-block instruction order violated");
    if (progress_.instructions >= maxInstructions_) {
      progress_.exit = Exit::InstructionBudget;
      stopped_ = true;
      return false;
    }
    if (progress_.cycles >= maxCycles_) {
      progress_.exit = Exit::CycleBudget;
      stopped_ = true;
      return false;
    }
    if (!hooks_.callBefore(block_, index)) {
      progress_.exit = Exit::Guard;
      stopped_ = true;
      return false;
    }
    begun_ = true;
    TIME_RESEARCH_OPS_BEGIN();
    return true;
  }
  bool retire(std::size_t index, const InterpreterResult &result) {
    if (stopped_ || !begun_ || index != progress_.instructions ||
        !result.retirementReached)
      throw std::logic_error("whole-block retirement violated");
    TIME_RESEARCH_OPS_END();
    begun_ = false;
    const auto &instruction = block_.instructions.at(index);
    const auto cycles = result.cycleCondition ? instruction.cyclesTaken
                                              : instruction.cyclesNotTaken;
    ++progress_.instructions;
    progress_.cycles =
        cycles > std::numeric_limits<std::uint64_t>::max() - progress_.cycles
            ? std::numeric_limits<std::uint64_t>::max()
            : progress_.cycles + cycles;
    if (!hooks_.callRetire(instruction, result, progress_)) {
      progress_.exit = Exit::Observer;
      stopped_ = true;
      return false;
    }
    if (result.exitRequested || result.branchTaken || instruction.controlFlow ||
        instruction.interruptSensitive) {
      progress_.exit = Exit::ControlFlow;
      stopped_ = true;
      return false;
    }
    if (progress_.cycles >= maxCycles_) {
      progress_.exit = Exit::CycleBudget;
      stopped_ = true;
      return false;
    }
    if (progress_.instructions >= maxInstructions_) {
      progress_.exit = Exit::InstructionBudget;
      stopped_ = true;
      return false;
    }
    return true;
  }

private:
  const Block &block_;
  Host &host_;
  HookSet hooks_;
  std::uint64_t maxInstructions_, maxCycles_;
  Progress progress_;
  DirectRegisters* registers_ = nullptr;
  bool begun_ = false;
  bool stopped_ = false;
};

using Invocation = BasicInvocation<Hooks>;

#ifdef TIME_RESEARCH_TYPED_INVOCATION
// Only the isolated experiment binary can select the typed entry. Its generic
// and typed rows share one executable, machine code and host environment.
inline thread_local bool typedInvocationEnabled = true;
// B exposes the concrete Game Gear host in the same isolated executable.
inline thread_local bool concreteGameGearHostEnabled = false;
#endif

// Metadata and entry are emitted together into the same read-only executable
// image. Construction is a trusted build-time pairing, not an external ABI.
class CompiledBlock {
public:
  using Entry = void (*)(Invocation &);
  CompiledBlock(BlockPtr block, Entry entry,
                std::uint32_t architecture = kAnyArchitecture, Entry directEntry = nullptr)
      : block_(block ? std::make_shared<const Block>(*block) : nullptr),
        entry_(entry), architecture_(architecture), directEntry_(directEntry) {
    if (block) { TIME_RESEARCH_COPY(*block); }
    if (!block_ || !entry_ || !validate(*block_))
      throw std::invalid_argument("invalid emitted block");
  }
  const Block &block() const noexcept { return *block_; }
  Entry entry() const noexcept { return entry_; }
  Entry directEntry() const noexcept { return directEntry_; }
  std::uint32_t architectureId() const noexcept { return architecture_; }
  Progress execute(
      InterpreterHost &host, Hooks hooks, std::uint64_t instructions,
      std::uint64_t cycles = std::numeric_limits<std::uint64_t>::max(),
      DirectRegisters* registers = nullptr) const {
    if (registers && (!registers->valid() || !directEntry_))
      throw std::invalid_argument("invalid direct execution view");
    Invocation invocation(*block_, host, hooks, instructions, cycles, registers);
    // Exceptions propagate. Partially executed instructions are never
    // retried by this contract; the machine owner must fault the session.
    (registers ? directEntry_ : entry_)(invocation);
    return verifyCompleted(invocation);
  }
  // Research-only entry. Generated bodies and hooks become visible together to
  // the compiler; the host/register strategy and all retirement rules are shared
  // with the generic path. Dispatch must authenticate the original entry pointer.
  template<class Host, class HookSet, class EntryDispatch>
  Progress executeTyped(Host& host, HookSet hooks,
      std::uint64_t instructions, std::uint64_t cycles,
      DirectRegisters* registers, EntryDispatch dispatch) const {
    if (registers && (!registers->valid() || !directEntry_))
      throw std::invalid_argument("invalid direct execution view");
    BasicInvocation<HookSet, Host> invocation(*block_, host, hooks, instructions, cycles, registers);
    if (!dispatch(invocation))
      throw std::invalid_argument("unknown typed research entry");
    return verifyCompleted(invocation);
  }

private:
  template<class InvocationType>
  Progress verifyCompleted(const InvocationType& invocation) const {
    if (invocation.pendingRetirement())
      throw std::logic_error("emitted entry omitted retirement");
    if (invocation.progress().exit == Exit::Completed &&
        invocation.progress().instructions != block_->instructions.size())
      throw std::logic_error("emitted entry returned without completing block");
    return invocation.progress();
  }

  BlockPtr block_;
  Entry entry_;
  std::uint32_t architecture_;
  Entry directEntry_;
};
} // namespace BMMQ::IR::Research
