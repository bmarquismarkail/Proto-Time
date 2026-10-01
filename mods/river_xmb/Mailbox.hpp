#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
struct Snapshot {
  std::array<char, 41> rom{};
  std::array<char, 128> app{};
  std::uint64_t generation = 0, observations = 0;
};
// Single producer (emulation callback), single consumer (plugin worker).
// Full queues drop the new sample; neither lane ever waits on the other.
class Mailbox {
  std::array<Snapshot, 3> slots_{};
  std::atomic<unsigned> read_{0}, write_{0};

public:
  bool push(const Snapshot &value) noexcept {
    auto w = write_.load(std::memory_order_relaxed), next = (w + 1) % 3;
    if (next == read_.load(std::memory_order_acquire))
      return false;
    slots_[w] = value;
    write_.store(next, std::memory_order_release);
    return true;
  }
  bool pop(Snapshot &value) noexcept {
    auto r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire))
      return false;
    value = slots_[r];
    read_.store((r + 1) % 3, std::memory_order_release);
    return true;
  }
};
