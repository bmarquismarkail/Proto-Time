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
    assert(service.configurePresenter({
        .windowTitle = "software",
        .scale = 1,
        .frameWidth = 8,
        .frameHeight = 8,
        .mode = BMMQ::VideoPresenterMode::Software,
        .createHiddenWindowOnOpen = true,
        .showWindowOnPresent = false,
    }));
    assert(service.resume());
    assert(service.submitFrame(BMMQ::makeBlankVideoFrame(8, 8, 12u)));
    (void)service.presentOneFrame();
    assert(service.diagnostics().configuredPresenterMode == BMMQ::VideoPresenterMode::Software);
    assert(service.diagnostics().activePresenterMode == BMMQ::VideoPresenterMode::Software);
    assert(service.diagnostics().presenterTextureUploadCount >= 1u);
    assert(service.diagnostics().presenterRenderCount >= 1u);
        assert(service.diagnostics().presenterPresentDurationSampleCount >= 1u);
        assert(service.diagnostics().presenterPresentDurationP95Nanos >=
            service.diagnostics().presenterPresentDurationP50Nanos);
        assert(service.diagnostics().presenterPresentDurationP99Nanos >=
            service.diagnostics().presenterPresentDurationP95Nanos);
        assert(service.diagnostics().presenterPresentDurationP999Nanos >=
            service.diagnostics().presenterPresentDurationP99Nanos);
        const auto softwareBucketTotal =
         service.diagnostics().presenterPresentDurationUnder50usCount +
         service.diagnostics().presenterPresentDuration50To100usCount +
         service.diagnostics().presenterPresentDuration100To250usCount +
         service.diagnostics().presenterPresentDuration250To500usCount +
         service.diagnostics().presenterPresentDuration500usTo1msCount +
         service.diagnostics().presenterPresentDuration1To2msCount +
         service.diagnostics().presenterPresentDuration2To5msCount +
         service.diagnostics().presenterPresentDuration5To10msCount +
         service.diagnostics().presenterPresentDurationOver10msCount;
        assert(softwareBucketTotal == service.diagnostics().presenterPresentDurationSampleCount);
    assert(service.pause());

    assert(service.configurePresenter({
        .windowTitle = "auto",
        .scale = 1,
        .frameWidth = 8,
        .frameHeight = 8,
        .mode = BMMQ::VideoPresenterMode::Auto,
        .createHiddenWindowOnOpen = true,
        .showWindowOnPresent = false,
    }));
    assert(service.resume());
    assert(service.submitFrame(BMMQ::makeBlankVideoFrame(8, 8, 13u)));
    (void)service.presentOneFrame();
    assert(service.diagnostics().configuredPresenterMode == BMMQ::VideoPresenterMode::Auto);
    assert(service.diagnostics().presenterPresentDurationSampleCount >= 1u);
    assert(service.pause());

    assert(service.configurePresenter({
        .windowTitle = "hardware",
        .scale = 1,
        .frameWidth = 8,
        .frameHeight = 8,
        .mode = BMMQ::VideoPresenterMode::Hardware,
        .createHiddenWindowOnOpen = true,
        .showWindowOnPresent = false,
    }));
    const bool openedHardware = service.resume();
    if (openedHardware) {
        assert(service.submitFrame(BMMQ::makeBlankVideoFrame(8, 8, 14u)));
        (void)service.presentOneFrame();
        assert(service.diagnostics().activePresenterMode == BMMQ::VideoPresenterMode::Hardware);
        assert(service.diagnostics().presenterPresentDurationSampleCount >= 1u);
        assert(service.pause());
    } else {
        assert(service.state() == BMMQ::VideoLifecycleState::Faulted);
        assert(!service.diagnostics().lastBackendError.empty());
    }
    assert(service.detachPresenter());

    return 0;
}
