#pragma once

#include "WholeBlockFixtures.hpp"
#include <array>

namespace BMMQ::IR::Research {
// Deterministic synthetic programs, not captures from commercial games. Only
// the pilot's reviewed compute/RAM opcodes are emitted. Setup, pointer motion,
// conditional control flow, and device accesses remain explicit baseline work.
struct CorpusSegment {
  std::vector<SourceInstruction> instructions;
  bool emitted = false;
};
struct CorpusProgram {
  std::vector<std::uint8_t> rom = std::vector<std::uint8_t>(32768);
  std::vector<CorpusSegment> segments;
};
inline constexpr std::array<const char *, 3> corpusNames = {
    "compute-heavy", "ram-heavy", "mixed-devices"};

inline CorpusProgram corpusProgram(bool gb, unsigned workload) {
  if (workload >= corpusNames.size())
    throw std::invalid_argument("unknown whole-block corpus workload");
  CorpusProgram program;
  std::uint16_t pc = gb ? 0x100 : 0;
  auto append = [&](std::vector<std::vector<std::uint8_t>> bytes, bool emitted) {
    CorpusSegment segment{.instructions = {}, .emitted = emitted};
    for (const auto &instruction : bytes) {
      SourceInstruction source{.address = pc,
                               .length = static_cast<std::uint8_t>(instruction.size())};
      std::copy(instruction.begin(), instruction.end(), source.bytes.begin());
      std::copy(instruction.begin(), instruction.end(), program.rom.begin() + pc);
      pc += instruction.size();
      segment.instructions.push_back(source);
    }
    program.segments.push_back(std::move(segment));
  };
  append({{0xc3, 0x50, 0x01}}, false); // Keep Game Boy header out of executable code.
  pc = 0x150;
  if (workload == 2) {
    if (gb)
      append({{0x3e, 0x91}, {0xe0, 0x40}, {0x3e, 0x80}, {0xe0, 0x26},
              {0x3e, 0xf3}, {0xe0, 0x12}, {0x3e, 0x87}, {0xe0, 0x14}}, false);
    else
      append({{0x3e, 0x40}, {0xd3, 0xbf}, {0x3e, 0x81}, {0xd3, 0xbf},
              {0x3e, 0x90}, {0xd3, 0x7f}}, false);
  }
  const auto loop = pc;
  append({{0x21, 0x00, 0xc0}}, false); // Real guest setup, repeated each traversal.
  for (unsigned block = 0; block < 4; ++block) {
    const bool ram = workload == 1 || (workload == 2 && block % 2);
    append(ram ? std::vector<std::vector<std::uint8_t>>{
                     {0x77}, {0x34}, {0x7e}, {0x35}, {0x86}, {0x18, 0x00}}
               : std::vector<std::vector<std::uint8_t>>{
                     {0x06, 0xff}, {0x04}, {0x48}, {0x81}, {0x0d}, {0xaf}, {0x18, 0x00}},
           true);
    append({{0x23}}, false); // Advance the RAM address between blocks.
  }
  if (workload == 2) {
    if (gb)
      append({{0xf0, 0x44}, {0xe0, 0x13}, {0xf0, 0x00}}, false);
    else
      append({{0xd3, 0xbe}, {0xd3, 0x7f}, {0xdb, 0xbf}, {0xdb, 0xdc}}, false);
  }
  // Both taken and untaken paths are visited as D wraps. Each is its own
  // segment so the scheduler never assumes a conditional branch was not taken.
  append({{0x14}, {0x7a}, {0x20, 0x01}}, false);
  append({{0x00}}, false);
  append({{0xc3, static_cast<std::uint8_t>(loop),
           static_cast<std::uint8_t>(loop >> 8)}}, false);
  return program;
}

inline BlockPtr lowerCorpusSegment(bool gb, const CorpusSegment &segment) {
  LoweringRequest request{.instructions = segment.instructions,
                          .mappingGeneration = 7, .executionState = 0};
  GB::IRExecution::GameBoyCoreAdapter gameboy;
  GameGearIR::CoreAdapter gamegear;
  auto &adapter = gb ? static_cast<IIrCoreAdapter &>(gameboy)
                     : static_cast<IIrCoreAdapter &>(gamegear);
  std::string error;
  auto block = adapter.lower(request, &error);
  if (!block || block->instructions.size() != segment.instructions.size() ||
      !adapter.validateLoweredBlock(request, *block))
    throw std::runtime_error("corpus lowering failed: " + error);
  return block;
}
std::vector<CompiledBlock> emittedGameBoyCorpus(unsigned workload);
std::vector<CompiledBlock> emittedGameGearCorpus(unsigned workload);
} // namespace BMMQ::IR::Research
