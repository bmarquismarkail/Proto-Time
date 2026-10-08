#include "WholeBlockFixtures.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <unistd.h>
using namespace BMMQ;
using namespace BMMQ::IR::Research;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "expected rejection");
}
struct TemporaryState {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("time-research-boundary-" + std::to_string(getpid()) + "-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".ptss");
  ~TemporaryState() {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
};
template <class Core>
constexpr bool gb = std::is_same_v<Core, GB::GameBoyMachine>;
template <class Core> std::vector<uint8_t> rom(const CompiledBlock &code) {
  std::vector<uint8_t> bytes(65536);
  if constexpr (gb<Core>) {
    bytes[0x147] = 1;
    bytes[0x148] = 1;
  }
  for (const auto &guard : code.block().guards)
    if (guard.kind == IR::GuardKind::CodeBytes && guard.subject < 0x8000)
      std::copy(guard.bytes.begin(), guard.bytes.end(),
                bytes.begin() + guard.subject);
  // Identical bank content makes generation checks independent of byte checks.
  std::copy(bytes.begin() + 0x4000, bytes.begin() + 0x8000,
            bytes.begin() + 0x8000);
  bytes[0x200] = 0xfb;
  bytes[0x201] = 0x00; // EI; NOP on both existing CPUs.
  bytes[0x210] = 0x76; // HALT.
  const uint8_t configureVdp[] = {0x3e, 0x20, 0xd3, 0xbf, 0x3e,
                                  0x81, 0xd3, 0xbf, 0x18, 0xfe};
  std::copy(std::begin(configureVdp), std::end(configureVdp),
            bytes.begin() + 0x300);
  return bytes;
}
template <class Core> void pc(Core &machine, uint16_t address) {
  auto r = machine.debugRegisters();
  r[5] = address;
  machine.debugCommitRegisters(r);
  if constexpr (gb<Core>)
    require(machine.runtimeContext().read8(0xff04) ==
                machine.runtimeContext().readRegister8("DIV"),
            "reset divider backing/cache mismatch");
}
template <class Core> void prepare(Core &machine, const CompiledBlock &code) {
  machine.loadRom(rom<Core>(code));
  Plugin::DefaultStepPolicy policy;
  machine.attachExecutorPolicy(policy);
  for (const auto &guard : code.block().guards)
    if (guard.kind == IR::GuardKind::CodeBytes && guard.subject >= 0xc000)
      for (size_t n = 0; n < guard.bytes.size(); ++n)
        machine.runtimeContext().write8(guard.subject + n, guard.bytes[n]);
  auto r = machine.debugRegisters();
  r[0] = 0x8000;
  r[3] = 0xc100;
  r[4] = 0xcffe;
  r[5] = code.block().guestStart;
  require(machine.debugValidateRegisters(r), "fixture register validation");
  machine.debugCommitRegisters(r);
}
template <class Core> void equivalent(Core &a, Core &b) {
  require(a.debugRegisters() == b.debugRegisters(),
          "architectural register mismatch");
  require(a.deterministicStateFingerprint() ==
              b.deterministicStateFingerprint(),
          "machine state mismatch");
}
template <class Core> struct Compare final : InstructionRetirementSink {
  Core &baseline;
  Core &native;
  unsigned count = 0;
  std::function<void()> after;
  Compare(Core &a, Core &b) : baseline(a), native(b) {}
  InstructionRetirementDecision
  retireInstruction(const CpuFeedback &f,
                    const ExecutionSliceProgress &) override {
    auto expected = baseline.runSlice(
        {.maxInstructions = 1, .stopOnSegmentBoundary = false});
    require(expected.progress.retiredInstructions == 1,
            "baseline retirement missing");
    const auto &e = expected.lastFeedback;
    require(e.pcBefore == f.pcBefore && e.pcAfter == f.pcAfter &&
                e.retiredCycles == f.retiredCycles &&
                e.isControlFlow == f.isControlFlow &&
                e.segmentBoundaryHint == f.segmentBoundaryHint,
            "retirement feedback mismatch");
    equivalent(baseline, native);
    ++count;
    if (after)
      after();
    return InstructionRetirementDecision::continueSlice();
  }
};
template <class Core>
void rejectedWithoutMutation(Core &machine, BoundBlock &bound) {
  const auto before = machine.deterministicStateFingerprint();
  const auto result = machine.runResearchBlock(
      bound, {.maxInstructions = 16, .stopOnSegmentBoundary = false});
  require(result.progress.retiredInstructions == 0 &&
              result.progress.retiredCycles == 0,
          "guard retired an instruction");
  require(result.exitReason == ExecutionSliceExitReason::MachineBoundary,
          "guard must pause explicitly");
  require(before == machine.deterministicStateFingerprint(),
          "guard mutated state");
}
template <class Core> void ram(const CompiledBlock &code) {
  Core baseline, native;
  prepare(baseline, code);
  prepare(native, code);
  auto bound = native.bindResearchBlock(code);
  Compare<Core> compare(baseline, native);
  for (unsigned n = 0; n < 16; ++n) {
    auto result = native.runResearchBlock(
        bound, {.maxInstructions = 6, .stopOnSegmentBoundary = false},
        &compare);
    require(result.progress.retiredInstructions == 6,
            "RAM block did not complete");
  }
  require(compare.count == 96, "RAM comparison accounting");
  native.runtimeContext().write8(
      0xe002, 0x00); // Canonical RAM mirror rewrites executable bytes.
  rejectedWithoutMutation(native, bound);
  rejects([&] { native.bindResearchBlock(code); });

  // A guest store rewrites a later instruction in this same block.
  prepare(baseline, code);
  prepare(native, code);
  for (auto *m : {&baseline, &native}) {
    auto r = m->debugRegisters();
    r[0] = 0;
    r[3] = 0xc001;
    m->debugCommitRegisters(r);
  }
  auto self = native.bindResearchBlock(code);
  Compare<Core> selfCompare(baseline, native);
  auto result = native.runResearchBlock(
      self, {.maxInstructions = 6, .stopOnSegmentBoundary = false},
      &selfCompare);
  require(result.progress.retiredInstructions == 1 && selfCompare.count == 1,
          "self-modifying code did not side-exit");
  rejectedWithoutMutation(native, self);
  baseline.runSlice({.maxInstructions = 1});
  native.runSlice({.maxInstructions = 1});
  equivalent(baseline, native);
}
template <class Core> void bank(const CompiledBlock &code) {
  Core baseline, native;
  prepare(baseline, code);
  prepare(native, code);
  auto bound = native.bindResearchBlock(code);
  Compare<Core> compare(baseline, native);
  auto full = native.runResearchBlock(
      bound, {.maxInstructions = 7, .stopOnSegmentBoundary = false}, &compare);
  require(full.progress.retiredInstructions == 7,
          "bank block did not complete");
  TemporaryState state;
  native.save_state(state.path);
  native.runtimeContext().write8(gb<Core> ? 0x2000 : 0xfffe, 2);
  rejectedWithoutMutation(native, bound);
  auto rebound =
      native.bindResearchBlock(code); // Same bytes, different physical backing.
  require(native.runResearchBlock(rebound, {.maxInstructions = 1})
                  .progress.retiredInstructions == 1,
          "new bank binding rejected");
  native.load_state(state.path);
  rejectedWithoutMutation(native, bound);
  rejectedWithoutMutation(native, rebound);
  auto restored = native.bindResearchBlock(code);
  require(native.runResearchBlock(restored, {.maxInstructions = 1})
                  .progress.retiredInstructions == 1,
          "restored bank binding rejected");
}
struct Throwing final : InstructionRetirementSink {
  InstructionRetirementDecision
  retireInstruction(const CpuFeedback &,
                    const ExecutionSliceProgress &) override {
    throw std::runtime_error("injected publication failure");
  }
};
template <class Core> void bios(const CompiledBlock &code) {
  Core machine;
  prepare(machine, code);
  auto bound = machine.bindResearchBlock(code);
  machine.loadExternalBootRom(std::vector<uint8_t>(gb<Core> ? 256 : 1024));
  pc(machine, code.block().guestStart); // Keep PC and RAM bytes identical.
  rejectedWithoutMutation(machine, bound);
  auto fresh = machine.bindResearchBlock(code);
  require(machine.runResearchBlock(fresh, {.maxInstructions = 1})
                  .progress.retiredInstructions == 1,
          "BIOS mapping rebinding failed");
}
template <class Core> void transitions(const CompiledBlock &code) {
  Core baseline, native;
  prepare(baseline, code);
  prepare(native, code);
  auto bound = native.bindResearchBlock(code);
  Compare<Core> compare(baseline, native);
  compare.after = [&] {
    for (auto *m : {&baseline, &native})
      m->runtimeContext().write8(gb<Core> ? 0x2000 : 0xfffe, 2);
  };
  auto result = native.runResearchBlock(
      bound, {.maxInstructions = 7, .stopOnSegmentBoundary = false}, &compare);
  require(result.progress.retiredInstructions == 1 && compare.count == 1,
          "mapping transition did not stop at retirement");
  equivalent(baseline, native);
  prepare(baseline, code);
  prepare(native, code);
  TemporaryState checkpoint;
  native.save_state(checkpoint.path);
  auto previous = native.bindResearchBlock(code);
  Compare<Core> restore(baseline, native);
  restore.after = [&] {
    baseline.load_state(checkpoint.path);
    native.load_state(checkpoint.path);
  };
  result = native.runResearchBlock(
      previous, {.maxInstructions = 7, .stopOnSegmentBoundary = false},
      &restore);
  require(result.progress.retiredInstructions == 1 && restore.count == 1,
          "restore transition executed an old instruction");
  equivalent(baseline, native);
  rejectedWithoutMutation(native, previous);
  auto fresh = native.bindResearchBlock(code);
  Compare<Core> budget(baseline, native);
  result = native.runResearchBlock(
      fresh,
      {.maxInstructions = 7, .maxCycles = 1, .stopOnSegmentBoundary = false},
      &budget);
  require(result.progress.retiredInstructions == 1 && budget.count == 1 &&
              result.exitReason == ExecutionSliceExitReason::CycleBudget,
          "soft cycle budget boundary");
}
template <class Core> void checkpoint(const CompiledBlock &code) {
  Core native;
  prepare(native, code);
  auto bound = native.bindResearchBlock(code);
  TemporaryState valid, corrupt;
  native.save_state(valid.path);
  const auto before = native.deterministicStateFingerprint();
  std::filesystem::copy_file(valid.path, corrupt.path);
  {
    std::fstream file(corrupt.path,
                      std::ios::in | std::ios::out | std::ios::binary);
    file.seekg(-1, std::ios::end);
    char c = 0;
    file.get(c);
    file.seekp(-1, std::ios::end);
    file.put(c ^ 0x80);
  }
  rejects([&] { native.load_state(corrupt.path); });
  require(before == native.deterministicStateFingerprint(),
          "corrupt restore mutated state");
  require(native.runResearchBlock(bound, {.maxInstructions = 1})
                  .progress.retiredInstructions == 1,
          "rejected restore invalidated binding");
  native.load_state(valid.path);
  require(before == native.deterministicStateFingerprint(),
          "successful restore mismatch");
  rejectedWithoutMutation(native, bound);
  auto fresh = native.bindResearchBlock(code);
  Throwing throwing;
  rejects([&] {
    native.runResearchBlock(fresh, {.maxInstructions = 1}, &throwing);
  });
  rejects([&] { native.runSlice({.maxInstructions = 1}); });
  rejects([&] { native.load_state(corrupt.path); });
  rejects([&] { native.runSlice({.maxInstructions = 1}); });
  native.load_state(valid.path);
  rejectedWithoutMutation(native, fresh);
  auto recovered = native.bindResearchBlock(code);
  require(native.runResearchBlock(recovered, {.maxInstructions = 1})
                  .progress.retiredInstructions == 1,
          "checkpoint failed to recover research fault");
}
template <class Core> void interruptState(const CompiledBlock &code) {
  Core native;
  prepare(native, code);
  auto bound = native.bindResearchBlock(code);
  pc(native, 0x200);
  native.runSlice({.maxInstructions = 1});
  pc(native, code.block().guestStart);
  if constexpr (gb<Core>) {
    // Game Boy's canonical retirement implements delayed EI even in IR.
    TemporaryState pending;
    native.save_state(pending.path);
    Core baseline;
    prepare(baseline, code);
    baseline.load_state(pending.path);
    Compare<Core> compare(baseline, native);
    require(native.runResearchBlock(bound, {.maxInstructions = 1}, &compare)
                    .progress.retiredInstructions == 1,
            "Game Boy delayed EI retirement");
  } else {
    rejectedWithoutMutation(native, bound);
    rejects([&] { native.bindResearchBlock(code); });
    native.runSlice({.maxInstructions = 1}); // Z80 entry clears deferred EI.
  }
  pc(native, code.block().guestStart);
  auto enabled = native.bindResearchBlock(code);
  if constexpr (gb<Core>) {
    native.runtimeContext().write8(0xffff, 1);
    native.runtimeContext().write8(0xff0f, 1);
  } else {
    auto r = native.debugRegisters();
    r[14] = r[15] = r[16] = 0;
    native.debugCommitRegisters(r);
    pc(native, 0x300);
    uint64_t cycles = 0;
    while (cycles < 100000) {
      auto s = native.runSlice(
          {.maxInstructions = 256, .stopOnSegmentBoundary = false});
      require(s.progress.retiredInstructions > 0,
              "VDP IRQ preparation stalled");
      cycles += s.progress.retiredCycles;
    }
    r = native.debugRegisters();
    r[5] = code.block().guestStart;
    r[14] = r[15] = r[16] = 1;
    r[17] = 1;
    native.debugCommitRegisters(r);
  }
  rejectedWithoutMutation(native, enabled);
  rejects([&] { native.bindResearchBlock(code); });
  TemporaryState pendingIrq;
  native.save_state(pendingIrq.path);
  Core baseline;
  prepare(baseline, code);
  baseline.load_state(pendingIrq.path);
  auto reference = baseline.runSlice({.maxInstructions = 1});
  auto irq = native.runSlice({.maxInstructions = 1});
  equivalent(baseline, native);
  require(reference.lastFeedback.retiredCycles ==
              irq.lastFeedback.retiredCycles,
          "interrupt cycles mismatch");
  require(irq.progress.retiredInstructions == 1 &&
              irq.lastFeedback.pcAfter != code.block().guestStart,
          "baseline did not service pending interrupt");
  prepare(native, code);
  auto halted = native.bindResearchBlock(code);
  pc(native, 0x210);
  native.runSlice({.maxInstructions = 1});
  pc(native, code.block().guestStart);
  rejectedWithoutMutation(native, halted);
  rejects([&] { native.bindResearchBlock(code); });
}
} // namespace
int main() try {
  std::cout << "Game Boy RAM\n" << std::flush;
  ram<GB::GameBoyMachine>(emittedGameBoyRamCode());
  std::cout << "Game Gear RAM\n" << std::flush;
  ram<GameGearMachine>(emittedGameGearRamCode());
  std::cout << "Game Boy bank\n" << std::flush;
  bank<GB::GameBoyMachine>(emittedGameBoyBank());
  std::cout << "Game Gear bank\n" << std::flush;
  bank<GameGearMachine>(emittedGameGearBank());
  std::cout << "Game Boy bank boundary\n" << std::flush;
  bank<GB::GameBoyMachine>(emittedGameBoyBankBoundary());
  std::cout << "Game Gear bank boundary\n" << std::flush;
  bank<GameGearMachine>(emittedGameGearBankBoundary());
  bios<GB::GameBoyMachine>(emittedGameBoyRamCode());
  bios<GameGearMachine>(emittedGameGearRamCode());
  transitions<GB::GameBoyMachine>(emittedGameBoyBank());
  transitions<GameGearMachine>(emittedGameGearBank());
  std::cout << "Game Boy checkpoint\n" << std::flush;
  checkpoint<GB::GameBoyMachine>(emittedGameBoyRamCode());
  std::cout << "Game Gear checkpoint\n" << std::flush;
  checkpoint<GameGearMachine>(emittedGameGearRamCode());
  std::cout << "Game Boy interrupt\n" << std::flush;
  interruptState<GB::GameBoyMachine>(emittedGameBoyRamCode());
  std::cout << "Game Gear interrupt\n" << std::flush;
  interruptState<GameGearMachine>(emittedGameGearRamCode());
  std::cout << "real-core RAM, bank, checkpoint and interrupt boundary checks "
               "passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
