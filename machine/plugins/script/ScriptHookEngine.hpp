#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace BMMQ::Script {
// The only live actions are fixed counters and a pause latch. Interpreter code,
// file access and arbitrary mutation never enter the machine lane.
struct HookAction {
  std::uint64_t first{1}, period{1};
  bool pause{};
};
struct HookCapabilities {
  bool deterministic{}, snapshotAware{}, liveCallbackSafe{}, boundedExecution{};
};
class ScriptHookEngine {
  std::array<HookAction, 8> actions_{};
  std::array<std::uint64_t, 8> counts_{};
  std::uint64_t generation_{}, retired_{}, rejected_{}, invalidations_{};
  unsigned length_{};

public:
  bool install(std::span<const HookAction> actions, HookCapabilities caps,
               std::uint64_t generation, bool paused) noexcept {
    if (!paused || !caps.deterministic || !caps.snapshotAware ||
        !caps.liveCallbackSafe || !caps.boundedExecution ||
        actions.size() > actions_.size()) {
      ++rejected_;
      return false;
    }
    for (const auto &action : actions)
      if (!action.first || !action.period) {
        ++rejected_;
        return false;
      }
    for (unsigned i = 0; i < actions.size(); ++i)
      actions_[i] = actions[i];
    length_ = actions.size();
    generation_ = generation;
    retired_ = 0;
    counts_.fill(0);
    return true;
  }
  void invalidate(std::uint64_t generation) noexcept {
    if (generation_ != generation) {
      if (length_)
        ++invalidations_;
      length_ = 0;
      generation_ = generation;
      retired_ = 0;
    }
  }
  bool retire(std::uint64_t generation) noexcept {
    invalidate(generation);
    ++retired_;
    bool pause = false;
    for (unsigned i = 0; i < length_; ++i) {
      const auto &action = actions_[i];
      if (retired_ >= action.first &&
          (retired_ - action.first) % action.period == 0) {
        ++counts_[i];
        pause |= action.pause;
      }
    }
    return pause;
  }
  std::array<std::uint64_t, 8> counters() const noexcept { return counts_; }
  std::uint64_t rejected() const noexcept { return rejected_; }
  std::uint64_t invalidations() const noexcept { return invalidations_; }
  void clear() noexcept { length_ = 0; }
};
} // namespace BMMQ::Script
