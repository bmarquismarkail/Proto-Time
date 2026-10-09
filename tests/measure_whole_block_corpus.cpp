// Real-machine comparisons of synthetic representative execution patterns.
#include "WholeBlockCorpus.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <sstream>

namespace {
using namespace BMMQ;
using namespace BMMQ::IR::Research;
using Json = nlohmann::json;
constexpr const char *modes[] = {"baseline", "cached-block", "portable-ir", "emitted"};
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
std::string hashRom(const std::vector<std::uint8_t> &bytes) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned size = 0;
  require(EVP_Digest(bytes.data(), bytes.size(), hash, &size, EVP_sha256(), nullptr) == 1,
          "ROM hash failure");
  std::ostringstream out;
  for (unsigned i = 0; i < size; ++i)
    out << std::hex << std::setw(2) << std::setfill('0') << unsigned(hash[i]);
  return out.str();
}
template <class Core>
void prepare(Core &machine, const CorpusProgram &program, unsigned seed, unsigned mode) {
  machine.loadRom(program.rom);
  if (mode == 1) {
    Plugin::VisibleStatePreservingStepPolicy policy;
    machine.attachExecutorPolicy(policy);
  } else if (mode == 2) {
    Plugin::PortableIrStepPolicy policy;
    machine.attachExecutorPolicy(policy);
  }
  auto registers = machine.debugRegisters();
  registers[0] = seed << 8; // A, with valid zero flags.
  registers[2] = seed << 8; // D traverses both conditional paths.
  registers[3] = 0xc000;
  require(machine.debugValidateRegisters(registers), "invalid corpus registers");
  machine.debugCommitRegisters(registers);
  for (unsigned i = 0; i < 4; ++i)
    machine.debugCommitByte(0xc000 + i, static_cast<std::uint8_t>(seed + i));
}

struct Accounting {
  std::uint64_t instructions = 0, cycles = 0, emittedInstructions = 0;
  std::uint64_t nonEmittedInstructions = 0, partialEntryInstructions = 0;
  std::uint64_t emittedInvocations = 0;
  Json json() const {
    return {{"instructions", instructions}, {"cycles", cycles},
            {"emittedInstructions", emittedInstructions},
            {"nonEmittedInstructions", nonEmittedInstructions},
            {"partialEntryInstructions", partialEntryInstructions},
            {"emittedInvocations", emittedInvocations}};
  }
};
template <class Core> class Runner {
public:
  Runner(Core &machine, const CorpusProgram &program,
         const std::vector<CompiledBlock> &code, bool emitted)
      : machine_(machine), program_(program), emitted_(emitted), dispatch_(65536) {
    const auto saved = machine_.debugRegisters();
    unsigned block = 0;
    for (std::size_t s = 0; s < program.segments.size(); ++s) {
      const auto &segment = program.segments[s];
      for (std::size_t i = 0; i < segment.instructions.size(); ++i) {
        const auto address = segment.instructions[i].address;
        require(!dispatch_.at(address).valid, "overlapping corpus instruction");
        dispatch_.at(address) = {s, i, block, true};
      }
      if (segment.emitted) {
        require(block < code.size(), "missing emitted corpus block");
        require(code[block].block().guestStart == segment.instructions.front().address,
                "corpus artifact PC mismatch");
        if (emitted_) {
          auto registers = saved;
          registers[5] = segment.instructions.front().address;
          machine_.debugCommitRegisters(registers);
          bindings_.push_back(machine_.bindResearchBlock(code[block]));
        }
        ++block;
      }
    }
    require(block == code.size(), "extra emitted corpus block");
    machine_.debugCommitRegisters(saved); // All binding/allocation is outside timing.
  }
  Accounting advance(std::uint64_t count, InstructionRetirementSink *observer = nullptr) {
    Accounting total;
    while (total.instructions < count) {
      const auto pc = machine_.runtimeContext().readRegister16("PC");
      const auto &entry = dispatch_.at(pc);
      require(entry.valid, "corpus left its declared instruction map");
      const auto &segment = program_.segments[entry.segment];
      const auto requested = std::min<std::uint64_t>(count - total.instructions,
          segment.instructions.size() - entry.offset);
      const bool native = emitted_ && segment.emitted && entry.offset == 0;
      const ExecutionBudget budget{.maxInstructions = requested, .stopOnSegmentBoundary = false};
      const auto result = native
          ? machine_.runResearchBlock(bindings_.at(entry.block), budget, observer)
          : machine_.runSlice(budget, observer);
      require(result.progress.retiredInstructions == requested,
              "corpus guard/retirement failure; no implicit fallback");
      total.instructions += requested;
      total.cycles += result.progress.retiredCycles;
      if (native) {
        total.emittedInstructions += requested;
        ++total.emittedInvocations;
      } else {
        total.nonEmittedInstructions += requested;
        if (emitted_ && segment.emitted) total.partialEntryInstructions += requested;
      }
    }
    require(total.emittedInstructions + total.nonEmittedInstructions == count,
            "corpus coverage accounting mismatch");
    return total;
  }
private:
  struct Entry {
    std::size_t segment = 0, offset = 0, block = 0;
    bool valid = false;
  };
  Core &machine_;
  const CorpusProgram &program_;
  bool emitted_;
  std::vector<Entry> dispatch_;
  std::vector<BoundBlock> bindings_;
};

