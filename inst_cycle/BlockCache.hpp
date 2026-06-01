#ifndef BLOCKCACHE_HPP
#define BLOCKCACHE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <mutex>
#include <atomic>
#include <map>

namespace BMMQ {

// Basic block size for block cache
constexpr std::uint32_t BASIC_BLOCK_SIZE = 256;

// Guard validity states
enum class GuardValidity : std::uint8_t {
    Valid,
    Invalidated,
    PendingInvalidate,
};

// Guard structure for protecting block accesses
struct Guard {
    std::uint32_t guardValue = 0xDEADBEEF;
    GuardValidity validity = GuardValidity::Valid;
    std::uint64_t generation = 0;

    Guard() = default;
    Guard(std::uint32_t value) : guardValue(value), validity(GuardValidity::Valid) {}
    Guard(int v) : guardValue(static_cast<std::uint32_t>(v)), validity(GuardValidity::Valid) {}
    Guard(int v, std::uint64_t gen) : guardValue(static_cast<std::uint32_t>(v)), validity(GuardValidity::Valid), generation(gen) {}
    Guard(GuardValidity v) : validity(v) { guardValue = 0xDEADBEEF; }
    Guard(GuardValidity v, std::uint64_t gen) : validity(v), generation(gen) { guardValue = 0xDEADBEEF; }

    bool isProtected() const {
        return validity == GuardValidity::Valid && guardValue == 0xDEADBEEF;
    }

    void invalidate() {
        validity = GuardValidity::Invalidated;
        guardValue = 0xCAFEBABE;
    }
};

// Cache statistics
struct CacheStats {
    std::atomic<std::uint64_t> hits{0};
    std::atomic<std::uint64_t> misses{0};
    std::atomic<std::uint64_t> invalidations{0};
    std::atomic<std::uint64_t> evictions{0};

    CacheStats() = default;
    CacheStats(std::uint64_t h, std::uint64_t m, std::uint64_t i, std::uint64_t e)
        : hits(h), misses(m), invalidations(i), evictions(e) {}

    double hitRate() const {
        auto total = hits.load() + misses.load();
        if (total == 0) return 0.0;
        return static_cast<double>(hits.load()) / total;
    }
};

// Block cache class
template <typename AddressType = std::uint16_t, typename DataType = std::vector<std::uint8_t>>
class BlockCache {
public:
    using CacheKey = AddressType;

private:
    mutable std::mutex mutex_;
    std::atomic<std::uint64_t> generation_;
    std::map<CacheKey, DataType> blocks_;
    std::map<CacheKey, Guard> guards_;
    std::map<CacheKey, std::size_t> blockSizes_;
    CacheStats stats_;
    std::uint64_t maxEntries_;

public:
    BlockCache(std::uint64_t maxEntries = 1024)
        : generation_(0), maxEntries_(maxEntries) {}

    ~BlockCache() = default;

    // Set a block with guard
    void setBlock(CacheKey address, const DataType& data, std::uint32_t guardVal) {
        std::lock_guard<std::mutex> lock(mutex_);

        const bool replacing = blocks_.find(address) != blocks_.end();
        if (!replacing && blocks_.size() >= maxEntries_) {
            const CacheKey oldestKey = blocks_.begin()->first;
            blocks_.erase(oldestKey);
            guards_.erase(oldestKey);
            blockSizes_.erase(oldestKey);
            stats_.evictions.fetch_add(1, std::memory_order_relaxed);
        }

        blocks_[address] = data;
        blockSizes_[address] = data.size();
        guards_[address] = Guard(guardVal, generation_.load(std::memory_order_relaxed));
    }

    // Get block (with thread-safe access)
    std::optional<DataType> getBlock(CacheKey address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = blocks_.find(address);
        if (it != blocks_.end()) {
            stats_.hits.fetch_add(1, std::memory_order_relaxed);
            return it->second;
        }
        stats_.misses.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    // Check if guard is valid for an address
    bool guardValid(CacheKey address) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = guards_.find(address);
        if (it == guards_.end()) return false;
        // Check if validity is Valid (not Invalidated or PendingInvalidate)
        return it->second.validity == GuardValidity::Valid;
    }

