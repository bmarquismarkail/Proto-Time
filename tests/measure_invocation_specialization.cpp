// Equivalent-work orchestration screen. Never feeds acceleration admission.
#define main original_synthetic_corpus_main
#include "measure_whole_block_corpus.cpp"
#undef main
#include "InvocationHookControl.hpp"

int main(int argc, char** argv) try {
  bool smoke = false, concreteGameGear = false;
  unsigned repetitions = 9, orderSeed = 20261011;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--concrete-gamegear") concreteGameGear = true;
    else if (arg == "--smoke") { smoke = true; repetitions = 1; }
    else if (arg == "--seed" && i + 1 < argc) orderSeed = std::stoul(argv[++i]);
    else if (arg == "--repetitions" && i + 1 < argc) repetitions = std::stoul(argv[++i]);
    else throw std::invalid_argument("invalid invocation experiment option");
  }
  require(repetitions == 1 || repetitions == 9, "invalid invocation repetitions");
  const char* backends[] = {"baseline", "cached-block", "portable-ir", "emitted", "typed", "concrete"};
  const unsigned modes = concreteGameGear ? 6 : 5;
  Json output = {{"schema", concreteGameGear ? "proto-time-concrete-gamegear-v1" : "proto-time-invocation-specialization-v1"},
      {"qualification", false}, {"diagnosticBuild", false}, {"profileTimings", false},
      {"admissionChanged", false}, {"repetitions", repetitions}, {"orderSeed", orderSeed},
      {"instructions", smoke ? 512 : 100000}, {"warmupInstructions", 4096},
      {"registerStrategy", "generic-host"}, {"guardStrategy", "unchanged-cached-metadata"},
      {"samples", Json::array()}, {"correctness", Json::array()},
      {"lowerBounds", Json::array()}, {"policyLookups", Json::array()}};
  auto run = [&]<class Core>(bool gb, unsigned workload) {
    const auto program = corpusProgram(gb, workload);
    const auto code = gb ? emittedGameBoyCorpus(workload) : emittedGameGearCorpus(workload);
    std::uint16_t branch = 0;
    for (const auto& segment : program.segments)
      for (const auto& instruction : segment.instructions) {
        require(instruction.bytes[0] != 0x76, "active fixture includes HALT");
        if (instruction.bytes[0] == 0x20) branch = instruction.address;
      }
    Json checked = Json::array();
    for (unsigned mode = 1; mode < modes; ++mode) {
      typedInvocationEnabled = mode >= 4;
      concreteGameGearHostEnabled = mode == 5;
      std::uint64_t taken = 0, notTaken = 0;
      for (unsigned seed : {0u, 127u, 128u, 255u}) {
        Core baseline, candidate;
        prepare(baseline, program, seed, 0);
        prepare(candidate, program, seed, std::min(mode, 3u));
        Runner<Core> runner(candidate, program, code, mode >= 3);
        Compare<Core> observer(baseline, candidate, branch);
        const auto progress = runner.advance(smoke ? 256 : 1024, &observer);
        require(observer.count == (smoke ? 256u : 1024u), "incomplete invocation parity");
        if (mode >= 3) require(progress.emittedInstructions && progress.nonEmittedInstructions,
                                "missing emitted/fallback parity");
        taken += observer.taken; notTaken += observer.notTaken;
      }
      require(taken && notTaken, "missing conditional paths");
      checked.push_back({{"backend", backends[mode]}, {"taken", taken}, {"notTaken", notTaken}});
    }
    output["correctness"].push_back({{"core", gb ? "gameboy" : "gamegear"},
        {"workload", corpusNames[workload]}, {"status", "passed"},
        {"romSha256", hashRom(program.rom)}, {"seeds", {0, 127, 128, 255}},
        {"instructionsPerSeedAndBackend", smoke ? 256 : 1024}, {"branchCoverage", checked}});
    Json expected;
    for (unsigned repeat = 0; repeat < repetitions; ++repeat) {
      for (unsigned order = 0; order < modes; ++order) {
        const unsigned mode = (order + repeat + orderSeed % modes) % modes;
        typedInvocationEnabled = mode >= 4;
        concreteGameGearHostEnabled = mode == 5;
        Core machine;
        prepare(machine, program, 128, std::min(mode, 3u));
        Runner<Core> runner(machine, program, code, mode >= 3);
        const auto warmup = runner.advance(4096);
        const auto warmState = machine.deterministicStateFingerprint();
        const auto start = std::chrono::steady_clock::now();
        const auto progress = runner.advance(smoke ? 512 : 100000);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        Json state = {{"registers", machine.debugRegisters()},
            {"fingerprint", machine.deterministicStateFingerprint()},
            {"instructions", progress.instructions}, {"cycles", progress.cycles},
            {"warmupCycles", warmup.cycles}, {"warmupFingerprint", warmState}};
        if (expected.is_null()) expected = state;
        else require(expected == state, "invocation timed endpoint mismatch");
        auto row = progress.json();
        row.update({{"core", gb ? "gameboy" : "gamegear"}, {"workload", corpusNames[workload]},
            {"backend", backends[mode]}, {"repeat", repeat}, {"order", order},
            {"nanoseconds", elapsed}, {"romSha256", hashRom(program.rom)}, {"finalState", state}});
        output["samples"].push_back(row);
      }
    }
    // Inclusive virtual machine -> policy -> backend lookup, outside active timing.
    Core machine;
    prepare(machine, program, 128, 0);
    for (unsigned repeat = 0; repeat < repetitions; ++repeat) {
      const auto count = smoke ? 1024u : 1000000u;
      std::uint64_t checksum = 0;
      const auto start = std::chrono::steady_clock::now();
      for (unsigned i = 0; i < count; ++i) checksum += lookupPolicyBackend(machine);
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
      output["policyLookups"].push_back({{"core", gb ? "gameboy" : "gamegear"},
          {"workload", corpusNames[workload]}, {"repeat", repeat}, {"calls", count},
          {"nanoseconds", elapsed}, {"checksum", checksum}, {"qualification", false}});
    }
  };
  for (unsigned workload = 0; workload < 3; ++workload) {
    if (!concreteGameGear) run.template operator()<GB::GameBoyMachine>(true, workload);
    run.template operator()<GameGearMachine>(false, workload);
  }
  for (unsigned repeat = 0; repeat < repetitions; ++repeat) {
    for (unsigned order = 0; order < 2; ++order) {
      const bool typed = (order + repeat) % 2;
      EmptyHookState state;
      const auto count = smoke ? 1024u : 1000000u;
      const auto erased = emptyHooks(state);
      const TypedHooks<EmptyHookState> concrete{state};
      const IR::Block block;
      const IR::GuestInstruction instruction;
      const IR::InterpreterResult result;
      const Progress progress;
      const auto start = std::chrono::steady_clock::now();
      for (unsigned i = 0; i < count; ++i) {
        if (typed) {
          require(concrete.callBefore(block, 0), "empty typed before");
          require(concrete.callRetire(instruction, result, progress), "empty typed retire");
        } else {
          require(erased.callBefore(block, 0), "empty erased before");
          require(erased.callRetire(instruction, result, progress), "empty erased retire");
        }
      }
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
      require(state.calls == 2 * count, "empty hook count mismatch");
      output["lowerBounds"].push_back({{"backend", typed ? "typed" : "erased"},
          {"repeat", repeat}, {"order", order}, {"calls", state.calls},
          {"nanoseconds", elapsed}, {"qualification", false},
          {"scope", "empty before/retire bodies; no CPU or device effects"}});
    }
  }
  std::cout << output.dump(2) << '\n';
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
