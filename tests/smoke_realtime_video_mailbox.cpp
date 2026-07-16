#ifdef NDEBUG
#undef NDEBUG
#endif

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "machine/plugins/video/RealtimeVideoSurface.hpp"
#include "machine/plugins/video/RealtimeVideoMailbox.hpp"

namespace {

BMMQ::RealtimeVideoSubmission makePacket(std::uint64_t generation)
{
    BMMQ::RealtimeVideoPacket packet;
    packet.width = 8;
    packet.height = 8;
    packet.displayEnabled = true;
    packet.generation = generation;
    packet.surface = BMMQ::makeArgbVideoSurface(
        std::vector<std::uint32_t>(64u, 0xFF000000u | static_cast<std::uint32_t>(generation)),
        packet.width, packet.height);
    return {.packet = std::move(packet)};
}

} // namespace

int main()
{
    BMMQ::RealtimeVideoMailbox mailbox;
    assert(!mailbox.hasPending());
    assert(mailbox.publish(makePacket(1u), 7u));
    assert(mailbox.hasPending());
    auto first = mailbox.tryConsumeLatest();
    assert(first.has_value());
    assert(first->packet.generation == 1u);
    assert(first->lifecycleEpoch == 7u);
    assert(first->packet.lifecycleEpoch == 7u);
    assert(first->packet.producedAtNs != 0u);
    assert(first->diagnostics.width == 8);
    assert(first->diagnostics.height == 8);
    assert(first->diagnostics.displayEnabled);
    assert(!mailbox.hasPending());

    constexpr std::uint64_t kPublications = 10'000u;
    std::atomic<bool> producerDone{false};
    std::uint64_t lastConsumed = 0u;
    std::thread producer([&]() {
        for (std::uint64_t generation = 2u; generation <= kPublications; ++generation) {
            assert(mailbox.publish(makePacket(generation), 8u));
        }
        producerDone.store(true, std::memory_order_release);
    });
    std::thread consumer([&]() {
        while (!producerDone.load(std::memory_order_acquire) || mailbox.hasPending()) {
            if (auto packet = mailbox.tryConsumeLatest()) {
                assert(packet->lifecycleEpoch == 8u);
                assert(packet->packet.generation > lastConsumed);
                lastConsumed = packet->packet.generation;
            } else {
                std::this_thread::yield();
            }
        }
    });
    producer.join();
    consumer.join();

    const auto stats = mailbox.stats();
    assert(lastConsumed == kPublications);
    assert(stats.lastPublishedGeneration == kPublications);
    assert(stats.lastConsumedGeneration == kPublications);
    assert(stats.publishedFrameCount == kPublications);
    assert(stats.consumedFrameCount + stats.overwriteCount == stats.publishedFrameCount);
    assert(stats.mailboxHighWaterMark == 1u);
    assert(stats.mailboxDepth == 0u);
    assert(stats.frameAgeHighWaterNs >= stats.frameAgeLastNs);
    return 0;
}