    // Check if guard is valid for a range
    bool guardValidRange(CacheKey start, CacheKey end) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [addr, guard] : guards_) {
            if (addr >= start && addr <= end && guard.validity == GuardValidity::Valid) {
                return true;
            }
        }
        return false;
    }

    // Invalidate a specific guard
    void invalidateGuard(CacheKey address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = guards_.find(address);
        if (it != guards_.end()) {
            it->second.validity = GuardValidity::Invalidated;
            stats_.invalidations.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Mark a guard for invalidation (async)
    void markGuardInvalid(CacheKey address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = guards_.find(address);
        if (it != guards_.end()) {
            it->second.validity = GuardValidity::PendingInvalidate;
        }
    }

    void invalidateRange(CacheKey start, CacheKey end) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [addr, guard] : guards_) {
            const auto sizeIt = blockSizes_.find(addr);
            const std::size_t size = sizeIt != blockSizes_.end() ? sizeIt->second : 1u;
            const auto blockEnd = static_cast<CacheKey>(addr + static_cast<CacheKey>(size - 1u));
            if (addr <= end && blockEnd >= start && guard.validity == GuardValidity::Valid) {
                guard.validity = GuardValidity::Invalidated;
                stats_.invalidations.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    void invalidateAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [addr, guard] : guards_) {
            if (guard.validity == GuardValidity::Valid) {
                stats_.invalidations.fetch_add(1, std::memory_order_relaxed);
            }
            guard.validity = GuardValidity::Invalidated;
        }
        generation_.fetch_add(1, std::memory_order_relaxed);
    }

    void recordHit() noexcept {
        stats_.hits.fetch_add(1, std::memory_order_relaxed);
    }

    void recordMiss() noexcept {
        stats_.misses.fetch_add(1, std::memory_order_relaxed);
    }

    // Get guard details
    const Guard& getGuard(CacheKey address) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = guards_.find(address);
        static Guard emptyGuard;
        return it != guards_.end() ? it->second : emptyGuard;
    }

    // Increment generation (for cache coherency)
    void incrementGeneration() {
        generation_.fetch_add(1, std::memory_order_relaxed);
    }

    // Clear all cache entries
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        blocks_.clear();
        guards_.clear();
        blockSizes_.clear();
        stats_.hits.store(0, std::memory_order_relaxed);
        stats_.misses.store(0, std::memory_order_relaxed);
        stats_.invalidations.store(0, std::memory_order_relaxed);
        stats_.evictions.store(0, std::memory_order_relaxed);
        generation_.fetch_add(1, std::memory_order_relaxed);
    }

    // Get cache size
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return blocks_.size();
    }

    // Check if address is in cache
    bool contains(CacheKey address) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return guards_.find(address) != guards_.end();
    }

    // Get stats
    CacheStats getStats() const {
        return CacheStats(stats_.hits.load(), stats_.misses.load(),
                         stats_.invalidations.load(), stats_.evictions.load());
    }

    // Get generation
    std::uint64_t generation() const {
        return generation_.load();
    }
};

// Guard invalidator utility class
template <typename AddressType = std::uint16_t>
class GuardInvalidator {
private:
    mutable std::mutex mutex_;
    std::map<AddressType, Guard> guards_;
    std::atomic<std::uint64_t> generation_{0};

public:
    GuardInvalidator() = default;

    void setGuard(AddressType address, int v, std::uint64_t gen = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        guards_[address] = Guard(v, gen);
    }

    const Guard& getGuard(AddressType address) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = guards_.find(address);
        static Guard emptyGuard;
        return it != guards_.end() ? it->second : emptyGuard;
    }

    void invalidateGuard(AddressType address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = guards_.find(address);
        if (it != guards_.end()) {
            it->second.validity = GuardValidity::Invalidated;
        }
    }

    bool guardValid(AddressType address) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = guards_.find(address);
        return it != guards_.end() && it->second.validity == GuardValidity::Valid;
    }

    template <typename AddressType2>
    bool anyGuardValid(AddressType2 start, AddressType2 end) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [addr, guard] : guards_) {
            if (addr >= start && addr <= end && guard.validity == GuardValidity::Valid) {
                return true;
            }
        }
        return false;
    }

    void invalidateAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [addr, guard] : guards_) {
            guard.validity = GuardValidity::Invalidated;
        }
    }

    void incrementGeneration() {
        generation_++;
    }

    std::uint64_t generation() const {
        return generation_.load();
    }
};

} // namespace BMMQ

#endif // BLOCKCACHE_HPP
