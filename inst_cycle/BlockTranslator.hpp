#ifndef BLOCKTRANSLATOR_HPP
#define BLOCKTRANSLATOR_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "IntermediateRepresentation.hpp"

namespace BMMQ {

class BlockBackendArtifact {
public:
    virtual ~BlockBackendArtifact() = default;
};

using BlockBackendArtifactPtr = std::shared_ptr<const BlockBackendArtifact>;

enum class TranslatedBlockExitReason : std::uint8_t {
    SequentialLimit,
    ControlFlow,
    InterruptSensitive,
    PageBoundary,
    Unsupported,
};

template <typename AddressType = std::uint16_t, typename DataType = std::uint8_t>
struct TranslatedInstruction {
    AddressType address = 0;
    std::array<DataType, 3> bytes{};
    std::uint8_t length = 0;
};

template <typename AddressType = std::uint16_t, typename DataType = std::uint8_t>
struct TranslatedBlockEntry {
    AddressType start = 0;
    AddressType end = 0;
    std::uint64_t mappingGeneration = 0;
    std::vector<TranslatedInstruction<AddressType, DataType>> instructions;
    // Optional validated, architecture-neutral lowering. The Phase 10 byte
    // sequence remains the portable fallback and invalidation authority.
    IR::BlockPtr intermediateRepresentation{};
    // Optional immutable host-backend artifact derived from the validated IR.
    // Its lifetime is tied to the cache entry and it is discarded by the same
    // invalidation/retirement rules as the portable representation.
    BlockBackendArtifactPtr backendArtifact{};
    // Diagnostic context retained when lowering or backend preparation falls
    // back to the Phase 10 byte sequence.
    std::string irFallbackReason{};
    TranslatedBlockExitReason exitReason = TranslatedBlockExitReason::SequentialLimit;
    bool valid = true;
};

struct ThreadedBlockCacheStats {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t translations = 0;
    std::uint64_t translatedInstructions = 0;
    std::uint64_t invalidations = 0;
    std::uint64_t guardFailures = 0;
    std::uint64_t chainContinuations = 0;
    std::uint64_t unsupportedFallbacks = 0;
    std::uint64_t fastEligibilityStops = 0;
    std::uint64_t invalidationRequests = 0;
    std::uint64_t invalidationPageSkips = 0;
    std::uint64_t invalidationScans = 0;
    std::uint64_t invalidationBlocksExamined = 0;
    std::uint64_t irTranslations = 0;
    std::uint64_t irExecutions = 0;
    std::uint64_t irGuardFailures = 0;
    std::uint64_t irFallbacks = 0;
    std::uint64_t irLoweredInstructions = 0;
    std::uint64_t irIneligibleTranslations = 0;
    std::uint64_t irLoweringNanos = 0;
    std::uint64_t irGuardChecks = 0;
    std::uint64_t irGuardCheckNanos = 0;
    std::uint64_t irExecutionNanos = 0;
    std::uint64_t irBlockEntries = 0;
    std::uint64_t irBlockContinuations = 0;
    std::uint64_t irBlockContinuationRejects = 0;
    std::array<std::uint64_t, 5> exits{};
    std::array<std::uint64_t, 256> unsupportedFallbackOpcodes{};
    std::array<std::uint64_t, 256> fastEligibilityStopOpcodes{};
};

// Emulation-thread-owned cache. Direct PC slots make a sequential successor a
// constant-time threaded dispatch without sharing mutable guest state.
template <typename AddressType = std::uint16_t, typename DataType = std::uint8_t>
class ThreadedBlockCache {
public:
    using Instruction = TranslatedInstruction<AddressType, DataType>;
    using Block = TranslatedBlockEntry<AddressType, DataType>;
    struct Lookup {
        const Block* block = nullptr;
        std::size_t instructionIndex = 0;
        explicit operator bool() const noexcept { return block != nullptr; }
    };

    static constexpr std::size_t kAddressCount =
        static_cast<std::size_t>(std::numeric_limits<AddressType>::max()) + 1u;
    static constexpr std::size_t kPageShift = 8u;
    static constexpr std::size_t kPageSize = std::size_t{1u} << kPageShift;
    static constexpr std::size_t kPageCount =
        (kAddressCount + kPageSize - 1u) / kPageSize;

    explicit ThreadedBlockCache(std::size_t maxBlocks = 4096u)
        : maxBlocks_(std::max<std::size_t>(maxBlocks, 1u))
    {
    }

    [[nodiscard]] Lookup lookup(AddressType address) noexcept
    {
        auto slot = slots_[static_cast<std::size_t>(address)];
        if (slot.block == nullptr || !slot.block->valid ||
            slot.block->mappingGeneration != mappingGeneration_) {
            ++stats_.misses;
            return {};
        }
        ++stats_.hits;
        if (slot.instructionIndex != 0u) ++stats_.chainContinuations;
        return {slot.block, slot.instructionIndex};
    }

    void insert(Block block)
    {
        if (block.instructions.empty()) return;
        if (blocks_.size() >= maxBlocks_) clearEntries();
        block.mappingGeneration = mappingGeneration_;
        auto stored = std::make_unique<Block>(std::move(block));
        auto* pointer = stored.get();
        for (std::size_t index = 0; index < pointer->instructions.size(); ++index) {
            const auto address = pointer->instructions[index].address;
            slots_[static_cast<std::size_t>(address)] = Slot{pointer, index};
        }
        ++stats_.translations;
        stats_.translatedInstructions += pointer->instructions.size();
        if (pointer->intermediateRepresentation) ++stats_.irTranslations;
        ++stats_.exits[static_cast<std::size_t>(pointer->exitReason)];
        adjustActivePages(*pointer, 1);
        blocks_.push_back(std::move(stored));
    }

