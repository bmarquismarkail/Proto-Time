#include "WholeBlockFixtures.hpp"
#include "inst_cycle/research/MachineBlock.hpp"
#define TIME_RESEARCH_TYPED_BODIES_ONLY
#include "research-whole-blocks.cpp"
#undef TIME_RESEARCH_TYPED_BODIES_ONLY
#include <iostream>

using namespace BMMQ;
using namespace BMMQ::IR;
using namespace BMMQ::IR::Research;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
struct Host final : InterpreterHost {
  std::array<std::uint64_t, 14> registers{};
  std::array<std::uint8_t, 65536> memory{};
  std::uint64_t effects = 0;
  bool throwHelper = false;
  std::uint64_t readRegister(std::uint32_t id, ValueType) override { return registers.at(id); }
  void writeRegister(std::uint32_t id, ValueType, std::uint64_t value) override {
    registers.at(id) = value; ++effects;
  }
  std::uint64_t loadMemory(std::uint64_t address, ValueType, MemoryClass) override { return memory.at(address); }
  void storeMemory(std::uint64_t address, ValueType, MemoryClass, std::uint64_t value) override {
    memory.at(address) = value; ++effects;
  }
  std::uint64_t callHelper(std::uint32_t, ValueType, std::span<const std::uint64_t>) override {
    if (throwHelper) throw std::runtime_error("helper fault");
    ++effects; return 1;
  }
  void setProgramCounter(std::uint64_t address) override { registers[13] = address; ++effects; }
};
struct Observer final : InstructionRetirementSink {
  std::vector<unsigned>& order;
  bool stop = false;
  explicit Observer(std::vector<unsigned>& sequence) : order(sequence) {}
  InstructionRetirementDecision retireInstruction(const CpuFeedback&, const ExecutionSliceProgress&) override {
    order.push_back(3);
    return stop ? InstructionRetirementDecision::exitSlice(ExecutionSliceExitReason::RetirementRequested)
                : InstructionRetirementDecision::continueSlice();
  }
};
struct Outcome {
  Host host;
  ExecutionSliceResult result;
  std::vector<unsigned> order;
  unsigned faults = 0;
  bool threw = false;
};
Outcome run(bool typed, ExecutionBudget budget, bool machineStop, bool observerStop,
            std::size_t rejectIndex, bool helperFault = false) {
  const auto code = emittedGameBoyCompute();
  const auto owner = std::make_shared<OwnerIdentity>();
  BoundBlock bound(code, owner, 1);
  Outcome outcome;
  outcome.host.registers[13] = code.block().guestStart;
  outcome.host.throwHelper = helperFault;
  Observer observer(outcome.order); observer.stop = observerStop;
  auto check = [&](const Block&, std::size_t index) { return index != rejectIndex; };
  auto cpu = [&](const GuestInstruction& instruction, const InterpreterResult& result) {
    outcome.order.push_back(1);
    CpuFeedback feedback;
    feedback.retiredCycles = result.cycleCondition ? instruction.cyclesTaken : instruction.cyclesNotTaken;
    feedback.segmentBoundaryHint = instruction.controlFlow || instruction.interruptSensitive;
    feedback.pcAfter = outcome.host.registers[13];
    return feedback;
  };
  auto machine = [&](const CpuFeedback&, const ExecutionSliceProgress&) {
    outcome.order.push_back(2);
    return machineStop ? InstructionRetirementDecision::exitSlice(ExecutionSliceExitReason::MachineBoundary)
                       : InstructionRetirementDecision::continueSlice();
  };
  auto fault = [&] { ++outcome.faults; };
  try {
    if (typed) {
      typedInvocationEnabled = true;
      outcome.result = executeMachineBlock(bound, outcome.host, budget, &observer,
          check, cpu, machine, fault, nullptr, [&](auto& invocation) {
            return dispatchTypedResearchBlock(code, invocation, false);
          });
    } else {
      outcome.result = executeMachineBlock(bound, outcome.host, budget, &observer, check, cpu, machine, fault);
    }
  } catch (const std::runtime_error&) { outcome.threw = true; }
  if (helperFault && outcome.threw)
    require(!bound.matches(owner, 1), "faulted binding accepted");
  return outcome;
}
int main() try {
  for (auto instructions : {0u, 1u, 7u}) for (auto cycles : {0u, 1u, 128u})
    for (bool segment : {false, true}) for (bool machineStop : {false, true})
      for (bool observerStop : {false, true}) for (std::size_t reject : {0u, 1u, 99u}) {
        const ExecutionBudget budget{instructions, cycles, segment};
        const auto generic = run(false, budget, machineStop, observerStop, reject);
        const auto typed = run(true, budget, machineStop, observerStop, reject);
        require(generic.host.registers == typed.host.registers && generic.host.memory == typed.host.memory &&
            generic.host.effects == typed.host.effects && generic.order == typed.order, "typed effect ordering differs");
        require(generic.result.progress.retiredInstructions == typed.result.progress.retiredInstructions &&
            generic.result.progress.retiredCycles == typed.result.progress.retiredCycles &&
            generic.result.exitReason == typed.result.exitReason && generic.faults == typed.faults,
            "typed budget or exit precedence differs");
        for (std::size_t i = 0; i < typed.order.size(); i += 3)
          require(typed.order.at(i) == 1 && typed.order.at(i+1) == 2 && typed.order.at(i+2) == 3,
                  "observer suppressed or reordered after machine exit");
        if (typed.result.progress.retiredInstructions && machineStop)
          require(typed.result.exitReason == ExecutionSliceExitReason::MachineBoundary, "machine exit priority lost");
      }
  const auto genericFault = run(false, {7, 128, false}, false, false, 99, true);
  const auto typedFault = run(true, {7, 128, false}, false, false, 99, true);
  require(genericFault.threw && typedFault.threw && genericFault.faults == 1 && typedFault.faults == 1 &&
      genericFault.order == typedFault.order && genericFault.host.registers == typedFault.host.registers &&
      genericFault.host.effects == typedFault.host.effects, "partial helper fault retried or retired");
  const auto code = emittedGameBoyCompute();
  Host host;
  Hooks hooks{nullptr,
      +[](void*, const Block&, std::size_t) { return true; },
      +[](void*, const GuestInstruction&, const InterpreterResult&, const Progress&) { return true; }};
  auto rejects = [&](auto dispatch) {
    bool rejected = false;
    try { (void)code.executeTyped(host, hooks, 7, 128, nullptr, dispatch); }
    catch (const std::exception&) { rejected = true; }
    require(rejected, "malformed typed entry accepted");
  };
  rejects([](auto&) { return false; }); // Unrecognized generated entry.
  rejects([](auto&) { return true; }); // No operations or retirement.
  rejects([](auto& invocation) { (void)invocation.begin(0); return true; });
  rejects([](auto& invocation) { (void)invocation.begin(1); return true; });
  std::cout << "typed/generic budget, callback order, exit priority and partial fault parity passed\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
