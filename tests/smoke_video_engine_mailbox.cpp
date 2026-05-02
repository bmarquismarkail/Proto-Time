#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>

#include "machine/plugins/video/VideoEngine.hpp"

// Smoke test for the triple-buffer SPSC mailbox inside VideoEngine.
// All operations are single-threaded (SPSC correctness is a structural property
// of the 3-slot triple-buffer; no threads are needed to verify the invariants).

namespace {

BMMQ::VideoPresentPacket makePacket(std::uint64_t generation, BMMQ::VideoFrameSource source)
{
    BMMQ::VideoFramePacket raw;
    raw.width  = 4;
    raw.height = 2;
    raw.generation = generation;
    raw.source = source;
    raw.pixels.assign(8u, 0xFF000000u);

    BMMQ::VideoPresentPacket pkt;
    pkt.pixels     = raw.pixels;
    pkt.width      = raw.width;
    pkt.height     = raw.height;
    pkt.generation = raw.generation;
    pkt.source     = raw.source;
    return pkt;
}

} // namespace

int main()
{
    // -------------------------------------------------------------------------
    // 1. Empty mailbox returns nullopt.
    // -------------------------------------------------------------------------
    BMMQ::VideoEngine engine({.frameWidth = 4, .frameHeight = 2, .mailboxDepthFrames = 2});
    assert(!engine.tryConsumeLatestFrame().has_value());
    assert(engine.mailboxFrameCount() == 0u);

    // -------------------------------------------------------------------------
    // 2. Submit one frame; consume it; mailbox is empty again.
    // -------------------------------------------------------------------------
    const auto r1 = engine.submitPresentPacket(makePacket(1u, BMMQ::VideoFrameSource::MachineSnapshot));
    assert(r1.accepted);
    assert(!r1.overwroteOldFrame);
    assert(engine.mailboxFrameCount() == 1u);

    const auto c1 = engine.tryConsumeLatestFrame();
    assert(c1.has_value());
    assert(c1->generation == 1u);
    assert(engine.mailboxFrameCount() == 0u);
    assert(!engine.tryConsumeLatestFrame().has_value());

    // -------------------------------------------------------------------------
    // 3. Submit three frames without consuming: only the latest survives.
    // -------------------------------------------------------------------------
    const auto rA = engine.submitPresentPacket(makePacket(10u, BMMQ::VideoFrameSource::MachineSnapshot));
    assert(rA.accepted);
    assert(!rA.overwroteOldFrame);  // nothing pending before A

    const auto rB = engine.submitPresentPacket(makePacket(11u, BMMQ::VideoFrameSource::MachineSnapshot));
    assert(rB.accepted);
    assert(rB.overwroteOldFrame);   // A was still pending
    assert(engine.stats().overwriteCount == 1u);
    assert(engine.stats().overwriteDebugFrameCount == 1u);

    const auto rC = engine.submitPresentPacket(makePacket(12u, BMMQ::VideoFrameSource::MachineSnapshot));
    assert(rC.accepted);
    assert(rC.overwroteOldFrame);   // B was still pending
    assert(engine.stats().overwriteCount == 2u);
    assert(engine.stats().overwriteDebugFrameCount == 2u);
    assert(engine.stats().overwriteRealtimeFrameCount == 0u);

    // mailboxFrameCount is always 0 or 1 with the triple buffer.
    assert(engine.mailboxFrameCount() == 1u);

    const auto latest = engine.tryConsumeLatestFrame();
    assert(latest.has_value());
    assert(latest->generation == 12u);   // only the last submitted frame
    assert(engine.stats().consumedFrameCount == 2u);  // 1 from step 2, 1 here
    assert(engine.stats().staleFrameDropCount == 0u); // triple-buffer never drains stale on consume

    assert(!engine.tryConsumeLatestFrame().has_value());
    assert(engine.mailboxFrameCount() == 0u);

    // -------------------------------------------------------------------------
    // 4. Mixed realtime/debug overwrite tracking.
    // -------------------------------------------------------------------------
    [[maybe_unused]] const auto rRt = engine.submitPresentPacket(makePacket(20u, BMMQ::VideoFrameSource::RealtimeSnapshot));
    const auto rtOverwrite = engine.submitPresentPacket(makePacket(21u, BMMQ::VideoFrameSource::MachineSnapshot));
    assert(rtOverwrite.overwroteOldFrame);
    assert(engine.stats().overwriteRealtimeFrameCount == 1u);  // realtime frame was overwritten

    const auto c20 = engine.tryConsumeLatestFrame();
    assert(c20.has_value());
    assert(c20->generation == 21u);

    // -------------------------------------------------------------------------
    // 5. advanceGeneration clears the queue.
    // -------------------------------------------------------------------------
    [[maybe_unused]] const auto rAdv = engine.submitPresentPacket(makePacket(30u, BMMQ::VideoFrameSource::MachineSnapshot));
    assert(engine.mailboxFrameCount() == 1u);
    const auto newGen = engine.advanceGeneration();  // clears the pending frame
    assert(newGen == 1u);
    assert(engine.mailboxFrameCount() == 0u);
    assert(!engine.tryConsumeLatestFrame().has_value());

    // -------------------------------------------------------------------------
    // 6. fallbackFrame returns last valid frame after drain.
    // -------------------------------------------------------------------------
    [[maybe_unused]] const auto rFb = engine.submitPresentPacket(makePacket(40u, BMMQ::VideoFrameSource::MachineSnapshot));
    [[maybe_unused]] const auto consumed40 = engine.tryConsumeLatestFrame();
    const auto fb = engine.fallbackFrame();
    assert(fb.generation == 40u);
    assert(fb.source == BMMQ::VideoFrameSource::LastValidFallback);

    return 0;
}
