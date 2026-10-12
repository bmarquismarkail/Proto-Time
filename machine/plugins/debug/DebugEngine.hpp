#pragma once
#include "machine/CPU.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>

namespace BMMQ::Debug {
// One producer and one consumer. Only owned, fixed-size values cross lanes.
template <class T, std::size_t Capacity> class Queue {
  static_assert(Capacity > 1);
  std::array<T, Capacity> slots_{};
  alignas(64) std::atomic<std::uint64_t> head_{0};
  alignas(64) std::atomic<std::uint64_t> tail_{0};

public:
  bool push(const T &value) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) == Capacity)
      return false;
    slots_[head % Capacity] = value;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }
  std::optional<T> pop() noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire))
      return {};
    auto value = slots_[tail % Capacity];
    tail_.store(tail + 1, std::memory_order_release);
    return value;
  }
};
enum class Access : std::uint8_t { Read, Write, PortRead, PortWrite };
enum class TraceKind : std::uint8_t { Boundary, Access, Retirement };
struct TraceRecord {
  TraceKind kind{};
  Access access{};
  std::uint64_t generation{}, instruction{}, backing{};
  std::uint16_t address{}, value{};
  CpuFeedback feedback{};
  std::array<std::uint16_t, 20> registers{};
};
struct Breakpoint {
  std::uint16_t address{};
  std::uint64_t backing{};
  bool bankSpecific{};
};
struct Watchpoint {
  std::uint16_t first{}, last{};
  Access access{};
};
class DebugEngine {
  Queue<TraceRecord, 1024> records_;
  std::array<Breakpoint, 64> breaks_{};
  std::array<Watchpoint, 64> watches_{};
  std::size_t breakCount_{}, watchCount_{};
  std::uint64_t generation_{}, instruction_{}, codeBacking_{};
  bool active_{}, hit_{};
  bool recording_{true};
  std::atomic<std::uint64_t> lost_{0};
  void publish(TraceRecord record) noexcept {
    if (!recording_) return;
    record.generation = generation_;
    record.instruction = instruction_;
    if (!records_.push(record))
      lost_.fetch_add(1, std::memory_order_relaxed);
  }

public:
  // Execution owners may use the same exclusive instruction lease without
  // collecting a debugger trace (e.g. frame lockstep). Rules remain empty.
  explicit DebugEngine(bool recording = true) noexcept : recording_(recording) {}
  // Configuration is machine-lane, paused-only; service validates bounds.
  bool rules(std::span<const Breakpoint> breaks,
             std::span<const Watchpoint> watches) noexcept {
    if (breaks.size() > breaks_.size() || watches.size() > watches_.size())
      return false;
    breakCount_ = breaks.size();
    watchCount_ = watches.size();
    std::copy(breaks.begin(), breaks.end(), breaks_.begin());
    std::copy(watches.begin(), watches.end(), watches_.begin());
    return true;
  }
  void boundary(std::uint64_t generation) noexcept {
    generation_ = generation;
    active_ = hit_ = false;
    publish({.kind = TraceKind::Boundary});
  }
  bool breakpoint(std::uint16_t pc, std::uint64_t backing) const noexcept {
    for (std::size_t i = 0; i < breakCount_; ++i)
      if (breaks_[i].address == pc &&
          (!breaks_[i].bankSpecific || breaks_[i].backing == backing))
        return true;
    return false;
  }
  void begin(std::uint64_t backing = 0) noexcept {
    active_ = true;
    hit_ = false;
    codeBacking_ = backing;
    ++instruction_;
  }
  bool active() const noexcept { return active_; }
  void cancel() noexcept { active_ = false; }
  void access(Access kind, std::uint16_t address, std::uint8_t value,
              std::uint64_t backing) noexcept {
    if (!active_)
      return; // Inspection and paused edits never become execution evidence.
    publish({.kind = TraceKind::Access,
             .access = kind,
             .backing = backing,
             .address = address,
             .value = value});
    for (std::size_t i = 0; i < watchCount_; ++i)
      if (watches_[i].access == kind && address >= watches_[i].first &&
          address <= watches_[i].last)
        hit_ = true;
  }
  bool retire(const CpuFeedback &feedback,
              const std::array<std::uint16_t, 20> &registers) noexcept {
    active_ = false;
    publish({.kind = TraceKind::Retirement,
             .backing = codeBacking_,
             .feedback = feedback,
             .registers = registers});
    return hit_;
  }
  std::optional<TraceRecord> consume() noexcept { return records_.pop(); }
  std::uint64_t lost() const noexcept {
    return lost_.load(std::memory_order_relaxed);
  }
};
} // namespace BMMQ::Debug
