#ifndef BMMQ_REALTIME_VIDEO_MAILBOX_HPP
#define BMMQ_REALTIME_VIDEO_MAILBOX_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "../IoPlugin.hpp"

namespace BMMQ {

static_assert(std::atomic<std::uint8_t>::is_always_lock_free,
              "Realtime video mailbox publication requires lock-free byte atomics");
static_assert(std::atomic<std::size_t>::is_always_lock_free,
              "Realtime video mailbox diagnostics require lock-free size atomics");

struct PublishedRealtimeVideoPacket {
    RealtimeVideoPacket packet{};
    RealtimeVideoDiagnostics diagnostics{};
    std::uint64_t lifecycleEpoch = 1u;
    std::uint64_t publishedAtNs = 0u;
};

struct RealtimeVideoMailboxStats {
    std::size_t publishedFrameCount = 0u;
    std::size_t consumedFrameCount = 0u;
    std::size_t overwriteCount = 0u;
    std::size_t mailboxDepth = 0u;
    std::size_t mailboxHighWaterMark = 0u;
    std::size_t publishedPixelBytes = 0u;
    std::uint64_t lastPublishedGeneration = 0u;
    std::uint64_t lastConsumedGeneration = 0u;
    std::uint64_t frameAgeLastNs = 0u;
    std::uint64_t frameAgeHighWaterNs = 0u;
    std::size_t frameAgeUnder50usCount = 0u;
    std::size_t frameAge50To100usCount = 0u;
    std::size_t frameAge100To250usCount = 0u;
    std::size_t frameAge250To500usCount = 0u;
    std::size_t frameAge500usTo1msCount = 0u;
    std::size_t frameAge1To2msCount = 0u;
    std::size_t frameAge2To5msCount = 0u;
    std::size_t frameAge5To10msCount = 0u;
    std::size_t frameAgeOver10msCount = 0u;
};

// A three-slot latest-only SPSC mailbox. The emulation lane is the sole
// producer and the UI/render lane is the sole consumer. Neither operation
// allocates or takes a lock; packet vector ownership moves between private
// slots across the single atomic exchange.
class RealtimeVideoMailbox final {
public:
    RealtimeVideoMailbox() noexcept
    {
        resetQuiescent(true);
    }

    RealtimeVideoMailbox(const RealtimeVideoMailbox&) = delete;
    RealtimeVideoMailbox& operator=(const RealtimeVideoMailbox&) = delete;

    [[nodiscard]] bool publish(RealtimeVideoSubmission submission,
                               std::uint64_t lifecycleEpoch) noexcept
    {
        auto& packet = submission.packet;
        if (packet.empty()) {
            return false;
        }

        const auto payloadBytes = packet.payloadBytes();
        const auto generation = packet.generation;
        const auto publishedAtNs = steadyClockNs();
        submission.diagnostics.width = packet.width;
        submission.diagnostics.height = packet.height;
        submission.diagnostics.displayEnabled = packet.displayEnabled;
        submission.diagnostics.inVBlank = packet.inVBlank;
        submission.diagnostics.scanlineIndex = packet.scanlineIndex;
        packet.lifecycleEpoch = lifecycleEpoch;
        packet.producedAtNs = publishedAtNs;
        slots_[producerSlot_] = PublishedRealtimeVideoPacket{
            .packet = std::move(packet),
            .diagnostics = std::move(submission.diagnostics),
            .lifecycleEpoch = lifecycleEpoch,
            .publishedAtNs = publishedAtNs,
        };

        const auto toShare = static_cast<std::uint8_t>(kDirty | producerSlot_);
        const auto previous = shared_.exchange(toShare, std::memory_order_acq_rel);
        const bool overwrote = (previous & kDirty) != 0u;
        producerSlot_ = static_cast<std::uint8_t>(previous & kSlotMask);

        publishedFrameCount_.fetch_add(1u, std::memory_order_relaxed);
        publishedPixelBytes_.fetch_add(payloadBytes, std::memory_order_relaxed);
        lastPublishedGeneration_.store(generation, std::memory_order_relaxed);
        mailboxHighWaterMark_.store(1u, std::memory_order_relaxed);
        if (overwrote) {
            overwriteCount_.fetch_add(1u, std::memory_order_relaxed);
        }
        return true;
    }

    [[nodiscard]] std::optional<PublishedRealtimeVideoPacket> tryConsumeLatest() noexcept
    {
        const auto current = shared_.load(std::memory_order_acquire);
        if ((current & kDirty) == 0u) {
            return std::nullopt;
        }

        const auto previous = shared_.exchange(consumerSlot_, std::memory_order_acq_rel);
        if ((previous & kDirty) == 0u) {
            return std::nullopt;
        }

        consumerSlot_ = static_cast<std::uint8_t>(previous & kSlotMask);
        auto published = std::move(slots_[consumerSlot_]);
        consumedFrameCount_.fetch_add(1u, std::memory_order_relaxed);
        lastConsumedGeneration_.store(published.packet.generation, std::memory_order_relaxed);
        if (published.publishedAtNs != 0u) {
            const auto now = steadyClockNs();
            const auto age = now >= published.publishedAtNs ? now - published.publishedAtNs : 0u;
            frameAgeLastNs_.store(age, std::memory_order_relaxed);
            updateHighWater(frameAgeHighWaterNs_, age);
            frameAgeBuckets_[frameAgeBucket(age)].fetch_add(1u, std::memory_order_relaxed);
        }
        return published;
    }

