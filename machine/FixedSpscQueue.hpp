#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace BMMQ {
// Exactly one producer and one consumer; neither lane may reset a live queue.
template<class T, std::size_t Capacity> class FixedSpscQueue {
    static_assert(Capacity > 1 && (Capacity & (Capacity - 1)) == 0 && std::is_trivially_copyable_v<T>);
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
public:
    [[nodiscard]] bool push(const T& value) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) == Capacity) return false;
        slots_[head % Capacity] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] std::optional<T> pop() noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return {};
        const auto value = slots_[tail % Capacity];
        tail_.store(tail + 1, std::memory_order_release);
        return value;
    }
    // Lifecycle owner only, after both lanes have stopped.
    void resetQuiescent() noexcept { head_.store(0); tail_.store(0); }
};
}
