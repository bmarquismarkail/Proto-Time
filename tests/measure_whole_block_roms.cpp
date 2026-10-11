// Complete deterministic gameplay replays, with sampled active-instruction
// differential windows and full machine checks at every committed game tick.
#include "WholeBlockRomCorpus.hpp"
#include "AccelerationExperimentOptions.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/plugins/input/InputPlugin.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <openssl/evp.h>
#include <sstream>

namespace {
using namespace BMMQ;
using namespace BMMQ::IR::Research;
using Json = nlohmann::json;
constexpr const char *modes[] = {"baseline", "cached-block", "portable-ir", "emitted"};
constexpr unsigned windowSize = 128;
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string hash(std::span<const std::uint8_t> bytes) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned size = 0;
  require(EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr) == 1,
          "ROM corpus digest failure");
  std::ostringstream out;
  for (unsigned i = 0; i < size; ++i)
    out << std::hex << std::setfill('0') << std::setw(2) << unsigned(digest[i]);
  return out.str();
}
std::string hashJson(const Json &value) {
  const auto text = value.dump();
  return hash({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}
std::vector<std::uint8_t> romBytes(const Json &record) {
  const auto encoded = record.at("romHex").get<std::string>();
  require(encoded.size() == 131072, "ROM corpus size mismatch");
  std::vector<std::uint8_t> result;
  for (std::size_t i = 0; i < encoded.size(); i += 2)
    result.push_back(static_cast<std::uint8_t>(std::stoul(encoded.substr(i, 2), nullptr, 16)));
  require(hash(result) == record.at("romSha256").get<std::string>(), "ROM corpus hash mismatch");
  return result;
}
class Input final : public IDigitalInputSourcePlugin {
public:
  InputButtonMask mask = 0;
  InputPluginCapabilities capabilities() const noexcept override {
    return {true, false, true, true, false, true, false, false, false, true};
  }
  std::string_view name() const noexcept override { return "acceleration-ROM-replay"; }
  bool open() override { return true; }
  void close() noexcept override {}
  std::string_view lastError() const noexcept override { return {}; }
  std::optional<InputButtonMask> sampleDigitalInput() override { return mask; }
};
struct Accounting {
  std::uint64_t instructions = 0, cycles = 0, emittedInstructions = 0;
  std::uint64_t nonEmittedInstructions = 0, partialEntryInstructions = 0;
  std::uint64_t cpuBoundaryInstructions = 0, emittedInvocations = 0;
  std::uint64_t bindings = 0, mappingChanges = 0, sideExits = 0, haltPolls = 0;
  std::map<std::string, std::uint64_t> regions;
  std::map<unsigned, std::uint64_t> banks;
  std::uint64_t prefixedInstructions = 0, shadowInstructions = 0;
  std::uint64_t rebindings = 0, haltBatches = 0;
  std::array<std::uint64_t,3> sampledNanoseconds{}, sampledCalls{}; // HALT, emitted, other
  struct Timer {
    Accounting& total; bool sample; unsigned category=2;
    std::chrono::steady_clock::time_point start{};
    Timer(Accounting& a,bool enabled):total(a),sample(enabled){if(sample)start=std::chrono::steady_clock::now();}
    ~Timer(){if(sample){total.sampledNanoseconds[category]+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();++total.sampledCalls[category];}}
  };
  std::array<std::uint64_t, 9> blockLengths{};
  static constexpr std::array<const char*, 13> regionNames = {"Entry", "Irq", "Init", "CallBank1", "CallBank2", "ResetGame", "ReadInput", "Render", "SoundTick", "Tick", "Attempt", "SetVram", "Other"};
  std::array<std::uint64_t, 13> leanRegions{};
  std::array<std::uint64_t, 3> leanBanks{};
  Json json() const {
    auto regionJson = Json(regions);
    auto bankJson = Json(banks);
    for (unsigned i=0;i<leanRegions.size();++i) if(leanRegions[i]) regionJson[regionNames[i]]=leanRegions[i];
    if (std::any_of(leanBanks.begin(),leanBanks.end(),[](auto n){return n!=0;})) {
      bankJson=Json::array(); for(unsigned i=0;i<3;++i) if(leanBanks[i]) bankJson.push_back({i,leanBanks[i]});
    }
    return {{"instructions", instructions}, {"cycles", cycles},
      {"emittedInstructions", emittedInstructions}, {"nonEmittedInstructions", nonEmittedInstructions},
      {"partialEntryInstructions", partialEntryInstructions}, {"cpuBoundaryInstructions", cpuBoundaryInstructions},
      {"emittedInvocations", emittedInvocations}, {"bindings", bindings},
      {"mappingChanges", mappingChanges}, {"sideExits", sideExits}, {"haltPolls", haltPolls},
      {"regions", regionJson}, {"banks", bankJson}, {"prefixedInstructions", prefixedInstructions},
      {"shadowInstructions", shadowInstructions}, {"rebindings",rebindings}, {"haltBatches",haltBatches}, {"blockLengths",blockLengths}, {"sampledNanoseconds",sampledNanoseconds}, {"sampledCalls",sampledCalls}};
  }
};
template <class Core> Json state(Core &machine) {
  Json gameplay = Json::array();
  for (unsigned address : {0xc000u, 0xc001u, 0xc002u, 0xc003u, 0xc004u, 0xc005u,
       0xc006u, 0xc007u, 0xc008u, 0xc009u, 0xc00au, 0xc00bu, 0xc012u,
       0xc020u, 0xc021u, 0xc022u, 0xc023u, 0xc024u})
    gameplay.push_back(machine.runtimeContext().peek8(address));
  return {{"fingerprint", machine.deterministicStateFingerprint()},
          {"registers", machine.debugRegisters()}, {"gameplay", gameplay}};
}
template <class Core> struct Window final : InstructionRetirementSink {
  Core &machine;
  Json rows = Json::array();
  const Json *expected = nullptr;
  explicit Window(Core &m, const Json *reference) : machine(m), expected(reference) {}
  InstructionRetirementDecision retireInstruction(const CpuFeedback &f,
      const ExecutionSliceProgress &) override {
    auto row = state(machine);
    row["feedback"] = {f.pcBefore, f.pcAfter, f.retiredCycles,
                        unsigned(f.isControlFlow), unsigned(f.segmentBoundaryHint)};
    if (expected) require(row == expected->at(rows.size()), "ROM per-retirement differential mismatch");
    rows.push_back(std::move(row));
    return InstructionRetirementDecision::continueSlice();
  }
};

template <class Core> class Runner {
public:
  Runner(Core &machine, const Json &record, const std::vector<CompiledBlock> &code, bool native, ReplayExperimentOptions options = {})
      : machine_(machine), code_(code), native_(native), dispatch_(3 * 16384), options_(options) {
    unsigned block = 0;
    for (std::size_t s = 0; s < record.at("segments").size(); ++s) {
      const auto &segment = record.at("segments")[s];
      segments_.push_back({romCorpusSegment(segment), segment.at("region")});
      auto& added=segments_.back();
      for(unsigned n=0;n<Accounting::regionNames.size();++n) if(added.region==Accounting::regionNames[n]) added.regionId=n;
      unsigned offset = 0;
      for (const auto &instruction : segment.at("instructions")) {
        auto &entry = dispatch_.at(instruction.at("offset").get<unsigned>());
        require(!entry.valid, "ROM corpus instruction inventory overlap");
        entry = {s, offset++, block, true};
      }
      if (segment.at("emitted")) {
        require(block < code.size() && code[block].block().guestStart ==
                segment.at("instructions")[0].at("address"), "ROM artifact identity mismatch");
        ++block;
      }
    }
    require(block == code.size(), "ROM artifact count mismatch");
    bindings_.resize(block);
    recordCoreGameBoy_ = record.at("core") == "gameboy";
  }
  Accounting total;
  void advance(unsigned limit = 8, InstructionRetirementSink *observer = nullptr) {
    bool sampled = false;
    if (options_.profileTimings) {
      advanceCalls_ ^= advanceCalls_ << 13; advanceCalls_ ^= advanceCalls_ >> 17; advanceCalls_ ^= advanceCalls_ << 5;
      sampled = (advanceCalls_ & 63u) == 0;
    }
    Accounting::Timer timing(total, sampled);
    require(total.instructions < 10'000'000, "ROM replay instruction budget exhausted");
    const auto pc = machine_.runtimeContext().readRegister16("PC");
    const auto backing = machine_.debugBacking(pc);
    const unsigned bank = (backing >> 16) & 65535;
    require((backing >> 32) == 1 && bank < 3, "ROM replay left its reviewed backing");
    const auto &entry = dispatch_.at(bank * 16384 + (backing & 16383));
    if (!entry.valid)
      throw std::runtime_error("ROM replay left its declared instruction map: PC=" +
          std::to_string(pc) + " bank=" + std::to_string(bank) +
          " steps=" + std::to_string(total.instructions));
    const auto &segment = segments_.at(entry.segment);
    const auto &instructions = segment.code.instructions;
    const auto before = machine_.researchBlockState();
    if (mapping_ && mapping_ != before.mappingGeneration) {
      ++total.mappingChanges;
      if (!options_.reuseBindings) for (auto &binding : bindings_) binding.reset();
    }
    mapping_ = before.mappingGeneration;
    const bool eligible = segment.code.emitted && entry.offset == 0 && before.executionState == 0;
    const auto haltBit = recordCoreGameBoy_ ? std::uint64_t(GB::IRExecution::Halt) : std::uint64_t(GameGearIR::Halted);
    const bool batchHalt = !observer && options_.haltBatch && before.executionState == haltBit;
    const unsigned requested = batchHalt ? options_.haltBatch : eligible ? std::min<unsigned>(limit, instructions.size()) : 1;
    const ExecutionBudget budget{.maxInstructions = requested, .stopOnSegmentBoundary = false};
    const bool emitted = native_ && eligible;
    timing.category = (before.executionState & haltBit) ? 0 : emitted ? 1 : 2;
    ExecutionSliceResult result;
    if (emitted) {
      auto &binding = bindings_.at(entry.block);
      if (!binding) {
        // The host harness is paused between slices. Binding cost and mapping
        // invalidation are deliberately included in timed gameplay replays.
        TIME_RESEARCH_SCOPE(bindPhase, Phase::Bind);
        binding = machine_.bindResearchBlock(code_.at(entry.block));
        ++total.bindings;
      }
      else if(options_.reuseBindings && !binding->scalarGuardsMatch(before.mappingGeneration, before.executionState, 1)) {
        TIME_RESEARCH_SCOPE(remapPhase, Phase::Remap);
        binding=machine_.rebindResearchBlock(*binding, options_.copyBindings); ++total.rebindings;
      }
      result = machine_.runResearchBlock(*binding, budget, observer, options_.directRegisters);
      ++total.emittedInvocations;
    } else {
      if(batchHalt) {
        struct HaltBoundary final : InstructionRetirementSink {
          Core& machine; MachineBlockState before; std::uint64_t polls=0;
          HaltBoundary(Core& m, MachineBlockState b):machine(m),before(b){}
          InstructionRetirementDecision retireInstruction(const CpuFeedback&, const ExecutionSliceProgress&) override {
            ++polls;
            const auto after=machine.researchBlockState();
            return after.executionState==before.executionState && after.mappingGeneration==before.mappingGeneration
              ? InstructionRetirementDecision::continueSlice()
              : InstructionRetirementDecision::exitSlice(ExecutionSliceExitReason::MachineBoundary);
          }
        } boundary(machine_,before);
        result=machine_.runSlice(budget,&boundary); ++total.haltBatches;
        require(boundary.polls==result.progress.retiredInstructions,"HALT skipped a retirement");
      } else result = machine_.runSlice(budget, observer);
    }
    const auto retired = result.progress.retiredInstructions;
    require(retired > 0 && retired <= requested, "unexpected zero retirement/guard rejection");
    require((before.executionState && (retired == 1 || batchHalt)) || result.lastFeedback.pcBefore ==
            instructions.at(entry.offset + retired - 1).address,
            "baseline left a linear ROM segment during a batch");
    if (retired != requested && !batchHalt) {
      const auto after = machine_.researchBlockState();
      require(emitted && result.exitReason == ExecutionSliceExitReason::MachineBoundary &&
              (after.executionState != 0 || after.mappingGeneration != before.mappingGeneration),
              "unexpected native side exit; no implicit fallback");
      ++total.sideExits;
    }
    total.instructions += retired;
    total.cycles += result.progress.retiredCycles;
    if (emitted) { total.emittedInstructions += retired; ++total.blockLengths.at(retired); }
    else {
      total.nonEmittedInstructions += retired;
      if (native_ && before.executionState) total.cpuBoundaryInstructions += retired;
      if (native_ && segment.code.emitted && entry.offset) total.partialEntryInstructions += retired;
    }
    if(options_.leanAccounting) {total.leanRegions[segment.regionId]+=retired;total.leanBanks.at(bank)+=retired;}
    else {total.regions[segment.region]+=retired;total.banks[bank]+=retired;}

    if ((before.executionState & haltBit) && result.lastFeedback.pcAfter == pc) total.haltPolls += retired;
    for (unsigned i = 0; i < (batchHalt ? 0u : retired); ++i) {
      const auto op = instructions.at(entry.offset + i).bytes[0];
      if (op == 0xdd || op == 0xfd || op == 0xed || op == 0xcb) ++total.prefixedInstructions;
      if (!recordCoreGameBoy_ && (op == 0x08 || op == 0xd9)) ++total.shadowInstructions;
    }
  }
private:
  struct Entry { std::size_t segment = 0; unsigned offset = 0, block = 0; bool valid = false; };
  struct Segment { CorpusSegment code; std::string region; unsigned regionId=12; };
  Core &machine_;
  const std::vector<CompiledBlock> &code_;
  bool native_;
  std::vector<Entry> dispatch_;
  std::vector<Segment> segments_;
  std::vector<std::optional<BoundBlock>> bindings_;
  std::uint64_t mapping_ = 0;
  bool recordCoreGameBoy_ = false;
  ReplayExperimentOptions options_;
  std::uint32_t advanceCalls_=0x12345678u;
};

template <class Core> Json replay(const Json &record, const std::vector<CompiledBlock> &code,
                                  unsigned mode, bool windows, const Json *reference, ReplayExperimentOptions options = {}) {
  std::cerr << record.at("core").get<std::string>() << ' '
            << record.at("workload").get<std::string>() << ' ' << modes[mode]
            << (windows ? " correctness\n" : " timed replay\n");
#ifdef TIME_RESEARCH_PHASES
  PhaseLedger ledger;PhaseSession phaseSession(ledger);
#endif
  Input input;
  Core machine;
  machine.loadRom(romBytes(record));
  if (mode == 1) { Plugin::VisibleStatePreservingStepPolicy policy; machine.attachExecutorPolicy(policy); }
  if (mode == 2) { Plugin::PortableIrStepPolicy policy; machine.attachExecutorPolicy(policy); }
  require(machine.inputService().attachExternalAdapter(input) && machine.inputService().resume(),
          "ROM input attachment failed");
  Runner<Core> runner(machine, record, code, mode == 3, options);
  auto read = [&](unsigned address) { return machine.runtimeContext().peek8(address); };
  auto tick = [&] { return unsigned(read(0xc010) | (read(0xc011) << 8)); };
  auto wait = [&](auto predicate) { while (!predicate()) runner.advance(); };
  wait([&] { return read(0xc00c) == 1 && tick() == 2; });
  auto warmState = state(machine);
  if (reference) require(warmState == reference->at("warmupState"), "ROM startup backend mismatch");
  const auto warm = runner.total.json();
  runner.total = {};
  Json timeline = Json::array(), checks = Json::array();
#ifdef TIME_RESEARCH_PHASES
  ledger=PhaseLedger{};
#endif
  const auto start = std::chrono::steady_clock::now();
  for (const auto &command : record.at("scenarios")) {
    input.mask = command.at("mask");
    machine.inputService().publishDigitalSnapshot(input.mask, machine.inputService().currentGeneration());
    machine.serviceInput();
    require(machine.currentDigitalInputMask() == input.mask, "ROM input generation rejected");
    const auto frames = command.at("frames").get<unsigned>();
    for (unsigned frame = 0; frame < frames; ++frame) {
      const auto end = tick() + 1;
      if (windows && (frame == 0 || frame + 1 == frames)) {
        wait([&] { return machine.runtimeContext().readRegister16("PC") == record.at("readInput"); });
        const auto *expected = reference ? &reference->at("windows")[checks.size()].at("rows") : nullptr;
        Window<Core> observer(machine, expected);
        while (observer.rows.size() < windowSize)
          runner.advance(windowSize - observer.rows.size(), &observer);
        require(tick() <= end, "correctness window crossed a logical tick");
        checks.push_back({{"scenario", command.at("name")}, {"frame", frame}, {"rows", std::move(observer.rows)}});
      }
      wait([&] { return tick() >= end; });
      require(tick() == end, "ROM replay skipped a committed tick");
      auto current = state(machine);
      current.update({{"tick", end}, {"instructions", runner.total.instructions}, {"cycles", runner.total.cycles}});
      if (reference) require(current == reference->at("timeline").at(timeline.size()), "ROM committed-tick differential mismatch");
      timeline.push_back(std::move(current));
    }
    const auto &expected = romCorpusManifest().at("contract").at("scenarioEndpoints").at(command.at("name").get<std::string>());
    require(Json::array({read(0xc000), read(0xc001), read(0xc003), read(0xc004)}) == expected,
            "ROM gameplay scenario obligation failed");
    require(read(0xc012) == 1, "nested game bank was not restored");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start).count();
  const auto &total = runner.total;
  require(total.emittedInstructions + total.nonEmittedInstructions == total.instructions, "ROM execution accounting mismatch");
  for (const auto &region : romCorpusManifest().at("contract").at("requiredRegions"))
    // Init is startup coverage, deliberately outside steady gameplay timing.
    require((region == "Init" ? warm.at("regions").contains("Init") : total.json().at("regions").contains(region.get<std::string>())),
            "missing ROM hardware/control scenario coverage");
  require(total.mappingChanges > 0 && total.haltPolls > 0 && (options.leanAccounting ? total.leanBanks[1]>0 && total.leanBanks[2]>0 : total.banks.contains(1) && total.banks.contains(2)),
          "missing bank/interrupt/HALT replay coverage");
  if (record.at("core") == "gamegear" && record.at("workload") == "collect-reverse")
    require(total.prefixedInstructions > 0 && total.shadowInstructions > 0, "missing indexed/shadow game coverage");
  if (mode == 3) require(total.emittedInstructions > 0 && total.nonEmittedInstructions > 0, "missing emitted/baseline ROM coverage");
  auto result = total.json();
  result.update({{"nanoseconds", elapsed}, {"timeline", timeline}, {"timelineSha256", hashJson(timeline)},
                 {"windows", checks}, {"warmupState", warmState}, {"warmup", warm}, {"finalState", timeline.back()}});
#ifdef TIME_RESEARCH_PHASES
  Json phaseRows=Json::array();
  for(unsigned i=0;i<ledger.phases.size();++i){const auto& d=ledger.phases[i];phaseRows.push_back({{"phase",phaseNames[i]},{"calls",d.calls},{"samples",d.samples},{"nanoseconds",d.nanoseconds}});}
  result["phaseDiagnostics"]={{"phases",phaseRows},{"metadataCopies",ledger.metadataCopies},{"estimatedMetadataBytes",ledger.estimatedMetadataBytes},{"sampling","independent xorshift per phase, 1/64"},{"operationsIncludeHelpers",true}};
#endif
  return result;
}

template <class Core> void measure(unsigned caseIndex, bool smoke, Json &output) {
  const auto &record = romCorpusManifest().at("cases").at(caseIndex);
  const auto code = emittedRomCorpus(caseIndex);
  const auto reference = replay<Core>(record, code, 0, true, nullptr);
  Json correctness = {{"core", record.at("core")}, {"workload", record.at("workload")},
    {"romSha256", record.at("romSha256")}, {"scenarioSha256", record.at("scenarioSha256")},
    {"status", "passed"}, {"backends", {"cached-block", "portable-ir", "emitted"}},
    {"timeline", reference.at("timeline")}, {"windows", Json::array()}, {"coverage", Json::array()}};
  for (unsigned mode : {1u, 2u, 3u}) {
    auto result = replay<Core>(record, code, mode, true, &reference);
    result.erase("windows"); result.erase("timeline");
    result["backend"] = modes[mode];
    correctness["coverage"].push_back(std::move(result));
  }
  for (const auto &window : reference.at("windows"))
    correctness["windows"].push_back({{"scenario", window.at("scenario")},
      {"frame", window.at("frame")}, {"retirementsPerBackend", windowSize}, {"rowsSha256", hashJson(window.at("rows"))}});
  output["correctness"].push_back(std::move(correctness));
  for (unsigned repeat = 0; repeat < (smoke ? 1u : 9u); ++repeat)
    for (unsigned order = 0; order < 4; ++order) {
      const auto mode = (order + repeat) % 4;
      auto result = replay<Core>(record, code, mode, false, &reference);
      result.erase("windows"); result.erase("timeline");
      result.update({{"core", record.at("core")}, {"workload", record.at("workload")},
        {"romSha256", record.at("romSha256")}, {"scenarioSha256", record.at("scenarioSha256")},
        {"backend", modes[mode]}, {"repeat", repeat}, {"order", order}});
      output["samples"].push_back(std::move(result));
    }
}
} // namespace
int main(int argc, char **argv) try {
  const bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
  if (argc > 1 && !smoke) throw std::invalid_argument("usage: time-measure-whole-block-roms [--smoke]");
  Json output = {{"schema", "proto-time-whole-block-roms-v1"},
    {"scope", romCorpusManifest().at("contract").at("scope")},
    {"policy", "same static bank-aware segment schedule; explicit CPU side exits; unexpected guards fail closed; binding/invalidation and tick fingerprints included"},
    {"corpus", romCorpusManifest().at("contract")},
    {"manifestSha256", hashJson(romCorpusManifest())},
    {"contractSha256", romCorpusManifest().at("contractSha256")},
    {"repetitions", smoke ? 1 : 9}, {"correctness", Json::array()}, {"samples", Json::array()},
    {"romBindings", Json::array()}};
  for (unsigned i = 0; i < romCorpusManifest().at("cases").size(); ++i) {
    auto binding = romCorpusManifest().at("cases")[i];
    binding.erase("romHex"); binding.erase("segments");
    output["romBindings"].push_back(std::move(binding));
    if (romCorpusManifest().at("cases")[i].at("core") == "gameboy") measure<GB::GameBoyMachine>(i, smoke, output);
    else measure<GameGearMachine>(i, smoke, output);
  }
  std::cout << output.dump(2) << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
