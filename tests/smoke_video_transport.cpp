#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>

#include "machine/VideoService.hpp"
#include "machine/plugins/video/adapters/HeadlessFrameDumper.hpp"

int main()
{
    BMMQ::VideoService service(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 8,
        .mailboxDepthFrames = 1,
    });
    auto headless = std::make_unique<BMMQ::HeadlessFrameDumper>();
    auto* observer = headless.get();
    assert(service.attachPresenter(std::move(headless)));
    assert(service.configurePresenter({
        .windowTitle = "headless transport",
        .scale = 1,
        .frameWidth = 8,
        .frameHeight = 8,
        .mode = BMMQ::VideoPresenterMode::Software,
    }));
    assert(service.resume());
    assert(service.submitFrame(BMMQ::makeBlankVideoFrame(8, 8, 11u)));
    assert(service.presentOneFrame());
    assert(observer->presentedFrames().size() == 1u);
    assert(observer->presentedFrames().front().generation == 11u);
    const auto diagnostics = service.diagnostics();
    assert(diagnostics.mailboxDepth == 0u);
    assert(diagnostics.publishedFrameCount == 1u);
    assert(diagnostics.presentFromFreshFrameCount == 1u);
    assert(diagnostics.lastPresentedGeneration == 11u);
    assert(service.pause());
    assert(service.detachPresenter());
    assert(service.state() == BMMQ::VideoLifecycleState::Headless);
}
