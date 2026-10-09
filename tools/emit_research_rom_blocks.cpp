#include "inst_cycle/research/WholeBlockEmitter.hpp"
#include "tests/WholeBlockRomCorpus.hpp"
#include "space/CoreModel.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char **argv) try {
  if (argc != 3)
    throw std::invalid_argument("usage: time-emit-research-rom-blocks MANIFEST OUTPUT.cpp");
  using namespace BMMQ::IR::Research;
  std::ifstream input(argv[1]);
  nlohmann::json manifest;
  input >> manifest;
  if (manifest.at("schema") != "proto-time-rom-corpus-build-v1")
    throw std::invalid_argument("ROM corpus schema mismatch");
  std::string source = "#include \"tests/WholeBlockRomCorpus.hpp\"\n";
  std::string factory = "namespace BMMQ::IR::Research {\nstd::vector<CompiledBlock> emittedRomCorpus(unsigned c) { switch(c) {\n";
  unsigned caseIndex = 0;
  for (const auto &record : manifest.at("cases")) {
    const bool gb = record.at("core") == "gameboy";
    factory += "case " + std::to_string(caseIndex) + ": return {";
    unsigned index = 0;
    for (const auto &item : record.at("segments")) {
      const auto segment = romCorpusSegment(item);
      // The canonical shared decoder independently validates the physical Z80
      // inventory, including baseline instructions which are never lowered.
      for (const auto &instruction : segment.instructions)
        if (!gb && BMMQ::Space::z80InstructionLength(
            std::span(instruction.bytes.data(), instruction.length)) != instruction.length)
          throw std::invalid_argument("ROM Z80 inventory disagrees with canonical instruction length");
      if (!segment.emitted) continue;
      const auto name = "emittedRom" + std::to_string(caseIndex) + "Block" + std::to_string(index++);
      source += emitWholeBlock(*lowerCorpusSegment(gb, segment), name,
          gb ? GB::IRExecution::GameBoyCoreAdapter::kArchitectureId : BMMQ::GameGearIR::kArchitectureId);
      factory += name + "(),";
    }
    factory += "};\n";
    ++caseIndex;
  }
  source += factory + "default: throw std::invalid_argument(\"unknown ROM corpus case\"); } }\n";
  source += "const nlohmann::json& romCorpusManifest() { static const auto data = nlohmann::json::parse(R\"romdata(" +
      manifest.dump() + ")romdata\"); return data; } }\n";
  std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
  output << source;
  output.close();
  if (!output) throw std::runtime_error("ROM corpus source publication failed");
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
