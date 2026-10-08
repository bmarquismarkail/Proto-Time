#include "inst_cycle/research/WholeBlockEmitter.hpp"
#include "tests/WholeBlockFixtures.hpp"
#include <fstream>
#include <iostream>

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
