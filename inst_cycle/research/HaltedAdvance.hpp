#pragma once
#include <cstdint>
namespace BMMQ::IR::Research {
// Isolated shared-runtime experiment; production never defines its opt-in macro.
inline thread_local bool haltedAdvanceEnabled = false;
struct HaltedAdvanceStats {
  std::uint64_t bulkCalls = 0;
  std::uint64_t logicalIdlePolls = 0;
  std::uint64_t canonicalSteps = 0;
};
inline thread_local HaltedAdvanceStats haltedAdvanceStats{};
}
