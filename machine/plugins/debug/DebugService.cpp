#include "DebugService.hpp"
#include "machine/Machine.hpp"
#include <stdexcept>
namespace BMMQ::Debug {
DebugService::DebugService(Machine &machine) : DebugService(machine, true) {}
DebugService::DebugService(Machine &machine, bool recording)
    : machine_(machine), adapter_(dynamic_cast<IDebugMachineV1 &>(machine)), engine_(recording),
      generation_(machine.observationGeneration()) {
  adapter_.connectDebugEngine(&engine_);
  engine_.boundary(generation_);
}
DebugService::~DebugService() {
  if (connected_)
    adapter_.connectDebugEngine(nullptr);
}
void DebugService::lane() const {
  if (owner_ != std::this_thread::get_id())
    throw std::logic_error("debugger machine lane changed");
}
void DebugService::stop(Reason reason) noexcept {
  state_ = State::Paused;
  reason_ = reason;
  ++pauseId_;
  remainingSteps_ = 0;
}
void DebugService::synchronize() {
  if (generation_ != machine_.observationGeneration()) {
    generation_ = machine_.observationGeneration();
    engine_.boundary(generation_);
    scriptHooks_.invalidate(generation_);
    stop(Reason::Generation);
    skipBreakpoint_ = false;
  }
}
Reply DebugService::snapshot(std::uint32_t id) const {
  const auto registers = adapter_.debugRegisters();
  return {.id = id,
          .state = state_,
          .reason = reason_,
          .generation = generation_,
          .pauseId = pauseId_,
          .lost = engine_.lost(),
          .codeBacking = adapter_.debugBacking(registers[5]),
          .rejectedActions = rejectedActions_,
          .requestOverflow = requestOverflow_.load(std::memory_order_relaxed),
          .registers = registers};
}
Reply DebugService::apply(const Command &command) {
  auto reject = [&](Error error) {
    ++rejectedActions_;
    auto reply = snapshot(command.id);
    reply.error = error;
    return reply;
  };
  if (state_ == State::Detached)
    return reject(Error::Detached);
  if (state_ == State::Faulted && command.operation != Operation::Disconnect)
    return reject(Error::Fault);
  if (command.generation != generation_)
    return reject(Error::Stale);
  if (command.operation != Operation::Pause &&
      command.operation != Operation::Disconnect && command.pauseId != pauseId_)
    return reject(Error::Stale);
  switch (command.operation) {
  case Operation::Continue:
  case Operation::Step:
    if (state_ != State::Paused)
      return reject(Error::Running);
    if (command.operation == Operation::Step &&
        (!command.steps || command.steps > 10'000'000))
      return reject(Error::Invalid);
    remainingSteps_ = command.operation == Operation::Step ? command.steps : 0;
    skipBreakpoint_ = reason_ == Reason::Breakpoint;
    state_ = State::Running;
    break;
  case Operation::Pause:
    stop(Reason::Pause);
    break;
  case Operation::Rules:
    if (state_ != State::Paused)
      return reject(Error::Running);
    if (command.breakCount > 64 || command.watchCount > 64)
      return reject(Error::Invalid);
    for (unsigned i = 0; i < command.watchCount; ++i)
      if (command.watches[i].first > command.watches[i].last ||
          unsigned(command.watches[i].access) > unsigned(Access::PortWrite))
        return reject(Error::Invalid);
    for (unsigned i = 0; i < command.watchCount; ++i)
      if (!adapter_.debugPortBus() &&
          (command.watches[i].access == Access::PortRead ||
           command.watches[i].access == Access::PortWrite))
        return reject(Error::Unsupported);
    engine_.rules(std::span(command.breaks.data(), command.breakCount),
                  std::span(command.watches.data(), command.watchCount));
    break;
  case Operation::Edit:
    if (state_ != State::Paused)
      return reject(Error::Running);
    if (command.byteCount > 64 ||
        (command.editRegisters &&
         !adapter_.debugValidateRegisters(command.registers)))
      return reject(Error::Invalid);
    for (unsigned i = 0; i < command.byteCount; ++i)
      if (!adapter_.debugWritable(command.bytes[i].address))
        return reject(Error::Unsupported);
    // All validation precedes publication. Core commit methods cannot fail,
    // acknowledge ports, call host plugins or allocate.
    for (unsigned i = 0; i < command.byteCount; ++i)
      adapter_.debugCommitByte(command.bytes[i].address,
                               command.bytes[i].value);
    if (command.editRegisters)
      adapter_.debugCommitRegisters(command.registers);
    if (command.byteCount || command.editRegisters) {
      adapter_.debugEdited();
      synchronize();
    }
    break;
  case Operation::Inspect: {
    if (state_ != State::Paused)
      return reject(Error::Running);
    if (command.length > 256 ||
        std::uint32_t(command.address) + command.length > 65536)
      return reject(Error::Invalid);
    auto reply = snapshot(command.id);
    for (unsigned i = 0; i < command.length; ++i)
      if (!adapter_.debugPeek(std::uint16_t(command.address + i),
                              reply.memory[i]))
        return reject(Error::Unsupported);
    reply.length = command.length;
    return reply;
  }
  case Operation::Disconnect:
    scriptHooks_.clear();
    adapter_.connectDebugEngine(nullptr);
    connected_ = false;
    engine_.cancel();
    state_ = State::Detached;
    break;
  default:
    return reject(Error::Invalid);
  }
  return snapshot(command.id);
}
void DebugService::pump() {
  lane();
  synchronize();
  if (pendingReply_) {
    if (!replies_.push(*pendingReply_))
      return;
    pendingReply_.reset();
  }
  while (auto command = requests_.pop()) {
    auto reply = apply(*command);
    if (!replies_.push(reply)) {
      pendingReply_ = reply;
      return;
    }
  }
}
ExecutionSliceResult DebugService::run(std::uint64_t maxInstructions) {
  lane();
  pump();
  ExecutionSliceResult result;
  result.exitReason = ExecutionSliceExitReason::RetirementRequested;
  if (pendingReply_ || state_ != State::Running)
    return result;
  for (std::uint64_t i = 0; i < maxInstructions && state_ == State::Running;
       ++i) {
    synchronize();
    if (state_ != State::Running)
      break;
    const auto registers = adapter_.debugRegisters();
    const auto pc = registers[5];
    if (!skipBreakpoint_ && engine_.breakpoint(pc, adapter_.debugBacking(pc))) {
      stop(Reason::Breakpoint);
      break;
    }
    skipBreakpoint_ = false;
    engine_.begin(adapter_.debugBacking(pc));
    try {
      const auto step = machine_.runSlice({.maxInstructions = 1});
      result.lastFeedback = step.lastFeedback;
      result.progress.retiredInstructions += step.progress.retiredInstructions;
      result.progress.retiredCycles += step.progress.retiredCycles;
      if (!step.progress.retiredInstructions) {
        engine_.cancel();
        stop(Reason::Pause);
        break;
      }
      const auto watch =
          engine_.retire(step.lastFeedback, adapter_.debugRegisters());
      const bool hookPause=scriptHooks_.retire(generation_);
      if (watch)
        stop(Reason::Watchpoint);
      else if(hookPause)
        stop(Reason::Pause);
      else if (remainingSteps_ && --remainingSteps_ == 0)
        stop(Reason::Step);
    } catch (...) {
      engine_.cancel();
      state_ = State::Faulted;
      reason_ = Reason::Fault;
      break;
    }
    pump();
    if (pendingReply_)
      break;
  }
  if (state_ == State::Paused || state_ == State::Faulted) {
    auto stopped = snapshot();
    if (!replies_.push(stopped))
      pendingReply_ = stopped;
  }
  return result;
}
} // namespace BMMQ::Debug
