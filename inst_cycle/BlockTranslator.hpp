#ifndef BLOCKTRANSLATOR_HPP
#define BLOCKTRANSLATOR_HPP

#include <cstdint>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <map>
#include <vector>
#include <string>

namespace BMMQ {

// Native instruction handler type
using NativeInstructionHandler = std::function<void()>;

// Block translator interface
class BlockTranslator {
public:
    virtual ~BlockTranslator() = default;
    virtual bool canTranslate(int opcodeLength) const = 0;
    virtual void setNativeHandler(uint8_t opcodeLength, NativeInstructionHandler handler) = 0;
    virtual void clear() = 0;
    virtual std::vector<NativeInstructionHandler> getHandlers() const = 0;
};

// Concrete block translator implementation for Game Boy
template <typename AddressType = std::uint16_t, typename DataType = std::vector<std::uint8_t>, typename RegType = std::uint16_t>
class BlockTranslatorImpl : public BlockTranslator {
private:
    std::map<std::uint8_t, NativeInstructionHandler> nativeOpcodeHandlers_;
    std::map<AddressType, DataType> cachedBlocks_;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> translationCount_{0};
    std::atomic<std::uint64_t> cacheHitCount_{0};
    std::atomic<std::uint64_t> cacheMissCount_{0};
    mutable std::mutex mutex_;

public:
    BlockTranslatorImpl() = default;

    // Set handler for a specific opcode length
    bool canTranslate(int opcodeLength) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return nativeOpcodeHandlers_.find(static_cast<std::uint8_t>(opcodeLength)) != nativeOpcodeHandlers_.end();
    }

    void setNativeHandler(uint8_t opcodeLength, NativeInstructionHandler handler) override {
        std::lock_guard<std::mutex> lock(mutex_);
        nativeOpcodeHandlers_[opcodeLength] = std::move(handler);
    }

    void clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        nativeOpcodeHandlers_.clear();
        cachedBlocks_.clear();
    }

    // Get all registered handlers
    std::vector<NativeInstructionHandler> getHandlers() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NativeInstructionHandler> handlers;
        handlers.reserve(nativeOpcodeHandlers_.size());
        for (const auto& [opcode, handler] : nativeOpcodeHandlers_) {
            handlers.push_back(handler);
        }
        return handlers;
    }

    // Try to get cached block
    std::optional<DataType> getCachedBlock(AddressType address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cachedBlocks_.find(address);
        if (it != cachedBlocks_.end()) {
            cacheHitCount_.fetch_add(1, std::memory_order_relaxed);
            return it->second;
        }
        cacheMissCount_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    // Cache a block
    void cacheBlock(AddressType address, DataType data, int opcodeLength) {
        (void)opcodeLength; // unused
        std::lock_guard<std::mutex> lock(mutex_);
        cachedBlocks_[address] = std::move(data);
        translationCount_.fetch_add(1, std::memory_order_relaxed);
    }

    // Get statistics
    struct TranslationStats {
        std::uint64_t translations = 0;
        std::uint64_t cacheHits = 0;
        std::uint64_t cacheMisses = 0;
    };

    TranslationStats getStats() const {
        return TranslationStats{
            .translations = translationCount_.load(),
            .cacheHits = cacheHitCount_.load(),
            .cacheMisses = cacheMissCount_.load()
        };
    }

    // Invalidate a specific block
    void invalidateBlock(AddressType address) {
        std::lock_guard<std::mutex> lock(mutex_);
        cachedBlocks_.erase(address);
    }

    // Invalidate blocks in a range
    void invalidateRange(AddressType start, AddressType end) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto first = cachedBlocks_.lower_bound(start);
        auto last = cachedBlocks_.upper_bound(end);
        while (first != last) {
            if (first->first > end) break;
            cachedBlocks_.erase(first++);
        }
    }

    // Invalidate all blocks
    void invalidateAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        cachedBlocks_.clear();
    }

    // Increment generation (for cache coherency)
    void incrementGeneration() {
        generation_.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t generation() const {
        return generation_.load();
    }
};

} // namespace BMMQ

#endif // BLOCKTRANSLATOR_HPP