    void invalidateRange(AddressType start, AddressType end) noexcept
    {
        ++stats_.invalidationRequests;
        const auto rangeStart = static_cast<std::uint64_t>(start);
        const auto rangeEnd = static_cast<std::uint64_t>(end);
        if (!rangeHasActivePage(rangeStart, rangeEnd)) {
            ++stats_.invalidationPageSkips;
            return;
        }
        ++stats_.invalidationScans;
        for (auto& block : blocks_) {
            if (!block->valid) continue;
            ++stats_.invalidationBlocksExamined;
            if (static_cast<std::uint64_t>(block->start) <= rangeEnd &&
                static_cast<std::uint64_t>(block->end) >= rangeStart) {
                invalidateBlock(*block);
            }
        }
    }

    void invalidate(AddressType address, bool guardFailure = false) noexcept
    {
        auto slot = slots_[static_cast<std::size_t>(address)];
        if (slot.block != nullptr && slot.block->valid) {
            invalidateBlock(*slot.block);
        }
        if (guardFailure) ++stats_.guardFailures;
    }

    void invalidateAll() noexcept
    {
        for (auto& block : blocks_) {
            if (block->valid) {
                block->valid = false;
                ++stats_.invalidations;
            }
        }
        slots_.fill(Slot{});
        activeBlocksByPage_.fill(0u);
        ++mappingGeneration_;
    }

    void noteUnsupportedFallback(DataType opcode) noexcept {
        ++stats_.unsupportedFallbacks;
        ++stats_.unsupportedFallbackOpcodes[static_cast<std::size_t>(opcode)];
    }
    void noteFastEligibilityStop(DataType opcode) noexcept {
        ++stats_.fastEligibilityStops;
        ++stats_.fastEligibilityStopOpcodes[static_cast<std::size_t>(opcode)];
    }
    void noteIrExecution(std::uint64_t elapsedNanos = 0u) noexcept {
        ++stats_.irExecutions;
        stats_.irExecutionNanos += elapsedNanos;
    }
    void noteIrGuardFailure() noexcept { ++stats_.irGuardFailures; }
    void noteIrFallback() noexcept { ++stats_.irFallbacks; }
    void noteIrLowering(std::size_t instructions, std::uint64_t elapsedNanos) noexcept {
        stats_.irLoweredInstructions += instructions;
        stats_.irLoweringNanos += elapsedNanos;
    }
    void noteIrGuardCheck(std::uint64_t elapsedNanos) noexcept {
        ++stats_.irGuardChecks;
        stats_.irGuardCheckNanos += elapsedNanos;
    }
    void noteIrIneligibleTranslation() noexcept { ++stats_.irIneligibleTranslations; }
    void noteIrBlockEntry() noexcept { ++stats_.irBlockEntries; }
    void noteIrBlockContinuation() noexcept { ++stats_.irBlockContinuations; }
    void noteIrBlockContinuationReject() noexcept { ++stats_.irBlockContinuationRejects; }
    [[nodiscard]] ThreadedBlockCacheStats stats() const noexcept { return stats_; }
    [[nodiscard]] std::uint64_t mappingGeneration() const noexcept { return mappingGeneration_; }
    [[nodiscard]] std::size_t size() const noexcept { return blocks_.size(); }

    void clear() noexcept
    {
        clearEntries();
        stats_ = {};
        ++mappingGeneration_;
    }

private:
    struct Slot {
        Block* block = nullptr;
        std::size_t instructionIndex = 0;
    };

    void invalidateBlock(Block& block) noexcept
    {
        adjustActivePages(block, -1);
        block.valid = false;
        ++stats_.invalidations;
        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            auto& slot = slots_[static_cast<std::size_t>(block.instructions[index].address)];
            if (slot.block == &block) slot = {};
        }
    }

    void clearEntries() noexcept
    {
        slots_.fill(Slot{});
        activeBlocksByPage_.fill(0u);
        blocks_.clear();
    }

    void adjustActivePages(const Block& block, int delta) noexcept
    {
        const auto first = static_cast<std::size_t>(block.start) >> kPageShift;
        const auto last = static_cast<std::size_t>(block.end) >> kPageShift;
        auto adjust = [&](std::size_t page) {
            if (delta > 0) {
                ++activeBlocksByPage_[page];
            } else if (activeBlocksByPage_[page] != 0u) {
                --activeBlocksByPage_[page];
            }
        };
        if (first <= last) {
            for (auto page = first; page <= last; ++page) adjust(page);
            return;
        }
        for (auto page = first; page < kPageCount; ++page) adjust(page);
        for (std::size_t page = 0u; page <= last; ++page) adjust(page);
    }

    [[nodiscard]] bool rangeHasActivePage(
        std::uint64_t rangeStart, std::uint64_t rangeEnd) const noexcept
    {
        const auto first = static_cast<std::size_t>(rangeStart) >> kPageShift;
        const auto last = static_cast<std::size_t>(rangeEnd) >> kPageShift;
        for (auto page = first; page <= last; ++page) {
            if (activeBlocksByPage_[page] != 0u) return true;
        }
        return false;
    }

    std::array<Slot, kAddressCount> slots_{};
    std::array<std::uint32_t, kPageCount> activeBlocksByPage_{};
    std::vector<std::unique_ptr<Block>> blocks_{};
    std::size_t maxBlocks_ = 0;
    std::uint64_t mappingGeneration_ = 0;
    ThreadedBlockCacheStats stats_{};
};

} // namespace BMMQ

#endif // BLOCKTRANSLATOR_HPP
