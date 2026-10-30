#pragma once
#include <cstdint>
namespace BMMQ::IR::Research {
// Research-only switches. Existing corpus entrypoints keep all defaults off.
struct ReplayExperimentOptions {
  bool leanAccounting = false;
  unsigned haltBatch = 0;
  bool reuseBindings = false;
  bool directRegisters = false;
  bool profileTimings = false;
  bool copyBindings = false;
};
}
