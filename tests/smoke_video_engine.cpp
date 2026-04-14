#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <vector>

#include "machine/plugins/video/VideoEngine.hpp"

namespace {

BMMQ::VideoStateView makeDebugVideoState()
{
    BMMQ::VideoStateView state;
    state.vram.resize(0x2000u, 0);
    state.oam.resize(0x00A0u, 0);
    state.lcdc = 0x93u;
    state.bgp = 0xE4u;
    state.obp0 = 0xE4u;
    state.obp1 = 0xE4u;

    state.vram[0x0000] = 0xFFu;
    state.vram[0x0001] = 0x00u;
    for (std::size_t row = 1; row < 8; ++row) {
        state.vram[row * 2u] = 0xFFu;
        state.vram[row * 2u + 1u] = 0x00u;
    }
    state.vram[0x1800] = 0x00u;

    state.vram[0x0010] = 0xFFu;
    state.vram[0x0011] = 0xFFu;
    for (std::size_t row = 1; row < 8; ++row) {
        state.vram[0x0010u + row * 2u] = 0xFFu;
        state.vram[0x0011u + row * 2u + 1u] = 0xFFu;
    }

    state.oam[0] = 32u;
    state.oam[1] = 16u;
    state.oam[2] = 0x01u;
    state.oam[3] = 0x00u;
    return state;
}

} // namespace

int main()
{
    BMMQ::VideoEngine engine({
        .frameWidth = 32,
        .frameHeight = 24,
        .queueCapacityFrames = 2,
    });

    const auto state = makeDebugVideoState();
    auto frame = engine.buildDebugFrame(state, 7u);
    assert(frame.width == 32);
    assert(frame.height == 24);
    assert(frame.pixelCount() == 32u * 24u);
    assert(frame.generation == 7u);
    assert(frame.format == BMMQ::VideoFrameFormat::Argb8888);

    const auto shade1 = 0xFF88C070u;
    const auto shade3 = 0xFF081820u;
    assert(frame.pixels[0] == shade1);
    assert(frame.pixels[1] == shade1);
    assert(frame.pixels[7] == shade1);
    assert(frame.pixels[8] == shade1);
    assert(frame.pixels[16 * 32 + 8] == shade3);
    assert(frame.pixels[16 * 32 + 15] == shade3);

    assert(engine.submitFrame(frame));
    assert(engine.submitFrame(frame));
    assert(!engine.submitFrame(frame));
    assert(engine.stats().droppedFrameCount == 1u);
    assert(engine.stats().frameQueueHighWaterMark == 2u);

    auto consumed = engine.tryConsumeFrame();
    assert(consumed.has_value());
    assert(consumed->generation == 7u);
    assert(engine.tryConsumeFrame().has_value());
    assert(!engine.tryConsumeFrame().has_value());

    engine.advanceGeneration();
    assert(engine.currentGeneration() == 1u);
    assert(engine.lastValidFrame().has_value());
    assert(engine.fallbackFrame().generation == 7u);

    BMMQ::VideoEngine blankEngine({
        .frameWidth = 4,
        .frameHeight = 3,
        .queueCapacityFrames = 1,
    });
    const auto blank = blankEngine.fallbackFrame();
    assert(blank.width == 4);
    assert(blank.height == 3);
    assert(blank.pixelCount() == 12u);
    assert(blank.pixels[0] == 0xFF000000u);

    return 0;
}
