#include "WholeBlockFixtures.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include <iostream>
#include <stdexcept>
using namespace BMMQ;
using namespace BMMQ::IR::Research;
void require(bool v, const char *s) {
  if (!v)
    throw std::runtime_error(s);
}
std::vector<uint8_t> rom(const CompiledBlock &code) {
  std::vector<uint8_t> bytes(32768);
  for (const auto &g : code.block().guards)
    if (g.kind == IR::GuardKind::CodeBytes)
      std::copy(g.bytes.begin(), g.bytes.end(), bytes.begin() + g.subject);
  return bytes;
}
template <class Core> struct Compare final : InstructionRetirementSink {
  Core &baseline;
  Core &native;
  uint64_t count = 0;
  Compare(Core &a, Core &b) : baseline(a), native(b) {}
  InstructionRetirementDecision
  retireInstruction(const CpuFeedback &f,
                    const ExecutionSliceProgress &) override {
    auto s = baseline.runSlice(
        {.maxInstructions = 1, .stopOnSegmentBoundary = false});
    require(s.progress.retiredInstructions == 1, "baseline retirement");
    require(s.lastFeedback.retiredCycles == f.retiredCycles, "cycles");
    require(s.lastFeedback.pcBefore == f.pcBefore &&
                s.lastFeedback.pcAfter == f.pcAfter,
            "PC retirement");
    require(baseline.debugRegisters() == native.debugRegisters(),
            "complete registers");
    require(baseline.deterministicStateFingerprint() ==
                native.deterministicStateFingerprint(),
            "complete machine fingerprint");
    ++count;
    return InstructionRetirementDecision::continueSlice();
  }
};
struct Throwing final : InstructionRetirementSink {
  InstructionRetirementDecision
  retireInstruction(const CpuFeedback &,
                    const ExecutionSliceProgress &) override {
    throw std::runtime_error("observer failure");
  }
};
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  require(caught, "expected rejection");
}
template <class Core> void check(const CompiledBlock &code) {
  const auto bytes = rom(code);
  for (unsigned seed : {0u, 0x7fu, 0x80u, 0xffu}) {
    Core baseline, native;
    baseline.loadRom(bytes);
    native.loadRom(bytes);
    auto registers = baseline.debugRegisters();
    registers[0] = seed << 8;
    registers[3] = 0xc000;
    require(baseline.debugValidateRegisters(registers),
            "valid fixture registers");
    baseline.debugCommitRegisters(registers);
    native.debugCommitRegisters(registers);
    baseline.debugCommitByte(0xc000, seed);
    native.debugCommitByte(0xc000, seed);
    auto bound = native.bindResearchBlock(code);
    Compare<Core> observer(baseline, native);
    for (unsigned loop = 0; loop < 128; ++loop) {
      auto s = native.runResearchBlock(
          bound,
          {.maxInstructions = code.block().instructions.size(),
           .stopOnSegmentBoundary = false},
          &observer);
      require(s.progress.retiredInstructions ==
                  code.block().instructions.size(),
              "whole block retirement");
    }
    // No observer uses the ROM continuation fast path. Check every possible
    // retirement boundary, including partial invocations, against baseline.
    for (std::size_t limit = 1; limit <= code.block().instructions.size(); ++limit) {
      for (auto *machine : {&baseline, &native}) {
        auto state = machine->debugRegisters();
        state[5] = code.block().guestStart;
        machine->debugCommitRegisters(state);
      }
      const ExecutionBudget budget{.maxInstructions = limit,
                                   .stopOnSegmentBoundary = false};
      const auto expected = baseline.runSlice(budget);
      const auto actual = native.runResearchBlock(bound, budget);
      require(actual.progress.retiredInstructions == limit &&
                  actual.progress.retiredCycles == expected.progress.retiredCycles,
              "unobserved retirement accounting");
      const auto &a = actual.lastFeedback;
      const auto &e = expected.lastFeedback;
      require(a.pcBefore == e.pcBefore && a.pcAfter == e.pcAfter &&
                  a.retiredCycles == e.retiredCycles && a.isControlFlow == e.isControlFlow &&
                  a.segmentBoundaryHint == e.segmentBoundaryHint,
              "unobserved retirement feedback");
      require(baseline.debugRegisters() == native.debugRegisters() &&
                  baseline.deterministicStateFingerprint() == native.deterministicStateFingerprint(),
              "unobserved ROM continuation mismatch");
    }
    auto start = native.debugRegisters();
    start[5] = code.block().guestStart;
    native.debugCommitRegisters(start);
    const auto before = native.deterministicStateFingerprint();
    auto zero = native.runResearchBlock(bound, {.maxInstructions = 0});
    require(zero.progress.retiredInstructions == 0, "zero budget");
    require(before == native.deterministicStateFingerprint(),
            "zero budget mutation");
    Core sibling;
    sibling.loadRom(bytes);
    require(sibling.runResearchBlock(bound, {.maxInstructions = 1})
                    .progress.retiredInstructions == 0,
            "owner guard");
    auto fresh = native.bindResearchBlock(code);
    Throwing throwing;
    rejects([&] {
      native.runResearchBlock(fresh, {.maxInstructions = 1}, &throwing);
    });
    rejects([&] { native.runSlice({.maxInstructions = 1}); });
    native.loadRom(bytes);
    require(native.runResearchBlock(bound, {.maxInstructions = 1})
                    .progress.retiredInstructions == 0,
            "reload generation guard");
  }
  Core machine;
  machine.loadRom(bytes);
  rejects([&] { machine.bindResearchBlock(emittedControl()); });
  BMMQ::Plugin::PortableIrStepPolicy policy;
  machine.attachExecutorPolicy(policy);
  rejects([&] { machine.bindResearchBlock(code); });
}
int main() try {
  GB::GameBoyMachine gb;
  gb.loadRom(rom(emittedGameBoyCompute()));
  GameGearMachine gg;
  gg.loadRom(rom(emittedGameGearCompute()));
  rejects([&] { gb.bindResearchBlock(emittedGameGearCompute()); });
  rejects([&] { gg.bindResearchBlock(emittedGameBoyCompute()); });
  check<GB::GameBoyMachine>(emittedGameBoyCompute());
  check<GB::GameBoyMachine>(emittedGameBoyRam());
  check<GameGearMachine>(emittedGameGearCompute());
  check<GameGearMachine>(emittedGameGearRam());
  std::cout << "real-core whole-block differential checks passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
