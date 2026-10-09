#pragma once
#include "WholeBlockCorpus.hpp"
#include <nlohmann/json.hpp>

namespace BMMQ::IR::Research {
// ROM data, symbols, physical instruction inventory, scenario obligations and
// generated code are embedded together at build time. No runtime ROM override.
const nlohmann::json &romCorpusManifest();
std::vector<CompiledBlock> emittedRomCorpus(unsigned);
inline CorpusSegment romCorpusSegment(const nlohmann::json &value) {
  CorpusSegment segment{.instructions = {}, .emitted = value.at("emitted")};
  for (const auto &item : value.at("instructions")) {
    const auto bytes = item.at("bytes").get<std::vector<std::uint8_t>>();
    SourceInstruction instruction{.address = item.at("address"),
        .length = static_cast<std::uint8_t>(bytes.size())};
    if (bytes.empty() || bytes.size() > instruction.bytes.size())
      throw std::invalid_argument("ROM corpus instruction length invalid");
    std::copy(bytes.begin(), bytes.end(), instruction.bytes.begin());
    segment.instructions.push_back(instruction);
  }
  return segment;
}
} // namespace BMMQ::IR::Research
