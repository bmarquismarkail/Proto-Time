#include "inst_cycle/research/WholeBlockEmitter.hpp"
#include "tests/WholeBlockFixtures.hpp"
#include "tests/WholeBlockCorpus.hpp"
#include <fstream>
#include <iostream>
#include <regex>

int main(int argc, char **argv) try {
  if (argc != 2)
    throw std::invalid_argument("usage: time-emit-research-blocks OUTPUT.cpp");
  using namespace BMMQ::IR::Research;
  // Construct all output before publication, so invalid lowering emits no
  // partially usable translation unit. Never executed on the machine lane.
  auto source =
      emitWholeBlock(*fixture(true, false), "emittedGameBoyCompute",
                     GB::IRExecution::GameBoyCoreAdapter::kArchitectureId) +
      emitWholeBlock(*fixture(false, false), "emittedGameGearCompute",
                     BMMQ::GameGearIR::kArchitectureId) +
      emitWholeBlock(*fixture(true, true), "emittedGameBoyRam",
                     GB::IRExecution::GameBoyCoreAdapter::kArchitectureId) +
      emitWholeBlock(*fixture(false, true), "emittedGameGearRam",
                     BMMQ::GameGearIR::kArchitectureId) +
      emitWholeBlock(*controlFixture(), "emittedControl");
  for (bool gb : {true, false}) {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
      const auto start = static_cast<std::uint16_t>(scenario == 0   ? 0xc000
                                                    : scenario == 1 ? 0x4000
                                                                    : 0x3ffd);
      const auto id = std::string(gb ? "emittedGameBoy" : "emittedGameGear") +
                      (scenario == 0   ? "RamCode"
                       : scenario == 1 ? "Bank"
                                       : "BankBoundary");
      source += emitWholeBlock(
          *fixture(gb, scenario == 0, start), id,
          gb ? GB::IRExecution::GameBoyCoreAdapter::kArchitectureId
             : BMMQ::GameGearIR::kArchitectureId);
    }
  }
  for (bool gb : {true, false}) {
    const auto prefix = std::string(gb ? "emittedGameBoyCorpus" : "emittedGameGearCorpus");
    std::string factory = "namespace BMMQ::IR::Research {\nstd::vector<CompiledBlock> " +
                          prefix + "(unsigned workload) { switch(workload) {\n";
    for (unsigned workload = 0; workload < corpusNames.size(); ++workload) {
      factory += "case " + std::to_string(workload) + ": return {";
      unsigned index = 0;
      for (const auto &segment : corpusProgram(gb, workload).segments) {
        if (!segment.emitted) continue;
        const auto name = prefix + std::to_string(workload) + "Block" + std::to_string(index++);
        source += emitWholeBlock(*lowerCorpusSegment(gb, segment), name,
            gb ? GB::IRExecution::GameBoyCoreAdapter::kArchitectureId
               : BMMQ::GameGearIR::kArchitectureId);
        factory += name + "(),";
      }
      factory += "};\n";
    }
    source += "#ifndef TIME_RESEARCH_TYPED_BODIES_ONLY\n" + factory +
        "default: throw std::invalid_argument(\"unknown corpus workload\"); } } }\n#endif\n";
  }
  // Same generated operations in the generic translation unit and in the
  // experiment's core-owned invocation context. Authenticate by the generic
  // entry's address, never by PC alone or mutable metadata.
  const std::regex pattern("template<bool Direct, class Context = Invocation> void (emitted[A-Za-z0-9_]+)_entry");
  std::string dispatch = "#ifdef TIME_RESEARCH_TYPED_BODIES_ONLY\nnamespace BMMQ::IR::Research {\n"
      "template<class Context> bool dispatchTypedResearchBlock(const CompiledBlock& code, Context& c, bool direct){\n";
  for (std::sregex_iterator it(source.begin(), source.end(), pattern), end; it != end; ++it) {
    const auto id = (*it)[1].str();
    dispatch += "if(code.entry()==&" + id + "_entry<false>){if(direct){"
        "if(code.directEntry()!=&" + id + "_entry<true>)throw std::invalid_argument(\"typed direct entry mismatch\");"
        + id + "_entry<true>(c);}else{" + id + "_entry<false>(c);}return true;}\n";
  }
  source += dispatch + "return false;} }\n#endif\n";
  std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
  file << source;
  file.close();
  if (!file)
    throw std::runtime_error("generated source publication failed");
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
