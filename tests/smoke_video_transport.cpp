#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdlib>

#include "machine/VideoService.hpp"
#include "machine/plugins/video/adapters/HeadlessFrameDumper.hpp"
#include "machine/plugins/video/adapters/SdlVideoPresenter.hpp"

int main()
{
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif

    BMMQ::VideoService service(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 8,
        .mailboxDepthFrames = 2,
    });

    auto headless = std::make_unique<BMMQ::HeadlessFrameDumper>();
    auto* headlessPtr = headless.get();
    assert(service.attachPresenter(std::move(headless)));
    assert(service.state() == BMMQ::VideoLifecycleState::Paused);
    assert(service.resume());
    assert(service.submitFrame(BMMQ::makeBlankVideoFrame(8, 8, 11u)));
    assert(service.presentOneFrame());
    assert(headlessPtr->presentedFrames().size() == 1u);
    assert(headlessPtr->presentedFrames()[0].generation == 11u);
    assert(service.pause());
    assert(service.detachPresenter());
    assert(service.state() == BMMQ::VideoLifecycleState::Headless);

    auto sdl = std::make_unique<BMMQ::SdlVideoPresenter>();
    assert(service.attachPresenter(std::move(sdl)));
    const bool opened = service.resume();
    if (opened) {
        assert(service.submitFrame(BMMQ::makeBlankVideoFrame(8, 8, 12u)));
        (void)service.presentOneFrame();
        assert(!service.diagnostics().activeBackendName.empty());
        assert(service.pause());
    } else {
        assert(service.state() == BMMQ::VideoLifecycleState::Faulted);
        assert(!service.diagnostics().lastBackendError.empty());
    }
    assert(service.detachPresenter());

    return 0;
}