    [[nodiscard]] bool hasPending() const noexcept
    {
        return (shared_.load(std::memory_order_acquire) & kDirty) != 0u;
    }

    [[nodiscard]] RealtimeVideoMailboxStats stats() const noexcept
    {
        return {
            .publishedFrameCount = publishedFrameCount_.load(std::memory_order_relaxed),
            .consumedFrameCount = consumedFrameCount_.load(std::memory_order_relaxed),
            .overwriteCount = overwriteCount_.load(std::memory_order_relaxed),
            .mailboxDepth = hasPending() ? 1u : 0u,
            .mailboxHighWaterMark = mailboxHighWaterMark_.load(std::memory_order_relaxed),
            .publishedPixelBytes = publishedPixelBytes_.load(std::memory_order_relaxed),
            .lastPublishedGeneration = lastPublishedGeneration_.load(std::memory_order_relaxed),
            .lastConsumedGeneration = lastConsumedGeneration_.load(std::memory_order_relaxed),
            .frameAgeLastNs = frameAgeLastNs_.load(std::memory_order_relaxed),
            .frameAgeHighWaterNs = frameAgeHighWaterNs_.load(std::memory_order_relaxed),
            .frameAgeUnder50usCount = frameAgeBuckets_[0].load(std::memory_order_relaxed),
            .frameAge50To100usCount = frameAgeBuckets_[1].load(std::memory_order_relaxed),
            .frameAge100To250usCount = frameAgeBuckets_[2].load(std::memory_order_relaxed),
            .frameAge250To500usCount = frameAgeBuckets_[3].load(std::memory_order_relaxed),
            .frameAge500usTo1msCount = frameAgeBuckets_[4].load(std::memory_order_relaxed),
            .frameAge1To2msCount = frameAgeBuckets_[5].load(std::memory_order_relaxed),
            .frameAge2To5msCount = frameAgeBuckets_[6].load(std::memory_order_relaxed),
            .frameAge5To10msCount = frameAgeBuckets_[7].load(std::memory_order_relaxed),
            .frameAgeOver10msCount = frameAgeBuckets_[8].load(std::memory_order_relaxed),
        };
    }

    // Lifecycle callers must quiesce producer and consumer before reset. This is
    // intentionally a control-plane operation rather than a concurrent clear.
    void resetQuiescent(bool resetStats) noexcept
    {
        shared_.store(kInitialSharedSlot, std::memory_order_relaxed);
        producerSlot_ = 0u;
        consumerSlot_ = 1u;
        for (auto& slot : slots_) {
            slot = {};
        }
        if (!resetStats) {
            return;
        }
        publishedFrameCount_.store(0u, std::memory_order_relaxed);
        consumedFrameCount_.store(0u, std::memory_order_relaxed);
        overwriteCount_.store(0u, std::memory_order_relaxed);
        mailboxHighWaterMark_.store(0u, std::memory_order_relaxed);
        publishedPixelBytes_.store(0u, std::memory_order_relaxed);
        lastPublishedGeneration_.store(0u, std::memory_order_relaxed);
        lastConsumedGeneration_.store(0u, std::memory_order_relaxed);
        frameAgeLastNs_.store(0u, std::memory_order_relaxed);
        frameAgeHighWaterNs_.store(0u, std::memory_order_relaxed);
        for (auto& bucket : frameAgeBuckets_) {
            bucket.store(0u, std::memory_order_relaxed);
        }
    }

private:
    static constexpr std::uint8_t kDirty = 0x80u;
    static constexpr std::uint8_t kSlotMask = 0x03u;
    static constexpr std::uint8_t kInitialSharedSlot = 2u;

    [[nodiscard]] static std::uint64_t steadyClockNs() noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    [[nodiscard]] static std::size_t frameAgeBucket(std::uint64_t age) noexcept
    {
        if (age < 50'000u) return 0u;
        if (age < 100'000u) return 1u;
        if (age < 250'000u) return 2u;
        if (age < 500'000u) return 3u;
        if (age < 1'000'000u) return 4u;
        if (age < 2'000'000u) return 5u;
        if (age < 5'000'000u) return 6u;
        if (age < 10'000'000u) return 7u;
        return 8u;
    }

    static void updateHighWater(std::atomic<std::uint64_t>& highWater,
                                std::uint64_t value) noexcept
    {
        auto previous = highWater.load(std::memory_order_relaxed);
        while (value > previous &&
               !highWater.compare_exchange_weak(previous, value,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
        }
    }

    std::array<PublishedRealtimeVideoPacket, 3u> slots_{};
    std::atomic<std::uint8_t> shared_{kInitialSharedSlot};
    std::uint8_t producerSlot_ = 0u;
    std::uint8_t consumerSlot_ = 1u;

    std::atomic<std::size_t> publishedFrameCount_{0u};
    std::atomic<std::size_t> consumedFrameCount_{0u};
    std::atomic<std::size_t> overwriteCount_{0u};
    std::atomic<std::size_t> mailboxHighWaterMark_{0u};
    std::atomic<std::size_t> publishedPixelBytes_{0u};
    std::atomic<std::uint64_t> lastPublishedGeneration_{0u};
    std::atomic<std::uint64_t> lastConsumedGeneration_{0u};
    std::atomic<std::uint64_t> frameAgeLastNs_{0u};
    std::atomic<std::uint64_t> frameAgeHighWaterNs_{0u};
    std::array<std::atomic<std::size_t>, 9u> frameAgeBuckets_{};
};

} // namespace BMMQ

#endif // BMMQ_REALTIME_VIDEO_MAILBOX_HPP
