#include "MachineBlock.hpp"

namespace BMMQ::IR::Research {
namespace {
bool same(const GuestInstruction &a, const GuestInstruction &b) {
  if (a.address != b.address || a.length != b.length ||
      a.cyclesNotTaken != b.cyclesNotTaken || a.cyclesTaken != b.cyclesTaken ||
      a.takenCondition != b.takenCondition || a.controlFlow != b.controlFlow ||
      a.interruptSensitive != b.interruptSensitive ||
      a.operations.size() != b.operations.size())
    return false;
  for (std::size_t i = 0; i < a.operations.size(); ++i) {
    const auto &x = a.operations[i];
    const auto &y = b.operations[i];
    if (x.opcode != y.opcode || x.result != y.result ||
        x.resultType != y.resultType || x.memoryClass != y.memoryClass ||
        x.operands.size() != y.operands.size())
      return false;
    for (std::size_t j = 0; j < x.operands.size(); ++j) {
      const auto &p = x.operands[j];
      const auto &q = y.operands[j];
      if (p.kind != q.kind || p.type != q.type || p.payload != q.payload)
        return false;
    }
  }
  return true;
}
} // namespace
BoundBlock bindMachineBlock(const CompiledBlock &code, IIrCoreAdapter &adapter,
                            const Owner &owner, std::uint64_t generation,
                            std::uint64_t mapping) {
  if (!owner || code.architectureId() != adapter.architectureId())
    throw std::invalid_argument("whole-block architecture mismatch");
  const auto &block = code.block();
  if (block.instructions.size() > IrExecutionService::Limits::kMaxInstructions)
    throw std::invalid_argument("whole-block instruction budget exceeded");
  std::vector<SourceInstruction> source;
  for (const auto &i : block.instructions) {
    SourceInstruction instruction{.address = i.address, .length = i.length};
    if (!i.length || i.length > instruction.bytes.size())
      throw std::invalid_argument("whole-block source length invalid");
    for (unsigned n = 0; n < i.length; ++n) {
      bool found = false;
      for (const auto &guard : block.guards)
        if (guard.kind == GuardKind::CodeBytes &&
            i.address + n >= guard.subject &&
            i.address + n - guard.subject < guard.bytes.size()) {
          instruction.bytes[n] = guard.bytes[i.address + n - guard.subject];
          found = true;
          break;
        }
      if (!found)
        throw std::invalid_argument("whole-block source bytes missing");
    }
    source.push_back(instruction);
  }
  LoweringRequest request{source, block.mappingGeneration, 0};
  auto validation = adapter.validateLoweredBlock(request, block);
  if (!validation)
    throw std::invalid_argument("whole-block validation failed: " +
                                validation.message);
  std::string error;
  auto canonical = adapter.lower(request, &error);
  if (!canonical ||
      canonical->instructions.size() != block.instructions.size() ||
      canonical->exit != block.exit)
    throw std::invalid_argument("whole-block canonical lowering failed: " +
                                error);
  for (std::size_t n = 0; n < block.instructions.size(); ++n)
    if (!same(block.instructions[n], canonical->instructions[n]))
      throw std::invalid_argument(
          "whole-block differs from canonical CPU lowering");
  BoundBlock result{code, owner, generation};
  result.mapping_ = mapping;
  result.canonical_ = true;
  return result;
}
BoundBlock BoundBlock::remap(const Owner& owner, std::uint64_t generation, std::uint64_t mapping) const {
  if (!canonical_ || !scalarValid_ || !matches(owner, generation) || !hasLowRomCode())
    throw std::invalid_argument("binding cannot reuse canonical validation");
  auto result = *this; // Shared immutable artifact; no metadata allocation/copy.
  result.mapping_ = mapping;
  return result;
}
BoundBlock BoundBlock::remapCopied(const Owner& owner, std::uint64_t generation, std::uint64_t mapping) const {
  if (!canonical_ || !scalarValid_ || !matches(owner, generation) || !hasLowRomCode())
    throw std::invalid_argument("binding cannot reuse canonical validation");
  TIME_RESEARCH_COPY(code_.block());
  auto block = std::make_shared<Block>(code_.block());
  block->mappingGeneration = mapping;
  for (auto& guard : block->guards)
    if (guard.kind == GuardKind::MappingGeneration) guard.expected = mapping;
  BoundBlock result{CompiledBlock(block, code_.entry(), code_.architectureId(), code_.directEntry()), owner, generation};
  result.canonical_ = true;
  return result;
}
} // namespace BMMQ::IR::Research
