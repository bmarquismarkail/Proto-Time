#include "inst_cycle/research/WholeBlockEmitter.hpp"
#include "tests/WholeBlockFixtures.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

namespace {
using namespace BMMQ::IR;
using namespace BMMQ::IR::Research;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

// IR-host differential model, deliberately independent of either CPU layout.
// Guest semantics and real device retirement need separate core integration.
class TraceHost final : public InterpreterHost {
public:
  std::array<std::uint64_t, 14> registers{};
  std::array<std::uint8_t, 65536> memory{};
  std::vector<std::array<std::uint64_t, 5>> effects;
  std::uint64_t deviceCycles = 0;
  bool failWrite = false, record = true;
  std::uint64_t readRegister(std::uint32_t id, ValueType type) override {
    if (record)
      effects.push_back({1, id, unsigned(type), 0, 0});
    return registers.at(id);
  }
  void writeRegister(std::uint32_t id, ValueType type,
                     std::uint64_t value) override {
    if (failWrite)
      throw std::runtime_error("injected publication failure");
    if (record)
      effects.push_back({2, id, unsigned(type), value, 0});
    registers.at(id) = value;
  }
  std::uint64_t loadMemory(std::uint64_t address, ValueType type,
                           MemoryClass cls) override {
    if (record)
      effects.push_back({3, address, unsigned(type), unsigned(cls), 0});
    return memory.at(address);
  }
  void storeMemory(std::uint64_t address, ValueType type, MemoryClass cls,
                   std::uint64_t value) override {
    if (failWrite)
      throw std::runtime_error("injected memory publication failure");
    if (record)
      effects.push_back({4, address, unsigned(type), unsigned(cls), value});
    memory.at(address) = value;
  }
  std::uint64_t callHelper(std::uint32_t id, ValueType type,
                           std::span<const std::uint64_t> args) override {
    if (record)
      effects.push_back({5, id, unsigned(type), args.size(), 0});
    std::uint64_t value = id;
    for (auto a : args) {
      if (record)
        effects.push_back({6, a, 0, 0, 0});
      value = value * 17 + a;
    }
    return value;
  }
  void setProgramCounter(std::uint64_t address) override {
    if (record)
      effects.push_back({7, address, 0, 0, 0});
    registers[13] = address;
  }
  bool same(const TraceHost &other) const {
    return registers == other.registers && memory == other.memory &&
           effects == other.effects && deviceCycles == other.deviceCycles;
  }
};

struct Context {
  const CompiledBlock &code;
  TraceHost actual, reference;
  Interpreter interpreter;
  std::uint64_t mapping = 7, lifecycle = 3, boundLifecycle = 3, state = 0,
                helperAbi = 1;
  std::size_t invalidateAfter = 0, stopAfter = 0;
  unsigned invalidation = 0;
  bool compare = true;
  std::size_t retired = 0;
  explicit Context(const CompiledBlock &c, std::uint64_t initial = 0)
      : code(c) {
    actual.registers[0] = reference.registers[0] = initial;
    actual.registers[11] = reference.registers[11] = 0xc000;
    actual.registers[13] = reference.registers[13] = c.block().guestStart;
    for (const auto &guard : c.block().guards)
      if (guard.kind == GuardKind::CodeBytes)
        for (std::size_t n = 0; n < guard.bytes.size(); ++n)
          actual.memory.at(guard.subject + n) =
              reference.memory.at(guard.subject + n) = guard.bytes[n];
  }
  Hooks hooks() { return {this, &before, &retire}; }
  static bool before(void *opaque, const Block &b, std::size_t index) {
    auto &c = *static_cast<Context *>(opaque);
    if (c.lifecycle != c.boundLifecycle ||
        c.actual.registers[13] != b.instructions.at(index).address)
      return false;
    for (const auto &g : b.guards) {
      switch (g.kind) {
      case GuardKind::MappingGeneration:
        if (c.mapping != g.expected)
          return false;
        break;
      case GuardKind::HelperAbi:
        if (c.helperAbi != g.expected)
          return false;
        break;
      case GuardKind::ExecutionState:
        if ((c.state & g.mask) != (g.expected & g.mask))
          return false;
        break;
      case GuardKind::CodeBytes:
        for (std::size_t n = 0; n < g.bytes.size(); ++n)
          if (c.actual.memory.at(g.subject + n) != g.bytes[n])
            return false;
        break;
      }
    }
    return true;
  }
  static bool retire(void *opaque, const GuestInstruction &i,
                     const InterpreterResult &r, const Progress &progress) {
    auto &c = *static_cast<Context *>(opaque);
    if (c.compare) {
      const auto expected = c.interpreter.execute(i, c.reference);
      require(r.branchTaken == expected.branchTaken &&
                  r.exitRequested == expected.exitRequested &&
                  r.cycleCondition == expected.cycleCondition &&
                  r.retirementReached == expected.retirementReached,
              "instruction result differs");
      require(c.actual.same(c.reference),
              "register/memory/ordered effects differ before retirement");
    }
    const auto cycles = r.cycleCondition ? i.cyclesTaken : i.cyclesNotTaken;
    c.actual.deviceCycles += cycles;
    c.reference.deviceCycles += cycles;
    ++c.retired;
    require(progress.instructions == c.retired &&
                progress.cycles == c.actual.deviceCycles,
            "retirement accounting differs");
    // Simulate owner effects at a retirement boundary. The next emitted
    // instruction must not mutate anything after a changed generation.
    if (c.retired == c.invalidateAfter) {
      if (c.invalidation == 0)
        ++c.mapping;
      if (c.invalidation == 1)
        ++c.lifecycle;
      if (c.invalidation == 2) {
        ++c.actual.memory.at(c.code.block().guestStart);
        ++c.reference.memory.at(c.code.block().guestStart);
      }
      if (c.invalidation == 3)
        c.state = ~std::uint64_t{0};
      if (c.invalidation == 4)
        ++c.helperAbi;
      if (c.invalidation == 5)
        ++c.actual.registers[13];
    }
    return c.retired != c.stopAfter;
  }
};

void checkWx(const CompiledBlock &code) {
  std::ifstream maps("/proc/self/maps");
  require(bool(maps), "cannot inspect executable protections");
  const auto address = reinterpret_cast<std::uintptr_t>(code.entry());
  std::string line;
  while (std::getline(maps, line)) {
    std::istringstream input(line);
    std::string range, permissions;
    input >> range >> permissions;
    const auto dash = range.find('-');
    const auto first = std::stoull(range.substr(0, dash), nullptr, 16);
    const auto last = std::stoull(range.substr(dash + 1), nullptr, 16);
    if (address >= first && address < last) {
      require(permissions.find('x') != std::string::npos &&
                  permissions.find('w') == std::string::npos,
              "generated code is not sealed RX");
      return;
    }
  }
  throw std::runtime_error("generated entry not mapped");
}
void differential(const CompiledBlock &code) {
  checkWx(code);
  for (unsigned initial = 0; initial < 256; ++initial) {
    Context c(code, initial);
    const auto progress = code.execute(c.actual, c.hooks(), 100);
    require(progress.instructions == code.block().instructions.size(),
            "block failed to retire all instructions");
    require(c.actual.same(c.reference), "final state differs");
  }
  Context zero(code);
  require(code.execute(zero.actual, zero.hooks(), 0).exit ==
              Exit::InstructionBudget,
          "zero instruction budget failed");
  require(zero.actual.same(zero.reference), "zero budget mutated state");
  Context zeroCycles(code);
  require(code.execute(zeroCycles.actual, zeroCycles.hooks(), 100, 0).exit ==
              Exit::CycleBudget,
          "zero cycle budget failed");
  require(zeroCycles.actual.same(zeroCycles.reference),
          "zero cycle budget mutated state");
  Context one(code);
  require(code.execute(one.actual, one.hooks(), 1).instructions == 1,
          "instruction budget overshot");
  Context softCycles(code);
  const auto soft = code.execute(softCycles.actual, softCycles.hooks(), 100, 1);
  require(soft.instructions == 1 && soft.exit == Exit::CycleBudget,
          "soft cycle boundary failed");
  for (unsigned kind = 0; kind < 6; ++kind) {
    Context c(code);
    c.invalidateAfter = 1;
    c.invalidation = kind;
    const auto progress = code.execute(c.actual, c.hooks(), 100);
    // The synthetic control block has no core guards; only test PC/lifecycle.
    if (code.block().guards.empty() && kind != 1 && kind != 5)
      continue;
    require(progress.instructions == 1 && progress.exit == Exit::Guard,
            "invalidation continued execution");
  }
  Context observer(code);
  observer.stopAfter = 1;
  require(code.execute(observer.actual, observer.hooks(), 100).exit ==
              Exit::Observer,
          "observer stop ignored");
  if (!code.block().guards.empty())
    for (unsigned kind = 0; kind < 5; ++kind) {
      Context stale(code);
      if (kind == 0)
        ++stale.mapping;
      if (kind == 1)
        ++stale.lifecycle;
      if (kind == 2)
        ++stale.actual.memory.at(code.block().guestStart);
      if (kind == 3)
        stale.state = ~std::uint64_t{0};
      if (kind == 4)
        ++stale.helperAbi;
      const auto effects = stale.actual.effects.size();
      require(code.execute(stale.actual, stale.hooks(), 100).instructions ==
                      0 &&
                  stale.actual.effects.size() == effects,
              "stale binding mutated state before rejection");
    }
  Context wrongPc(code);
  ++wrongPc.actual.registers[13];
  require(code.execute(wrongPc.actual, wrongPc.hooks(), 100).instructions == 0,
          "PC guard ignored");
  Context failure(code);
  failure.actual.failWrite = true;
  bool threw = false;
  try {
    (void)code.execute(failure.actual, failure.hooks(), 100);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  require(threw && failure.retired == 0,
          "partial execution was retried or retired");
}
void rejectedEmission() {
  const auto b = fixture(true, false);
  for (const std::string &id : std::vector<std::string>{
           "", "1invalid", "unsafe();", "for", std::string(97, 'a')}) {
    bool rejected = false;
    try {
      (void)emitWholeBlock(*b, id);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "untrusted identifier accepted");
  }
  auto corrupt = *b;
  corrupt.instructions.front().operations.pop_back();
  bool rejected = false;
  try {
    (void)emitWholeBlock(corrupt, "emittedValidIdentifier");
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "unretired IR accepted");
  BlockBuilder builder(0);
  builder.beginInstruction(0, 1, 4);
  (void)builder.emitValue(Opcode::ShiftLeft, ValueType::I8,
                          {Operand::immediate(1, ValueType::I8),
                           Operand::immediate(2, ValueType::I8)});
  builder.endInstruction();
  rejected = false;
  try {
    (void)emitWholeBlock(*builder.finish(), "emittedOutsidePilot");
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "pilot expanded implicitly");
}
void contractFailures() {
  const auto code = emittedGameBoyCompute();
  Context c(code);
  bool rejected = false;
  try {
    (void)code.execute(c.actual, {}, 100);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "missing hooks accepted");
  Invocation invocation(code.block(), c.actual, c.hooks(), 100, 1000);
  InterpreterResult result;
  result.retirementReached = true;
  rejected = false;
  try {
    (void)invocation.retire(0, result);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "retirement without entry accepted");
  require(invocation.begin(0), "valid entry rejected");
  rejected = false;
  try {
    (void)invocation.begin(0);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "duplicate begin accepted");
  auto mutableMetadata = std::make_shared<Block>(code.block());
  CompiledBlock copied(mutableMetadata, code.entry());
  mutableMetadata->instructions[0].address = 999;
  require(copied.block().instructions[0].address ==
              code.block().instructions[0].address,
          "compiled metadata borrowed a mutable alias");
  CompiledBlock incomplete(
      std::make_shared<const Block>(code.block()),
      +[](Invocation &x) { (void)x.begin(0); });
  rejected = false;
  try {
    (void)incomplete.execute(c.actual, c.hooks(), 100);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "entry omitted retirement silently");
  CompiledBlock empty(
      std::make_shared<const Block>(code.block()), +[](Invocation &) {});
  rejected = false;
  try {
    (void)empty.execute(c.actual, c.hooks(), 100);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "entry returned without execution silently");
  Context stopped(code);
  stopped.stopAfter = 1;
  Invocation stoppedInvocation(code.block(), stopped.actual, stopped.hooks(),
                               100, 1000);
  require(stoppedInvocation.begin(0), "entry failed");
  auto executed =
      stopped.interpreter.execute(code.block().instructions[0], stopped.actual);
  require(!stoppedInvocation.retire(0, executed), "observer stop failed");
  rejected = false;
  try {
    (void)stoppedInvocation.begin(1);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "terminal invocation restarted");
}

nlohmann::json measure(const CompiledBlock &code, bool emitted) {
  Context c(code);
  c.compare = false;
  c.actual.record = false;
  c.reference.record = false;
  const auto execute = [&] {
    c.retired = 0;
    c.actual.deviceCycles = 0;
    c.reference.deviceCycles = 0;
    Progress progress;
    if (emitted)
      progress = code.execute(c.actual, c.hooks(), 100);
    else {
      Invocation invocation(code.block(), c.actual, c.hooks(), 100,
                            std::numeric_limits<std::uint64_t>::max());
      for (std::size_t n = 0; n < code.block().instructions.size(); ++n) {
        if (!invocation.begin(n))
          break;
        const auto result =
            c.interpreter.execute(code.block().instructions[n], c.actual);
        if (!invocation.retire(n, result))
          break;
      }
      progress = invocation.progress();
    }
    require(progress.instructions == code.block().instructions.size(),
            "measurement did not finish block");
  };
  for (unsigned n = 0; n < 4096; ++n)
    execute();
  const auto start = std::chrono::steady_clock::now();
  for (unsigned n = 0; n < 10000; ++n)
    execute();
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  return {{"nanoseconds", elapsed},
          {"registers", c.actual.registers},
          {"ramByte", c.actual.memory[0xc000]},
          {"cyclesPerBlock", c.actual.deviceCycles}};
}
void measurements() {
  nlohmann::json output = {
      {"schema", 1},
      {"scope", "IR-host model only; excludes real CPU/device execution and "
                "native admission"},
      {"blocks", 10000},
      {"warmupBlocks", 4096},
      {"repetitions", 9},
      {"samples", nlohmann::json::array()}};
  for (unsigned fixtureId = 0; fixtureId < 4; ++fixtureId) {
    auto code = fixtureId == 0   ? emittedGameBoyCompute()
                : fixtureId == 1 ? emittedGameGearCompute()
                : fixtureId == 2 ? emittedGameBoyRam()
                                 : emittedGameGearRam();
    for (unsigned repeat = 0; repeat < 9; ++repeat) {
      nlohmann::json portable, emitted;
      if (repeat % 2) {
        emitted = measure(code, true);
        portable = measure(code, false);
      } else {
        portable = measure(code, false);
        emitted = measure(code, true);
      }
      require(portable["registers"] == emitted["registers"] &&
                  portable["ramByte"] == emitted["ramByte"] &&
                  portable["cyclesPerBlock"] == emitted["cyclesPerBlock"],
              "measurement state differs");
      output["samples"].push_back({{"fixture", fixtureId},
                                   {"repeat", repeat},
                                   {"portable", portable},
                                   {"emitted", emitted}});
    }
  }
  std::cout << output.dump(2) << '\n';
}

} // namespace
int main(int argc, char **argv) try {
  if (argc == 2 && std::string(argv[1]) == "--measure") {
    measurements();
    return 0;
  }
  if (argc != 1)
    throw std::invalid_argument(
        "usage: time-smoke-research-whole-block [--measure]");
  differential(emittedGameBoyCompute());
  differential(emittedGameGearCompute());
  differential(emittedGameBoyRam());
  differential(emittedGameGearRam());
  differential(emittedControl());
  rejectedEmission();
  contractFailures();
  std::cout << "whole-block IR differential: 1280 blocks; ordered effects, "
               "budgets, guards, RX and exceptions passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
