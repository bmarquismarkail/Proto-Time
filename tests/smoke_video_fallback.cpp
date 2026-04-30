#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <memory>
#include <string_view>

#include "machine/VideoService.hpp"

namespace {

class FailingPresenter final : public BMMQ::IVideoPresenterPlugin {
public:
    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "failing";
    }

    [[nodiscard]] BMMQ::VideoPluginCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = true,
            .frameSizePreserving = true,
            .deterministic = true,
            .headlessSafe = false,
        };
    }

    bool open(const BMMQ::VideoPresenterConfig&) override
    {
        opened = true;
        return true;
    }

    void close() noexcept override
    {
        opened = false;
    }

    [[nodiscard]] bool ready() const noexcept override
    {
        return opened;
    }

    bool present(const BMMQ::VideoFramePacket& frame) noexcept override
    {
        lastAttemptGeneration = frame.generation;
        ++presentAttempts;
        return false;
    }

    [[nodiscard]] std::string_view lastError() const noexcept override
    {
        return "forced failure";
    }

    bool opened = false;
    std::size_t presentAttempts = 0;
    std::uint64_t lastAttemptGeneration = 0;
};

class IncompatibleHotSwapProcessor final : public BMMQ::IVideoFrameProcessorPlugin {
public:
    [[nodiscard]] BMMQ::VideoPluginCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = true,
            .frameSizePreserving = true,
            .deterministic = true,
            .headlessSafe = true,
            .hotSwappable = true,
        };
    }

    bool process(const BMMQ::VideoFramePacket& input,
                 BMMQ::VideoFramePacket& output) noexcept override
    {
        output = input;
        ++output.width;
        return true;
    }
};

} // namespace

int main()
{
    BMMQ::VideoService service(BMMQ::VideoEngineConfig{
        .frameWidth = 4,
        .frameHeight = 4,
        .mailboxDepthFrames = 1,
    });
    auto presenter = std::make_unique<FailingPresenter>();
    auto* presenterPtr = presenter.get();
    assert(service.attachPresenter(std::move(presenter)));
    assert(service.resume());

    assert(service.addProcessor(std::make_unique<IncompatibleHotSwapProcessor>()));
    assert(service.submitFrame(BMMQ::makeBlankVideoFrame(4, 4, 9u)));
    assert(!service.presentOneFrame());
    assert(presenterPtr->presentAttempts >= 1u);
    assert(presenterPtr->lastAttemptGeneration == 9u);
    assert(service.diagnostics().presentFailureCount == 1u);
    assert(service.diagnostics().compatibilityFallbackCount == 1u);
    assert(service.diagnostics().presentFallbackCount == 0u);
    assert(service.diagnostics().lastPresentedGeneration == 9u);

    assert(service.submitFrame(BMMQ::makeBlankVideoFrame(4, 4, 10u)));
    assert(service.submitFrame(BMMQ::makeBlankVideoFrame(4, 4, 11u)));
    assert(service.diagnostics().overwriteCount == 1u);
    assert(!service.presentOneFrame());
    assert(presenterPtr->lastAttemptGeneration == 11u);
    assert(service.diagnostics().staleFrameDropCount == 0u);

    BMMQ::VideoService blankFallback(BMMQ::VideoEngineConfig{
        .frameWidth = 2,
        .frameHeight = 2,
        .mailboxDepthFrames = 1,
    });
    auto blankPresenter = std::make_unique<FailingPresenter>();
    auto* blankPresenterPtr = blankPresenter.get();
    assert(blankFallback.attachPresenter(std::move(blankPresenter)));
    assert(blankFallback.resume());
    assert(!blankFallback.presentOneFrame());
    assert(blankPresenterPtr->lastAttemptGeneration == 0u);
    assert(blankFallback.diagnostics().presentFailureCount == 1u);
    assert(blankFallback.diagnostics().presentFallbackCount == 1u);

    return 0;
}