template <class Core> struct Compare final : InstructionRetirementSink {
  Core &baseline, &candidate;
  std::uint64_t count = 0;
  std::uint64_t taken = 0, notTaken = 0;
  std::uint16_t branch;
  Compare(Core &a, Core &b, std::uint16_t pc) : baseline(a), candidate(b), branch(pc) {}
  InstructionRetirementDecision retireInstruction(const CpuFeedback &feedback,
      const ExecutionSliceProgress &) override {
    const auto expected = baseline.runSlice({.maxInstructions = 1, .stopOnSegmentBoundary = false});
    const auto &f = expected.lastFeedback;
    require(expected.progress.retiredInstructions == 1, "baseline did not retire");
    require(f.pcBefore == feedback.pcBefore && f.pcAfter == feedback.pcAfter &&
            f.retiredCycles == feedback.retiredCycles &&
            f.isControlFlow == feedback.isControlFlow &&
            f.segmentBoundaryHint == feedback.segmentBoundaryHint, "retirement feedback mismatch");
    require(baseline.debugRegisters() == candidate.debugRegisters(), "register mismatch");
    require(baseline.deterministicStateFingerprint() == candidate.deterministicStateFingerprint(),
            "per-instruction machine fingerprint mismatch");
    if (feedback.pcBefore == branch) {
      if (feedback.pcAfter == branch + 3u) ++taken;
      else {
        require(feedback.pcAfter == branch + 2u, "conditional branch target mismatch");
        ++notTaken;
      }
    }
    ++count;
    return InstructionRetirementDecision::continueSlice();
  }
};
template <class Core>
void measure(bool gb, unsigned workload, bool smoke, Json &output) {
  const auto program = corpusProgram(gb, workload);
  const auto code = gb ? emittedGameBoyCorpus(workload) : emittedGameGearCorpus(workload);
  const std::uint64_t checked = smoke ? 256 : 1024;
  Json correctness = {{"core", gb ? "gameboy" : "gamegear"},
      {"workload", corpusNames[workload]}, {"romSha256", hashRom(program.rom)},
      {"instructionsPerSeedAndBackend", checked}, {"seeds", {0, 127, 128, 255}},
      {"backends", {"cached-block", "portable-ir", "emitted"}},
      {"status", "passed"}};
  // Full fingerprints/feedback at every retirement, including explicit baseline
  // segments, are checked separately from the timed runs.
  std::uint16_t branch = 0;
  for (const auto &segment : program.segments)
    for (const auto &instruction : segment.instructions)
      if (instruction.bytes[0] == 0x20) branch = instruction.address;
  require(branch != 0, "missing corpus conditional branch");
  correctness["branchCoverage"] = Json::array();
  for (unsigned mode : {1u, 2u, 3u}) {
    std::uint64_t taken = 0, notTaken = 0;
    for (unsigned seed : {0u, 127u, 128u, 255u}) {
      Core baseline, candidate;
      prepare(baseline, program, seed, 0);
      prepare(candidate, program, seed, mode);
      Runner<Core> runner(candidate, program, code, mode == 3);
      Compare<Core> observer(baseline, candidate, branch);
      const auto progress = runner.advance(checked, &observer);
      require(observer.count == checked, "differential retirement accounting mismatch");
      if (mode == 3) require(progress.emittedInstructions > 0 && progress.nonEmittedInstructions > 0,
                             "corpus did not exercise both execution paths");
      taken += observer.taken;
      notTaken += observer.notTaken;
    }
    require(taken > 0 && notTaken > 0, "both conditional paths must be checked");
    correctness["branchCoverage"].push_back({{"backend", modes[mode]},
                                            {"taken", taken}, {"notTaken", notTaken}});
  }
  output["correctness"].push_back(std::move(correctness));
  Json reference;
  const std::uint64_t measured = smoke ? 512 : 100000;
  for (unsigned repeat = 0; repeat < (smoke ? 1u : 9u); ++repeat) {
    for (unsigned order = 0; order < 4; ++order) {
      const unsigned mode = (order + repeat) % 4;
      Core machine;
      prepare(machine, program, 128, mode);
      Runner<Core> runner(machine, program, code, mode == 3);
      const auto warmup = runner.advance(4096);
      const auto warmState = machine.deterministicStateFingerprint();
      const auto start = std::chrono::steady_clock::now();
      const auto progress = runner.advance(measured);
      const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
      Json state = {{"fingerprint", machine.deterministicStateFingerprint()},
                    {"registers", machine.debugRegisters()}, {"cycles", progress.cycles},
                    {"warmupFingerprint", warmState}, {"warmupCycles", warmup.cycles}};
      if (reference.is_null()) reference = state;
      require(state == reference, "timed/warmup backend state or cycle mismatch");
      Json sample = progress.json();
      sample.update(state);
      sample.update({{"core", gb ? "gameboy" : "gamegear"}, {"workload", corpusNames[workload]},
                     {"backend", modes[mode]}, {"repeat", repeat}, {"order", order},
                     {"romSha256", hashRom(program.rom)}, {"nanoseconds", nanos}});
      output["samples"].push_back(std::move(sample));
    }
  }
}
} // namespace

int main(int argc, char **argv) try {
  const bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
  if (argc > 1 && !smoke)
    throw std::invalid_argument("usage: time-measure-whole-block-corpus [--smoke]");
  Json output = {{"schema", "proto-time-whole-block-corpus-v1"},
      {"scope", "synthetic representative patterns on real cores; no game coverage or backend admission"},
      {"policy", "explicit segment dispatch; emitted guards fail closed; device/control segments use baseline"},
      {"warmupInstructions", 4096}, {"measuredInstructions", smoke ? 512 : 100000},
      {"repetitions", smoke ? 1 : 9}, {"correctness", Json::array()}, {"samples", Json::array()}};
  for (bool gb : {true, false}) for (unsigned workload = 0; workload < corpusNames.size(); ++workload) {
    if (gb) measure<GB::GameBoyMachine>(true, workload, smoke, output);
    else measure<GameGearMachine>(false, workload, smoke, output);
  }
  std::cout << output.dump(2) << '\n'; // Publish only after every correctness check passes.
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
