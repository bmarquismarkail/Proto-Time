#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <optional>

#include "machine/DebugSnapshotService.hpp"
#include "machine/DebugSnapshotTypes.hpp"
#include "machine/VideoDebugModel.hpp"
#include "machine/plugins/IoPlugin.hpp"

namespace {

BMMQ::VideoDebugFrameModel makeVideoModel(int width = 160, int height = 144)
{
    BMMQ::VideoDebugFrameModel model;
    model.width = width;
    model.height = height;
    model.displayEnabled = true;
    model.argbPixels.assign(static_cast<std::size_t>(width * height), 0xFF000000u);
    return model;
}

BMMQ::AudioStateView makeAudioState(uint64_t frame = 1)
{
    BMMQ::AudioStateView state;
    state.frameCounter = frame;
    state.sampleRate = 48000;
    state.channelCount = 2;
    return state;
}

} // namespace

int main()
{
    // -----------------------------------------------------------------------
    // Test 1: Default construction — queues start empty, stats all zero
    // -----------------------------------------------------------------------
    {
        BMMQ::DebugSnapshotService svc;
        const auto s = svc.stats();
        assert(s.videoSubmissions == 0u);
        assert(s.videoConsumptions == 0u);
        assert(s.videoOverflows == 0u);
        assert(s.audioSubmissions == 0u);
        assert(s.audioConsumptions == 0u);
        assert(s.audioOverflows == 0u);

        // tryConsume on empty queue returns nullopt
        assert(!svc.tryConsumeVideo().has_value());
        assert(!svc.tryConsumeAudio().has_value());
    }

    // -----------------------------------------------------------------------
    // Test 2: Submit and consume video model — stats update, value preserved
    // -----------------------------------------------------------------------
    {
        BMMQ::DebugSnapshotService svc;

        auto vid = makeVideoModel();
        const bool ok = svc.submitVideoModel(vid);
        assert(ok);

        {
            const auto s = svc.stats();
            assert(s.videoSubmissions == 1u);
            assert(s.videoConsumptions == 0u);
            assert(s.videoOverflows == 0u);
        }

        auto consumed = svc.tryConsumeVideo();
        assert(consumed.has_value());
        assert(consumed->width == 160);
        assert(consumed->height == 144);

        {
            const auto s = svc.stats();
            assert(s.videoSubmissions == 1u);
            assert(s.videoConsumptions == 1u);
        }

        // Queue empty again
        assert(!svc.tryConsumeVideo().has_value());
    }

    // -----------------------------------------------------------------------
    // Test 3: Submit and consume audio state — stats update, value preserved
    // -----------------------------------------------------------------------
    {
        BMMQ::DebugSnapshotService svc;

        auto aud = makeAudioState(42u);
        const bool ok = svc.submitAudioState(aud);
        assert(ok);

        auto consumed = svc.tryConsumeAudio();
        assert(consumed.has_value());
        assert(consumed->frameCounter == 42u);

        const auto s = svc.stats();
        assert(s.audioSubmissions == 1u);
        assert(s.audioConsumptions == 1u);
        assert(s.audioOverflows == 0u);

        assert(!svc.tryConsumeAudio().has_value());
    }

    // -----------------------------------------------------------------------
    // Test 4: Bounded capacity — overflow when queue is full
    // -----------------------------------------------------------------------
    {
        BMMQ::DebugSnapshotService svc(/*videoCapacity=*/2u, /*audioCapacity=*/2u);

        assert(svc.submitVideoModel(makeVideoModel()));
        assert(svc.submitVideoModel(makeVideoModel()));
        // Third submit exceeds capacity=2 → overflow
        assert(!svc.submitVideoModel(makeVideoModel()));

        {
            const auto s = svc.stats();
            assert(s.videoSubmissions == 3u);
            assert(s.videoOverflows == 1u);
        }

        // Drain both queued items
        assert(svc.tryConsumeVideo().has_value());
        assert(svc.tryConsumeVideo().has_value());
        assert(!svc.tryConsumeVideo().has_value());

        const auto s = svc.stats();
        assert(s.videoConsumptions == 2u);
    }

    // -----------------------------------------------------------------------
    // Test 5: Capacity=1 enforced even if constructed with 0
    // -----------------------------------------------------------------------
    {
        BMMQ::DebugSnapshotService svc(0u, 0u);  // clamped to 1
        assert(svc.submitVideoModel(makeVideoModel()));
        // Second submit overflows
        assert(!svc.submitVideoModel(makeVideoModel()));
        assert(svc.tryConsumeVideo().has_value());
        assert(!svc.tryConsumeVideo().has_value());
    }

    // -----------------------------------------------------------------------
    // Test 6: Submit nullopt video/audio — no-op, queue stays empty
    // -----------------------------------------------------------------------
    {
        BMMQ::DebugSnapshotService svc;
        // submitVideoModel(nullopt) is a no-op: returns true but doesn't enqueue
        assert(svc.submitVideoModel(std::nullopt));
        assert(svc.submitAudioState(std::nullopt));

        assert(!svc.tryConsumeVideo().has_value());   // queue still empty
        assert(!svc.tryConsumeAudio().has_value());   // queue still empty

        // Stats: no submission counted for nullopt
        const auto s = svc.stats();
        assert(s.videoSubmissions == 0u);
        assert(s.audioSubmissions == 0u);
    }

    // -----------------------------------------------------------------------
    // Test 7: FIFO ordering — earliest submit is consumed first
    // -----------------------------------------------------------------------
    {
        BMMQ::DebugSnapshotService svc(/*videoCapacity=*/4u);
        svc.submitVideoModel(makeVideoModel(10, 10));
        svc.submitVideoModel(makeVideoModel(20, 20));
        svc.submitVideoModel(makeVideoModel(30, 30));

        auto a = svc.tryConsumeVideo();
        auto b = svc.tryConsumeVideo();
        auto c = svc.tryConsumeVideo();

        assert(a.has_value() && a->width == 10);
        assert(b.has_value() && b->width == 20);
        assert(c.has_value() && c->width == 30);
        assert(!svc.tryConsumeVideo().has_value());
    }

    return 0;
}
