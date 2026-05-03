#include "../SdlFrontendPlugin.hpp"
#include "../../AudioService.hpp"
#include "../../DebugSnapshotService.hpp"
#include "../../VideoService.hpp"
#include "../audio_output/DummyAudioOutput.hpp"
#include "../audio_output/FileAudioOutput.hpp"
#include "../video/adapters/SdlVideoPresenter.hpp"
#include "SdlAudioOutput.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <condition_variable>

    enum class ControlAction : uint8_t {
        PauseToggle = 0,
        ThrottleToggle,
        SingleStep,
        SpeedUp,
        SpeedDown,
    };

    [[nodiscard]] static constexpr std::optional<ControlAction> mapControlKey(BMMQ::SdlFrontendHostKey key) noexcept
    {
        switch (key) {
        case BMMQ::SdlFrontendHostKey::Pause:
            return ControlAction::PauseToggle;
        case BMMQ::SdlFrontendHostKey::ThrottleToggle:
            return ControlAction::ThrottleToggle;
        case BMMQ::SdlFrontendHostKey::SingleStep:
            return ControlAction::SingleStep;
        case BMMQ::SdlFrontendHostKey::SpeedUp:
            return ControlAction::SpeedUp;
        case BMMQ::SdlFrontendHostKey::SpeedDown:
            return ControlAction::SpeedDown;
        default:
            return std::nullopt;
        }
    }
#include <cstdint>
#include <cctype>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../../Machine.hpp"
#include "../../MachineLifecycleCoordinator.hpp"
#include "../LoggingPlugins.hpp"

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
#  if defined(__has_include)
#    if __has_include(<SDL2/SDL.h>)
#      include <SDL2/SDL.h>
#    else
#      include <SDL.h>
#    endif
#  else
#    include <SDL.h>
#  endif
#endif

namespace {

constexpr std::size_t kApuFrameSamples = 256u;

[[nodiscard]] BMMQ::VideoPresenterMode presenterModeForPolicy(BMMQ::VideoPresenterPolicy policy) noexcept
{
    // Policy is the authoritative selector for runtime presenter mode.
    // SoftwareOnly always forces software; hardware-preferred attempts
    // hardware and lets the presenter apply fallback policy.
    switch (policy) {
    case BMMQ::VideoPresenterPolicy::SoftwareOnly:
        return BMMQ::VideoPresenterMode::Software;
    case BMMQ::VideoPresenterPolicy::HardwarePreferredWithFallback:
        return BMMQ::VideoPresenterMode::Hardware;
    }
    return BMMQ::VideoPresenterMode::Hardware;
}

enum class VideoRenderEventBucket : uint8_t {
    VBlank = 0,
    Scanline,
    MemoryWrite,
    Other,
};

[[nodiscard]] constexpr VideoRenderEventBucket videoRenderEventBucket(BMMQ::MachineEventType type) noexcept
{
    switch (type) {
    case BMMQ::MachineEventType::VBlank:
        return VideoRenderEventBucket::VBlank;
    case BMMQ::MachineEventType::VideoScanlineReady:
        return VideoRenderEventBucket::Scanline;
    case BMMQ::MachineEventType::MemoryWriteObserved:
        return VideoRenderEventBucket::MemoryWrite;
    default:
        return VideoRenderEventBucket::Other;
    }
}

class SdlFrontendPluginImpl final : public BMMQ::ISdlFrontendPlugin,
                                    public BMMQ::LoggingPluginSupport {
public:
    explicit SdlFrontendPluginImpl(BMMQ::SdlFrontendConfig config = {})
        : config_(std::move(config))
    {
        setMaxEntryCount(256u);
    }

    ~SdlFrontendPluginImpl() override
    {
        shutdownBackend();
    }

    [[nodiscard]] const BMMQ::SdlFrontendConfig& config() const noexcept override
    {
        return config_;
    }

    [[nodiscard]] BMMQ::SdlFrontendStats stats() const noexcept override
    {
        std::scoped_lock<std::mutex> lock(sharedStateMutex_);
        stats_.renderServiceState = renderServiceState_.load(std::memory_order_acquire);
        if (lifecycleCoordinator_ != nullptr) {
            const auto transition = lifecycleCoordinator_->lastTransitionResult();
            const auto coordinatorStats = lifecycleCoordinator_->stats();
            stats_.lifecycleLastOutcome = transition.outcome;
            stats_.lifecycleLastFailureStage = transition.failureStage;
            stats_.lifecycleLastRetryCountUsed = transition.retryCountUsed;
            stats_.lifecycleLastRejectedForReentry = transition.rejectedForReentry;
            stats_.lifecycleDegradedHeadlessVideoActive = lifecycleCoordinator_->degradedHeadlessVideoActive();
            stats_.lifecycleDegradedAudioDisabledActive = lifecycleCoordinator_->degradedAudioDisabledActive();
            stats_.lifecycleTransitionCount = coordinatorStats.transitionCount;
            stats_.lifecycleTransitionSuccessCount = coordinatorStats.successCount;
            stats_.lifecycleTransitionDegradedCount = coordinatorStats.degradedCount;
            stats_.lifecycleTransitionFailureCount = coordinatorStats.failureCount;
            stats_.lifecycleTransitionReentryAttemptCount = coordinatorStats.transitionReentryAttemptCount;
            stats_.lifecycleNestedTransitionRejectCount = coordinatorStats.nestedTransitionRejectCount;
            stats_.lifecycleReasonRomLoadCount =
                coordinatorStats.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::RomLoad)];
            stats_.lifecycleReasonHardResetCount =
                coordinatorStats.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::HardReset)];
            stats_.lifecycleReasonAudioBackendRestartCount =
                coordinatorStats.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::AudioBackendRestart)];
            stats_.lifecycleReasonVideoBackendRestartCount =
                coordinatorStats.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::VideoBackendRestart)];
            stats_.lifecycleReasonConfigReconfigureCount =
                coordinatorStats.reasonCounts[static_cast<std::size_t>(BMMQ::MachineTransitionReason::ConfigReconfigure)];
            stats_.lifecycleTransitionDurationP50Ns = coordinatorStats.transitionDurationP50Ns;
            stats_.lifecycleTransitionDurationP95Ns = coordinatorStats.transitionDurationP95Ns;
            stats_.lifecycleTransitionDurationMaxNs = coordinatorStats.transitionDurationMaxNs;
        }
        // Sync input stats from atomic counters (updated on the lockless fast path).
        stats_.inputPolls = inputPollsAtomic_.load(std::memory_order_relaxed);
        stats_.inputSamplesProvided = inputSamplesProvidedAtomic_.load(std::memory_order_relaxed);
        // Sync event-pump stats from atomic counters (Phase 35B).
        // Updated by collectBackendEvents/applyCollectedEvents (render thread,
        // lock-free) and by pumpBackendEvents (headless slow path, under lock).
        stats_.eventPumpCalls = eventPumpCallsAtomic_.load(std::memory_order_relaxed);
        stats_.backendEventsTranslated = backendEventsTranslatedAtomic_.load(std::memory_order_relaxed);
        stats_.renderServiceEventPumpCount = renderServiceEventPumpCountAtomic_.load(std::memory_order_relaxed);
        // Sync Phase 36A stat: frame notifications sent outside the lock.
        stats_.onVideoEventFrameNotifyOutsideLockCount =
            onVideoEventFrameNotifyOutsideLockCountAtomic_.load(std::memory_order_relaxed);
        stats_.videoDebugModelBuildSkipCount =
            videoDebugModelBuildSkipCountAtomic_.load(std::memory_order_relaxed);
        // Sync Phase 36B render-service wait-block shadow atomics.
        // Written under renderServiceWaitMutex_ (or no lock), so must be
        // folded in here rather than written directly to stats_.
        stats_.renderServiceSleepCount =
            renderServiceSleepCountAtomic_.load(std::memory_order_relaxed);
        stats_.renderServiceDeferredPresentFastSleepCount =
            renderServiceDeferredPresentFastSleepCountAtomic_.load(std::memory_order_relaxed);
        stats_.renderServiceFrameWakeCount =
            renderServiceFrameWakeCountAtomic_.load(std::memory_order_relaxed);
        stats_.renderServiceTimeoutWakeCount =
            renderServiceTimeoutWakeCountAtomic_.load(std::memory_order_relaxed);
        stats_.renderServiceSleepOvershootCount =
            renderServiceSleepOvershootCountAtomic_.load(std::memory_order_relaxed);
        syncAudioTransportStats();
        return stats_;
    }

    [[nodiscard]] const std::vector<std::string>& diagnostics() const noexcept override
    {
        return entries();
    }

    [[nodiscard]] const std::optional<BMMQ::VideoDebugFrameModel>& lastVideoDebugModel() const noexcept override
    {
        return lastVideoDebugModel_;
    }

    [[nodiscard]] const std::optional<BMMQ::AudioStateView>& lastAudioState() const noexcept override
    {
        return lastAudioState_;
    }

    [[nodiscard]] const std::optional<BMMQ::SdlAudioPreviewBuffer>& lastAudioPreview() const noexcept override
    {
        return lastAudioPreview_;
    }

    [[nodiscard]] const std::optional<BMMQ::DigitalInputStateView>& lastInputState() const noexcept override
    {
        return lastInputState_;
    }

    [[nodiscard]] const std::optional<BMMQ::SdlFrameBuffer>& lastFrame() const noexcept override
    {
        return lastFrame_;
    }

    [[nodiscard]] std::string_view lastRenderSummary() const noexcept override
    {
        return lastRenderSummary_;
    }

    [[nodiscard]] bool windowVisible() const noexcept override
    {
        return videoPresenter_ != nullptr
            ? videoPresenter_->windowVisible()
            : windowVisible_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool windowVisibilityRequested() const noexcept override
    {
        return videoPresenter_ != nullptr
            ? videoPresenter_->windowVisibilityRequested()
            : windowVisibilityRequested_.load(std::memory_order_acquire);
    }

    void requestWindowVisibility(bool visible) override
    {
        windowVisibilityRequested_.store(visible, std::memory_order_release);
        if (videoPresenter_ != nullptr) {
            videoPresenter_->requestWindowVisibility(visible);
        }
        appendLog(std::string("sdl: window visibility requested=") + (visible ? "visible" : "hidden"));
    }

    bool serviceFrontend() override
    {
        // ── First lock block: sync state, decisions, consume frame ───────────
        // SDL_RenderPresent is intentionally excluded from this block so the
        // emulation thread is not stalled during GPU upload/present (Phase 37B).
        std::optional<BMMQ::VideoFramePacket> processedFrame;
        std::size_t handledEvents = 0;
        bool hadFrame        = false;
        bool hasAudioState   = false;
        bool hadAudioPreview = false;
        bool visibilityChanged = false;
        bool presented       = false;
        bool audioActive     = false;
        {
            std::unique_lock<std::mutex> lock(sharedStateMutex_);
            ++stats_.serviceCalls;
            stats_.renderServiceState = renderServiceState_.load(std::memory_order_acquire);
            syncTimingStats();
            applyLifecycleRecoveryPolicy();
            if (renderServiceActive()) {
                syncVideoTransportStats();
                syncAudioTransportStats();
                const bool hasVideoWork = config_.enableVideo && (frameDirty_ || lastFrame_.has_value());
                const bool hasAudioStateRs = config_.enableAudio && lastAudioState_.has_value();
                const bool hadAudioPreviewRs = config_.enableAudio && lastAudioPreview_.has_value();
                return hasVideoWork || hasAudioStateRs || hadAudioPreviewRs || quitRequested_;
            }

            if (!config_.pumpBackendEventsOnInputSample) {
                handledEvents = pumpBackendEvents();
                if (handledEvents != 0u) {
                    // Mirror renderServiceEventPumpCount via atomic so stats() fold
                    // sees a unified total from both the render-service and headless paths.
                    renderServiceEventPumpCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
                }
            }

            syncVideoTransportStats();
            // Drain any debug snapshots queued by the emulation thread.
            if (debugSnapshotService_ != nullptr) {
                // Keep only the latest video and audio captures from the queue.
                while (auto vid = debugSnapshotService_->tryConsumeVideo()) {
                    lastVideoDebugModel_ = std::move(*vid);
                }
                while (auto aud = debugSnapshotService_->tryConsumeAudio()) {
                    lastAudioState_ = std::move(*aud);
                }
            }
            hadFrame        = config_.enableVideo && lastFrame_.has_value();
            hasAudioState   = config_.enableAudio && lastAudioState_.has_value();
            hadAudioPreview = config_.enableAudio && lastAudioPreview_.has_value();
            visibilityChanged =
                windowVisible_.load(std::memory_order_acquire) !=
                windowVisibilityRequested_.load(std::memory_order_acquire);
            audioActive = hasAudioState ||
                (audioService_ != nullptr && audioService_->engine().bufferedSamples() != 0u);

            if (hadFrame && (frameDirty_ || visibilityChanged)) {
                ++stats_.renderServicePresentAttempts;
                if (frameDirty_ && shouldDeferVideoFrameForAudioLowWater()) {
                    videoPresentDeferredForAudioLowWater_ = true;
                } else if (videoService_ != nullptr &&
                           backendReady_ &&
                           videoService_->state() == BMMQ::VideoLifecycleState::Active) {
                    if (config_.showWindowOnPresent) {
                        requestWindowVisibility(true);
                    }
                    ++stats_.renderAttempts;
                    processedFrame = videoService_->consumeAndProcessFrame();
                    if (!processedFrame.has_value()) {
                        // Headless: consumeAndProcessFrame set state; treat as success.
                        presented = true;
                        ++stats_.renderServicePresentSuccessCount;
                        ++stats_.framesPresented;
                        frameDirty_ = false;
                        videoPresentDeferredForAudioLowWater_ = false;
                        lastRenderSummary_ = "Presented (headless)";
                    }
                } else {
                    ++stats_.renderServicePresentFailureCount;
                    lastRenderSummary_ = "Frame prepared but backend not ready";
                }
            }
            applyWindowVisibilityRequest();
        }

        // ── Outside lock: SDL texture upload + SDL_RenderPresent ─────────────
        bool presentOk = false;
        std::string presentError;
        if (processedFrame.has_value()) {
            if (videoPresenter_ != nullptr && videoPresenter_->ready()) {
                presentOk = videoPresenter_->present(*processedFrame);
                if (!presentOk) {
                    presentError = std::string(videoPresenter_->lastError());
                }
            } else {
                presentError = "presenter not ready";
            }
        }

        // ── Second lock block: post-present state update ──────────────────────
        if (processedFrame.has_value()) {
            std::unique_lock<std::mutex> lock(sharedStateMutex_);
            if (videoService_ != nullptr) {
                videoService_->recordPresentOutcome(presentOk, presentError);
            }
            syncVideoTransportStats();
            if (presentOk) {
                presented = true;
                ++stats_.renderServicePresentSuccessCount;
                ++stats_.framesPresented;
                frameDirty_ = false;
                videoPresentDeferredForAudioLowWater_ = false;
                lastRenderSummary_ = "Presented frame " +
                    std::to_string(processedFrame->width) + "x" +
                    std::to_string(processedFrame->height);
            } else {
                ++stats_.renderServicePresentFailureCount;
                lastBackendError_ = presentError;
                lastRenderSummary_ = lastBackendError_.empty()
                    ? "Video presentation failed" : lastBackendError_;
                appendLog("sdl: video presentation failed: " + lastRenderSummary_);
            }
        }

        return handledEvents != 0 || hadFrame || hasAudioState || hadAudioPreview ||
               visibilityChanged || presented || audioActive || quitRequested_;
    }

    void setQueuedDigitalInputMask(uint32_t pressedMask) override
    {
        queuedDigitalInputMask_.store(static_cast<int32_t>(pressedMask & 0x00FFu),
                                      std::memory_order_release);
        publishQueuedInputToService();
        appendLog("sdl: queued input mask=" + BMMQ::detail::hexByte(static_cast<uint8_t>(pressedMask & 0x00FFu)));
    }

    void clearQueuedDigitalInputMask() override
    {
        // Publish an explicit neutral snapshot before clearing the mask so the
        // InputService observes "no buttons pressed" rather than only local state loss.
        queuedDigitalInputMask_.store(0, std::memory_order_release);
        publishQueuedInputToService();
        queuedDigitalInputMask_.store(-1, std::memory_order_release);
        appendLog("sdl: published neutral input and cleared queued input mask");
    }

    [[nodiscard]] std::optional<uint32_t> queuedDigitalInputMask() const noexcept override
    {
        const auto v = queuedDigitalInputMask_.load(std::memory_order_acquire);
        if (v < 0) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(v);
    }

    void pressButton(BMMQ::InputButton button) override
    {
        setButtonState(button, true);
    }

    void releaseButton(BMMQ::InputButton button) override
    {
        setButtonState(button, false);
    }

    [[nodiscard]] bool isButtonPressed(BMMQ::InputButton button) const noexcept override
    {
        const auto v = queuedDigitalInputMask_.load(std::memory_order_acquire);
        if (v < 0) {
            return false;
        }
        return (static_cast<uint32_t>(v) & buttonMask(button)) != 0;
    }

    void clearQuitRequest() noexcept override
    {
        quitRequested_ = false;
    }

    [[nodiscard]] bool quitRequested() const noexcept override
    {
        return quitRequested_;
    }

    [[nodiscard]] std::string_view lastHostEventSummary() const noexcept override
    {
        return lastHostEventSummary_;
    }

    [[nodiscard]] std::string_view lastBackendError() const noexcept override
    {
        return lastBackendError_;
    }

    [[nodiscard]] std::string backendStatusSummary() const override
    {
        std::string summary = std::string(backendName());
        summary += backendReady_ ? " ready" : " not-ready";
        if (config_.enableAudio) {
            summary += audioOutputReady() ? " audio=ready" : " audio=not-ready";
            const auto queuedBytes = queuedAudioBytes();
            if (queuedBytes != 0u) {
                summary += " queued-audio=" + std::to_string(queuedBytes) + "B";
            }
        }
        if (timingService_ != nullptr) {
            summary += " timing-profile=";
            summary += BMMQ::timingPolicyProfileName(timingService_->stats().activeProfile);
        }
        if (!lastBackendError_.empty()) {
            summary += " error=" + lastBackendError_;
        }
        if (!lastRenderSummary_.empty()) {
            summary += " render='" + lastRenderSummary_ + "'";
        }
        return summary;
    }

    [[nodiscard]] bool handleHostEvent(const BMMQ::SdlFrontendHostEvent& event) override
    {
        ++stats_.hostEventsHandled;
        switch (event.type) {
        case BMMQ::SdlFrontendHostEventType::Quit:
            lastHostEventSummary_ = "Quit requested";
            requestQuit();
            return true;
        case BMMQ::SdlFrontendHostEventType::KeyDown:
        case BMMQ::SdlFrontendHostEventType::KeyUp: {
            if (event.repeat && event.type == BMMQ::SdlFrontendHostEventType::KeyDown) {
                lastHostEventSummary_ = "Ignored repeated keydown";
                appendLog("sdl: repeated host key ignored");
                return false;
            }

            const auto mapped = mapHostKey(event.key);
            if (mapped.has_value()) {
                ++stats_.keyEventsHandled;
                const bool pressed = event.type == BMMQ::SdlFrontendHostEventType::KeyDown;
                setButtonState(*mapped, pressed);
                lastHostEventSummary_ = std::string(buttonName(*mapped)) + (pressed ? " pressed from host" : " released from host");
                return true;
            }

            // Check for control keys (timing/frontend controls)
            const auto control = mapControlKey(event.key);
            if (control.has_value() && event.type == BMMQ::SdlFrontendHostEventType::KeyDown) {
                switch (*control) {
                case ControlAction::PauseToggle:
                    if (timingService_ != nullptr) {
                        const auto s = timingService_->stats();
                        timingService_->setPaused(!s.paused);
                        appendLog(std::string("sdl: timing paused=") + (s.paused ? "false" : "true"));
                        lastHostEventSummary_ = "Timing pause toggled";
                        return true;
                    }
                    break;
                case ControlAction::ThrottleToggle:
                    if (timingService_ != nullptr) {
                        const auto s = timingService_->stats();
                        timingService_->setThrottled(!s.throttled);
                        appendLog(std::string("sdl: timing throttled=") + (s.throttled ? "false" : "true"));
                        lastHostEventSummary_ = "Timing throttle toggled";
                        return true;
                    }
                    break;
                case ControlAction::SingleStep:
                    if (timingService_ != nullptr) {
                        timingService_->requestSingleStep();
                        appendLog("sdl: timing single-step requested");
                        lastHostEventSummary_ = "Timing single-step requested";
                        return true;
                    }
                    break;
                case ControlAction::SpeedUp:
                    if (timingService_ != nullptr) {
                        const auto s = timingService_->stats();
                        timingService_->setSpeedMultiplier(s.speedMultiplier * 2.0);
                        appendLog("sdl: timing speed x2");
                        lastHostEventSummary_ = "Timing speed increased";
                        return true;
                    }
                    break;
                case ControlAction::SpeedDown:
                    if (timingService_ != nullptr) {
                        const auto s = timingService_->stats();
                        timingService_->setSpeedMultiplier(std::max(0.125, s.speedMultiplier / 2.0));
                        appendLog("sdl: timing speed /2");
                        lastHostEventSummary_ = "Timing speed decreased";
                        return true;
                    }
                    break;
                }
            }

            lastHostEventSummary_ = "Unmapped host key";
            appendLog("sdl: unmapped host key ignored");
            return false;
        }
        case BMMQ::SdlFrontendHostEventType::None:
            break;
        }

        lastHostEventSummary_ = "No host event";
        return false;
    }

    [[nodiscard]] std::string_view backendName() const noexcept override
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        return "SDL2 frontend";
#else
        return "SDL2 unavailable";
#endif
    }

    [[nodiscard]] bool backendReady() const noexcept override
    {
        return backendReady_;
    }

    [[nodiscard]] bool audioOutputReady() const noexcept override
    {
        return audioOutput_ != nullptr && audioOutput_->ready();
    }

    [[nodiscard]] std::size_t bufferedAudioSamples() const noexcept override
    {
        const auto buffered = audioService_ != nullptr ? audioService_->engine().bufferedSamples() : 0u;
        syncAudioTransportStats();
        return buffered;
    }

    [[nodiscard]] bool audioQueueBackpressureActive() const noexcept override
    {
        if (!config_.enableAudio || audioService_ == nullptr || !audioOutputReady()) {
            return false;
        }

        const auto capacity = audioService_->engine().bufferCapacitySamples();
        if (capacity == 0u) {
            return false;
        }

        const auto safetyMarginSamples = computeAudioSafetyMarginSamples();
        const auto highWater = capacity > safetyMarginSamples ? capacity - safetyMarginSamples : capacity;
        return audioService_->engine().bufferedSamples() >= highWater;
    }

    [[nodiscard]] uint32_t queuedAudioBytes() const noexcept override
    {
        if (audioService_ == nullptr) {
            return 0u;
        }
        return static_cast<uint32_t>(audioService_->engine().queuedBytes());
    }

    bool tryInitializeBackend() override
    {
        std::scoped_lock<std::mutex> lock(sharedStateMutex_);
        ++stats_.backendInitAttempts;
        lastBackendError_.clear();

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (backendReady_) {
            appendLog("sdl: backend already initialized");
            return true;
        }

        const auto initAction = [this]() -> BMMQ::MachineTransitionMutationResult {
            BMMQ::MachineTransitionMutationResult result;
            uint32_t initFlags = SDL_INIT_EVENTS;
            if (config_.enableAudio) {
                initFlags |= SDL_INIT_AUDIO;
            }

            if (SDL_InitSubSystem(initFlags) != 0) {
                lastBackendError_ = SDL_GetError();
                appendLog("sdl: backend init failed: " + lastBackendError_);
                backendReady_ = false;
                initializedBackendFlags_ = 0;
                result.success = false;
                result.videoReady = false;
                result.audioReady = false;
                return result;
            }

            initializedBackendFlags_ = initFlags;
            const bool videoReady = !config_.enableVideo || ensureVideoPresenter(false);
            if (!videoReady) {
                appendLog("sdl: continuing without live video output");
            }

            const bool audioReady = !config_.enableAudio || ensureAudioDevice(false);
            if (!audioReady) {
                appendLog("sdl: continuing without live audio output");
            }

            backendReady_ = true;
            startRenderServiceIfNeeded();
            appendLog("sdl: backend initialized");
            result.success = videoReady && audioReady;
            result.videoReady = videoReady;
            result.audioReady = audioReady;
            return result;
        };

        if (lifecycleCoordinator_ != nullptr) {
            return lifecycleCoordinator_->transitionConfigReconfigure(initAction);
        }
        return initAction().success;
#else
        appendLog("sdl: SDL2 backend unavailable; running in skeleton mode");
        backendReady_ = false;
        initializedBackendFlags_ = 0;
        return false;
#endif
    }

    std::size_t pumpBackendEvents() override
    {
        // Use the same atomic counters as collectBackendEvents() so that
        // stats() sees a unified total regardless of which poll path ran.
        eventPumpCallsAtomic_.fetch_add(1u, std::memory_order_relaxed);
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (!backendReady_) {
            return 0;
        }

        std::size_t handled = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (const auto translated = translateSdlEvent(event); translated.has_value()) {
                if (handleHostEvent(*translated)) {
                    ++handled;
                }
            }
        }
        backendEventsTranslatedAtomic_.fetch_add(handled, std::memory_order_relaxed);
        return handled;
#else
        return 0;
#endif
    }

    void setDebugSnapshotService(BMMQ::DebugSnapshotService* service) noexcept override
    {
        debugSnapshotService_ = service;
    }

    [[nodiscard]] BMMQ::DebugSnapshotService* debugSnapshotService() const noexcept override
    {
        return debugSnapshotService_;
    }

    void onAttach(BMMQ::MutableMachineView& view) override
    {
        ++stats_.attachCount;
        lifecycleCoordinator_ = &view.mutableMachine.lifecycleCoordinator();
        audioService_ = &view.audioService();
        audioService_->setBackendPausedOrClosed(true);
        inputService_ = &view.inputService();
        if (config_.enableInput) {
            (void)inputService_->attachExternalAdapter(*this);
            (void)inputService_->configureMappingProfile("sdl-default");
            (void)inputService_->resume();
        }
        videoService_ = &view.videoService();
        timingService_ = &view.timingService();
        (void)configureVideoService();
        appendLog("sdl: attached");
        if (config_.autoInitializeBackend) {
            tryInitializeBackend();
        }
    }

    void onDetach(BMMQ::MutableMachineView&) override
    {
        ++stats_.detachCount;
        shutdownBackend();
        audioService_ = nullptr;
        if (config_.enableInput && inputService_ != nullptr) {
            (void)inputService_->detachAdapter(inputService_->currentGeneration() + 1u);
        }
        inputService_ = nullptr;
        videoService_ = nullptr;
        timingService_ = nullptr;
        lifecycleCoordinator_ = nullptr;
        videoPresenter_ = nullptr;
        windowVisible_.store(false, std::memory_order_release);
        appendLog("sdl: detached");
    }

    void onVideoEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        // Pre-build video packet OUTSIDE the lock.
        // Machine state is owned by the emulation thread, which is the caller here,
        // so reading view.realtimeVideoPacket() / videoDebugFrameModel() before
        // acquiring sharedStateMutex_ is safe.
        const bool carriesVideoStateEarly =
            event.type == BMMQ::MachineEventType::MemoryWriteObserved ||
            event.type == BMMQ::MachineEventType::VBlank ||
            event.type == BMMQ::MachineEventType::VideoScanlineReady;

        std::optional<BMMQ::RealtimeVideoPacket> prebuiltRealtime;
        if (carriesVideoStateEarly &&
            event.type != BMMQ::MachineEventType::VideoScanlineReady) {
            prebuiltRealtime = view.realtimeVideoPacket({
                .frameWidth = std::max(config_.frameWidth, 1),
                .frameHeight = std::max(config_.frameHeight, 1),
            });
        }

        // Pre-build video debug model OUTSIDE the lock for VBlank/scanline events.
        // Only build the debug model when a debug consumer is connected — the VDP
        // traversal is expensive (touches all VRAM/OAM) and the result is thrown
        // away in production mode (Phase 38B).
        // debugSnapshotService_ is set in onAttach/onDetach, both serial with
        // onVideoEvent on the emulation thread, so reading it here is safe.
        const bool needsDebugModel = debugSnapshotService_ != nullptr;
        std::optional<BMMQ::VideoDebugFrameModel> prebuiltDebugModel;
        if (carriesVideoStateEarly && needsDebugModel) {
            prebuiltDebugModel = view.videoDebugFrameModel({
                .frameWidth = std::max(config_.frameWidth, 1),
                .frameHeight = std::max(config_.frameHeight, 1),
            });
        } else if (carriesVideoStateEarly) {
            videoDebugModelBuildSkipCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
        }

        // submittedVideoFrame is hoisted before the lock so it is visible to the
        // post-lock notification block (Phase 36A).
        bool submittedVideoFrame = false;
        {
            std::unique_lock<std::mutex> lock(sharedStateMutex_);
            if (!config_.enableVideo) {
                return;
            }
            ++stats_.videoEvents;
            const bool carriesVideoState =
                event.type == BMMQ::MachineEventType::MemoryWriteObserved ||
                event.type == BMMQ::MachineEventType::VBlank ||
                event.type == BMMQ::MachineEventType::VideoScanlineReady;
            const bool shouldSampleVideoState =
                carriesVideoState &&
                (event.type == BMMQ::MachineEventType::VBlank ||
                 event.type == BMMQ::MachineEventType::VideoScanlineReady ||
                 !lastFrame_.has_value());

            if (shouldSampleVideoState) {
                const bool deferPresentForAudioLowWater =
                    event.type == BMMQ::MachineEventType::VBlank && shouldDeferVideoFrameForAudioLowWater();

                bool realtimeSubmitted = false;
                if (prebuiltRealtime.has_value()) {
                    ++stats_.videoRealtimePacketsBuiltOutsideLock;
                    realtimeSubmitted = trySubmitPrebuiltRealtimeVideoPacket(event, std::move(*prebuiltRealtime));
                } else {
                    realtimeSubmitted = trySubmitRealtimeVideoPacket(event, view);
                }

                if (realtimeSubmitted) {
                    ++stats_.videoRealtimeRenderRequestCount;
                    switch (videoRenderEventBucket(event.type)) {
                    case VideoRenderEventBucket::VBlank:
                        ++stats_.videoRealtimeRenderFromVBlankCount;
                        break;
                    case VideoRenderEventBucket::Scanline:
                        ++stats_.videoRealtimeRenderFromScanlineCount;
                        break;
                    case VideoRenderEventBucket::MemoryWrite:
                        ++stats_.videoRealtimeRenderFromMemoryWriteCount;
                        break;
                    case VideoRenderEventBucket::Other:
                        ++stats_.videoRealtimeRenderFromOtherCount;
                        break;
                    }
                    submittedVideoFrame = true;
                    // When the realtime packet path succeeds, forward the pre-built debug
                    // model to DebugSnapshotService so the render thread can consume it.
                    if (debugSnapshotService_ != nullptr && prebuiltDebugModel.has_value()) {
                        (void)debugSnapshotService_->submitVideoModel(std::move(prebuiltDebugModel));
                    }
                    syncVideoTransportStats();
                    ++stats_.framesPrepared;
                    frameDirty_ = true;
                    // renderServiceFramePending_ and renderServiceWakeCv_.notify_all()
                    // are deferred to after the lock releases (Phase 36A).
                    if (deferPresentForAudioLowWater) {
                        ++stats_.audioQueueLowWaterHits;
                        videoPresentDeferredForAudioLowWater_ = true;
                        appendLog("sdl: deferred video presentation while audio buffer was low");
                    }
                } else if (BMMQ::VideoDebugFrameModel* videoModel = modelForVideoEvent(event, view, std::move(prebuiltDebugModel)); videoModel != nullptr) {
                    if (videoService_ != nullptr && videoService_->submitVideoDebugModel(event, *videoModel)) {
                        ++stats_.videoDebugRenderRequestCount;
                        switch (videoRenderEventBucket(event.type)) {
                        case VideoRenderEventBucket::VBlank:
                            ++stats_.videoDebugRenderFromVBlankCount;
                            break;
                        case VideoRenderEventBucket::Scanline:
                            ++stats_.videoDebugRenderFromScanlineCount;
                            break;
                        case VideoRenderEventBucket::MemoryWrite:
                            ++stats_.videoDebugRenderFromMemoryWriteCount;
                            break;
                        case VideoRenderEventBucket::Other:
                            ++stats_.videoDebugRenderFromOtherCount;
                            break;
                        }
                        submittedVideoFrame = true;
                        if (event.type == BMMQ::MachineEventType::VBlank) {
                            lastVideoDebugModel_ = *videoModel;
                            scanlineVideoDebugModel_.reset();
                        }
                        syncVideoTransportStats();
                        ++stats_.framesPrepared;
                        frameDirty_ = true;
                        // renderServiceFramePending_ and renderServiceWakeCv_.notify_all()
                        // are deferred to after the lock releases (Phase 36A).
                        if (deferPresentForAudioLowWater) {
                            ++stats_.audioQueueLowWaterHits;
                            videoPresentDeferredForAudioLowWater_ = true;
                            appendLog("sdl: deferred video presentation while audio buffer was low");
                        }
                    }
                } else {
                    lastFrame_.reset();
                    frameDirty_ = false;
                    scanlineVideoDebugModel_.reset();
                }
            }

            if (event.type != BMMQ::MachineEventType::MemoryWriteObserved &&
                event.type != BMMQ::MachineEventType::VideoScanlineReady &&
                event.type != BMMQ::MachineEventType::VBlank) {
                std::string message = std::string("sdl: video event=") + BMMQ::detail::machineEventTypeName(event.type);
                if (lastVideoDebugModel_.has_value()) {
                    message += lastVideoDebugModel_->displayEnabled ? " display=on" : " display=off";
                    if (lastVideoDebugModel_->scanlineIndex.has_value()) {
                        message += " scanline=" + std::to_string(*lastVideoDebugModel_->scanlineIndex);
                    }
                }
                if (lastFrame_.has_value()) {
                    message += " frame=" + std::to_string(lastFrame_->width) + "x" + std::to_string(lastFrame_->height);
                }
                appendLog(std::move(message));
            } else if (submittedVideoFrame) {
                appendLog("sdl: scanline frame prepared");
            }
        } // sharedStateMutex_ released here

        // Phase 36A: notify the render service AFTER the lock has been released.
        // renderServiceFramePending_ is an atomic; renderServiceWakeCv_ is paired
        // with renderServiceWaitMutex_ (not sharedStateMutex_), so both are safe
        // to signal here. The render thread may already have consumed the frame
        // via a timeout wake — the resulting spurious wake is harmless.
        if (submittedVideoFrame) {
            renderServiceFramePending_.store(true, std::memory_order_release);
            renderServiceWakeCv_.notify_all();
            onVideoEventFrameNotifyOutsideLockCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
        }
    }

    void onAudioEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        // Pre-build audio sources OUTSIDE the lock.
        // Machine state is owned by the emulation thread (the caller), so reading
        // view.realtimeAudioPacket() / view.audioState() before acquiring
        // sharedStateMutex_ is safe — same guarantee as the video event path.
        //
        // Phase 38A: view.audioState() is lazy — only called when the realtime
        // packet is absent or has a stale contract version, i.e. the fallback path.
        // This avoids a PCM vector copy on every frame in production mode.
        const auto audioT0 = std::chrono::steady_clock::now();
        const auto prebuiltRealtimePacket = view.realtimeAudioPacket();
        const bool realtimePacketValid = prebuiltRealtimePacket.has_value() &&
            prebuiltRealtimePacket->contractVersion == BMMQ::RealtimeAudioPacket::kContractVersion;
        const auto prebuiltAudioState = realtimePacketValid ? std::optional<BMMQ::AudioStateView>{} : view.audioState();
        const auto audioElapsedNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - audioT0).count());

        // Phase 38A: hoist buildAudioPreview(RealtimeAudioPacket) before the lock.
        // The const overload reads only from its argument + audioService_/config_
        // (both written only in onAttach/onDetach, serial with this call) — safe.
        // The AudioStateView overload mutates audioPhase_ and stays inside the lock.
        std::optional<BMMQ::SdlAudioPreviewBuffer> prebuiltRealtimePreview;
        if (realtimePacketValid) {
            prebuiltRealtimePreview = buildAudioPreview(*prebuiltRealtimePacket);
        }

        // Call appendRecentPcm BEFORE acquiring sharedStateMutex_.
        // appendRecentPcm() is internally thread-safe (SPSC ring buffer + atomic
        // notify to the audio worker); no outer lock is required.
        // audioService_ is set in onAttach and cleared in onDetach, both called
        // from the emulation thread — serial with onAudioEvent, no race.
        if (audioService_ != nullptr) {
            if (realtimePacketValid) {
                audioService_->appendRecentPcm(prebuiltRealtimePacket->pcmSamples,
                                               prebuiltRealtimePacket->frameCounter);
            } else if (prebuiltAudioState.has_value()) {
                audioService_->appendRecentPcm(prebuiltAudioState->pcmSamples,
                                               prebuiltAudioState->frameCounter);
            }
        }

        std::scoped_lock<std::mutex> lock(sharedStateMutex_);
        if (!config_.enableAudio) {
            return;
        }
        ++stats_.audioEvents;
        if (realtimePacketValid) {
            ++stats_.audioRealtimePacketsAccepted;
            // Pre-built outside the lock (Phase 38A); just move into place.
            lastAudioPreview_ = std::move(prebuiltRealtimePreview);
            ++stats_.audioPreviewsBuilt;
            ++audioPreviewGeneration_;
            // appendRecentPcm already dispatched above, outside the lock.
            lastAudioState_.reset();
        } else if (prebuiltRealtimePacket.has_value()) {
            ++stats_.audioRealtimePacketsSkipped;
            lastAudioPreview_.reset();
        } else if (prebuiltAudioState.has_value()) {
            // Use the pre-built audio state (built outside the lock).
            ++stats_.audioStateSnapshotsBuilt;
            stats_.audioStateSnapshotDurationLastNs = audioElapsedNs;
            stats_.audioStateSnapshotDurationHighWaterNs =
                std::max(stats_.audioStateSnapshotDurationHighWaterNs, audioElapsedNs);
            lastAudioState_ = prebuiltAudioState;
            lastAudioPreview_ = buildAudioPreview(*lastAudioState_);
            ++stats_.audioPreviewsBuilt;
            ++audioPreviewGeneration_;
            // appendRecentPcm already dispatched above, outside the lock.
            if (debugSnapshotService_ != nullptr) {
                (void)debugSnapshotService_->submitAudioState(lastAudioState_);
            }
        } else {
            ++stats_.audioRealtimePacketsSkipped;
            lastAudioPreview_.reset();
            if (audioService_ != nullptr && audioService_->canPerformReset()) {
                (void)audioService_->resetStats();
                (void)audioService_->resetStream();
            }
        }

        (void)event;
    }

    std::optional<uint32_t> sampleDigitalInput(const BMMQ::MachineView&) override
    {
        if (const auto sample = sampleDigitalInput(); sample.has_value()) {
            return static_cast<uint32_t>(*sample);
        }
        return std::nullopt;
    }

    std::optional<BMMQ::InputButtonMask> sampleDigitalInput() override
    {
        if (!config_.enableInput) {
            return std::nullopt;
        }
        inputPollsAtomic_.fetch_add(1u, std::memory_order_relaxed);
        if (config_.pumpBackendEventsOnInputSample && !renderServiceActive()) {
            // Slow path: pumpBackendEvents() is not thread-safe; serialise with
            // the render service loop via the shared state mutex.
            std::scoped_lock<std::mutex> lock(sharedStateMutex_);
            pumpBackendEvents();
        }
        const auto v = queuedDigitalInputMask_.load(std::memory_order_acquire);
        if (v >= 0) {
            inputSamplesProvidedAtomic_.fetch_add(1u, std::memory_order_relaxed);
            return static_cast<BMMQ::InputButtonMask>(static_cast<uint32_t>(v) & 0x00FFu);
        }
        return std::nullopt;
    }

    [[nodiscard]] BMMQ::InputPluginCapabilities capabilities() const noexcept override
    {
        return {
            .pollingSafe = true,
            .eventPumpSafe = true,
            .deterministic = true,
            .supportsDigital = config_.enableInput,
            .supportsAnalog = false,
            .fixedLogicalLayout = true,
            .hotSwapSafe = false,
            .liveSeek = false,
            .nonRealtimeOnly = false,
            .headlessSafe = true,
        };
    }

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return displayName();
    }

    [[nodiscard]] std::string_view lastError() const noexcept override
    {
        return lastBackendError();
    }

    // InputService expects open() to validate adapter availability, but this frontend
    // performs real backend initialization through tryInitializeBackend() instead.
    [[nodiscard]] bool open() override
    {
        return true;
    }

    // InputService may call close() during adapter lifecycle transitions, but actual
    // backend teardown is owned by shutdownBackend() rather than this adapter hook.
    void close() noexcept override {}

    void onDigitalInputEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        if (!config_.enableInput) {
            return;
        }
        ++stats_.inputEvents;
        lastInputState_ = view.digitalInputState();

        std::string message = std::string("sdl: input event=") + BMMQ::detail::machineEventTypeName(event.type);
        if (lastInputState_.has_value()) {
            message += " pressed=" + BMMQ::detail::hexByte(lastInputState_->pressedMask);
        }
        appendLog(std::move(message));
    }

private:
    [[nodiscard]] bool renderServiceActive() const noexcept
    {
        return renderServiceState_.load(std::memory_order_acquire) == BMMQ::SdlRenderServiceState::Active;
    }

    void setRenderServiceState(BMMQ::SdlRenderServiceState state) noexcept
    {
        // stats_.renderServiceState is NOT written here to avoid a data race:
        // this function runs on the render thread without sharedStateMutex_, while
        // stats() reads stats_ under the lock. stats() reads directly from the
        // renderServiceState_ atomic, so writing stats_.renderServiceState here
        // is redundant and would introduce a race (TSAN 36B).
        renderServiceState_.store(state, std::memory_order_release);
    }

    void startRenderServiceIfNeeded()
    {
        if (!config_.enableRenderServiceThread || !config_.enableVideo) {
            setRenderServiceState(BMMQ::SdlRenderServiceState::Stopped);
            return;
        }
        if (renderServiceThread_.joinable()) {
            return;
        }

        renderServiceStopRequested_.store(false, std::memory_order_release);
        setRenderServiceState(BMMQ::SdlRenderServiceState::Starting);
        renderServiceThread_ = std::thread([this]() { renderServiceLoop(); });
    }

    void stopRenderService()
    {
        if (!renderServiceThread_.joinable()) {
            setRenderServiceState(BMMQ::SdlRenderServiceState::Stopped);
            return;
        }

        setRenderServiceState(BMMQ::SdlRenderServiceState::Stopping);
        renderServiceStopRequested_.store(true, std::memory_order_release);
        renderServiceWakeCv_.notify_all();
        renderServiceThread_.join();
        setRenderServiceState(BMMQ::SdlRenderServiceState::Stopped);
    }

    // ──────────────────────────────────────────────────────────────────────────
    // Render-service event helpers (Phase 35A)
    // ──────────────────────────────────────────────────────────────────────────
    // Collect SDL events WITHOUT holding sharedStateMutex_.
    // Translates raw SDL_Events into typed SdlFrontendHostEvents stored in a
    // caller-supplied stack buffer. Returns the count of events collected.
    // Safe on the render thread without a lock: SDL_PollEvent uses SDL's own
    // internal queue lock; the rendering thread is the sole poller.
    // backendReady_ is read relaxed — a false-negative costs at most one skipped
    // poll iteration; backendReady_ is written only under sharedStateMutex_.
    static constexpr std::size_t kEventBatchCapacity = 64u;
    using EventBatch = std::array<BMMQ::SdlFrontendHostEvent, kEventBatchCapacity>;

    [[nodiscard]] std::size_t collectBackendEvents(EventBatch& batch) noexcept
    {
        eventPumpCallsAtomic_.fetch_add(1u, std::memory_order_relaxed);
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (!backendReady_) {
            return 0u;
        }
        std::size_t count = 0u;
        SDL_Event sdlEvent;
        while (count < kEventBatchCapacity && SDL_PollEvent(&sdlEvent) != 0) {
            if (const auto translated = translateSdlEvent(sdlEvent); translated.has_value()) {
                batch[count++] = *translated;
            }
        }
        backendEventsTranslatedAtomic_.fetch_add(count, std::memory_order_relaxed);
        return count;
#else
        (void)batch;
        return 0u;
#endif
    }

    // Apply pre-collected host events WHILE holding sharedStateMutex_.
    // Brief locked counterpart to collectBackendEvents(); mutates plugin state.
    // Returns the number of events handled (same semantics as pumpBackendEvents).
    std::size_t applyCollectedEvents(const EventBatch& batch, std::size_t count)
    {
        std::size_t handled = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            if (handleHostEvent(batch[i])) {
                ++handled;
            }
        }
        if (handled != 0u) {
            renderServiceEventPumpCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
        }
        return handled;
    }

    void renderServiceLoop()
    {
        setRenderServiceState(BMMQ::SdlRenderServiceState::Active);
        auto lastWake = std::chrono::steady_clock::now();
        const auto sleepInterval = std::chrono::milliseconds(2);

        while (!renderServiceStopRequested_.load(std::memory_order_acquire)) {
            // ── Pre-lock: collect SDL events lock-free (Phase 35A) ───────────────
            // SDL_PollEvent runs here WITHOUT sharedStateMutex_ so the emulation
            // thread's onVideoEvent/onAudioEvent can deposit frames and PCM without
            // waiting for SDL I/O to complete.
            EventBatch eventBatch{};
            const std::size_t eventCount = collectBackendEvents(eventBatch);

            // ── First lock block: apply events, sync, consume frame ──────────────
            std::optional<BMMQ::VideoFramePacket> processedFrame;
            {
                std::unique_lock<std::mutex> lock(sharedStateMutex_);
                ++stats_.renderServiceLoopCount;
                renderServiceFramePending_.store(false, std::memory_order_relaxed);
                // Apply pre-collected events under the lock (state mutation).
                applyCollectedEvents(eventBatch, eventCount);
                ++stats_.renderServiceLightweightSyncCount;
                syncPresentDecisionState();
                const bool hadFrame = config_.enableVideo && lastFrame_.has_value();
                const bool visibilityChanged =
                    windowVisible_.load(std::memory_order_acquire) !=
                    windowVisibilityRequested_.load(std::memory_order_acquire);
                if (hadFrame && (frameDirty_ || visibilityChanged)) {
                    ++stats_.renderServicePresentAttempts;
                    if (frameDirty_ && shouldDeferVideoFrameForAudioLowWater()) {
                        videoPresentDeferredForAudioLowWater_ = true;
                    } else if (videoService_ != nullptr &&
                               backendReady_ &&
                               videoService_->state() == BMMQ::VideoLifecycleState::Active) {
                        if (config_.showWindowOnPresent) {
                            requestWindowVisibility(true);
                        }
                        processedFrame = videoService_->consumeAndProcessFrame();
                        if (processedFrame.has_value()) {
                            ++stats_.renderServicePresentCallsOutsideLock;
                        } else {
                            // headless: consumeAndProcessFrame set state internally; treat as success
                            ++stats_.renderServicePresentSuccessCount;
                            frameDirty_ = false;
                            videoPresentDeferredForAudioLowWater_ = false;
                            lastRenderSummary_ = "Presented (headless)";
                        }
                    } else {
                        ++stats_.renderServicePresentFailureCount;
                        lastRenderSummary_ = "Frame prepared but backend not ready";
                    }
                }
                applyWindowVisibilityRequest();
            }
            // ── Outside lock: SDL texture upload + SDL_RenderPresent ─────────────
            bool presentOk = false;
            std::string presentError;
            if (processedFrame.has_value()) {
                if (videoPresenter_ != nullptr && videoPresenter_->ready()) {
                    presentOk = videoPresenter_->present(*processedFrame);
                    if (!presentOk) {
                        presentError = std::string(videoPresenter_->lastError());
                    }
                } else {
                    presentError = "presenter not ready";
                }
            }
            // ── Second lock block: post-present state update ─────────────────────
            if (processedFrame.has_value()) {
                std::unique_lock<std::mutex> lock(sharedStateMutex_);
                if (videoService_ != nullptr) {
                    videoService_->recordPresentOutcome(presentOk, presentError);
                }
                syncVideoTransportStats();
                if (presentOk) {
                    ++stats_.renderServicePresentSuccessCount;
                    ++stats_.framesPresented;
                    frameDirty_ = false;
                    videoPresentDeferredForAudioLowWater_ = false;
                    lastRenderSummary_ = "Presented frame " +
                        std::to_string(processedFrame->width) + "x" +
                        std::to_string(processedFrame->height);
                } else {
                    ++stats_.renderServicePresentFailureCount;
                    lastBackendError_ = presentError;
                    lastRenderSummary_ = lastBackendError_.empty() ? "Video presentation failed" : lastBackendError_;
                    appendLog("sdl: video presentation failed: " + lastRenderSummary_);
                }
            }

            renderServiceSleepCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
            {
                std::unique_lock<std::mutex> waitLock(renderServiceWaitMutex_);
                const auto beforeWait = std::chrono::steady_clock::now();
                const bool useShortSleep = videoPresentDeferredForAudioLowWater_;
                if (useShortSleep) {
                    renderServiceDeferredPresentFastSleepCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
                }
                const auto effectiveInterval = useShortSleep
                    ? std::chrono::microseconds(500)
                    : sleepInterval;
                const bool predicateMet = renderServiceWakeCv_.wait_for(waitLock, effectiveInterval, [this] {
                    return renderServiceStopRequested_.load(std::memory_order_acquire)
                        || renderServiceFramePending_.load(std::memory_order_acquire);
                });
                const auto wakeTime = std::chrono::steady_clock::now();
                if (predicateMet) {
                    renderServiceFrameWakeCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
                } else {
                    renderServiceTimeoutWakeCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
                }
                if (wakeTime - beforeWait > sleepInterval + std::chrono::milliseconds(2)) {
                    renderServiceSleepOvershootCountAtomic_.fetch_add(1u, std::memory_order_relaxed);
                }
                lastWake = wakeTime;
            }
            (void)lastWake;
        }
    }

    void applyLifecycleRecoveryPolicy()
    {
        if (lifecycleCoordinator_ == nullptr) {
            return;
        }
        const auto transition = lifecycleCoordinator_->lastTransitionResult();
        if (!shouldAttemptLifecycleRecovery(transition)) {
            return;
        }
        if (stats_.serviceCalls - lastLifecycleRecoveryAttemptServiceCall_ < kLifecycleRecoveryCooldownCalls) {
            ++stats_.lifecycleRecoveryCooldownSuppressCount;
            return;
        }
        lastLifecycleRecoveryAttemptServiceCall_ = stats_.serviceCalls;

        bool attemptedVideo = false;
        bool attemptedAudio = false;

        const bool attemptVideo =
            transition.outcome == BMMQ::MachineTransitionOutcome::Degraded
                ? transition.degradedHeadlessVideo
                : (transition.failureStage == BMMQ::MachineTransitionFailureStage::Resume ||
                   transition.failureStage == BMMQ::MachineTransitionFailureStage::Mutation);
        if (attemptVideo) {
            attemptedVideo = true;
            ++stats_.lifecycleRecoveryVideoAttemptCount;
            if (ensureVideoPresenter()) {
                ++stats_.lifecycleRecoveryVideoSuccessCount;
            } else {
                ++stats_.lifecycleRecoveryVideoFailureCount;
            }
        }

        const bool attemptAudio =
            transition.outcome == BMMQ::MachineTransitionOutcome::Degraded
                ? transition.degradedAudioDisabled
                : (transition.failureStage == BMMQ::MachineTransitionFailureStage::Resume ||
                   transition.failureStage == BMMQ::MachineTransitionFailureStage::Mutation);
        if (attemptAudio) {
            attemptedAudio = true;
            ++stats_.lifecycleRecoveryAudioAttemptCount;
            const bool reopened = ensureAudioDevice();
            syncAudioTransportStats();
            const bool transportPrimed = audioService_ != nullptr && audioService_->isOutputTransportPrimed();
            if (reopened && transportPrimed) {
                ++stats_.lifecycleRecoveryAudioSuccessCount;
            } else {
                ++stats_.lifecycleRecoveryAudioFailureCount;
            }
        }

        if (attemptedVideo && attemptedAudio) {
            stats_.lifecycleRecoveryLastTarget = BMMQ::SdlLifecycleRecoveryTarget::VideoAndAudio;
        } else if (attemptedVideo) {
            stats_.lifecycleRecoveryLastTarget = BMMQ::SdlLifecycleRecoveryTarget::Video;
        } else if (attemptedAudio) {
            stats_.lifecycleRecoveryLastTarget = BMMQ::SdlLifecycleRecoveryTarget::Audio;
        }

        if (attemptedVideo || attemptedAudio) {
            const auto recoveryResult = lifecycleCoordinator_->lastTransitionResult();
            stats_.lifecycleRecoveryLastTransitionReason = recoveryResult.reason;
            stats_.lifecycleRecoveryLastTransitionOutcome = recoveryResult.outcome;
            stats_.lifecycleRecoveryLastTransitionFailureStage = recoveryResult.failureStage;
            stats_.lifecycleRecoveryLastTransitionRejectedForReentry = recoveryResult.rejectedForReentry;
            stats_.lifecycleRecoveryLastUsedCoordinator = true;
        }
    }

    [[nodiscard]] static bool shouldAttemptLifecycleRecovery(const BMMQ::MachineTransitionResult& transition) noexcept
    {
        if (transition.rejectedForReentry) {
            return false;
        }
        if (transition.outcome == BMMQ::MachineTransitionOutcome::Degraded) {
            return transition.degradedHeadlessVideo || transition.degradedAudioDisabled;
        }
        if (transition.outcome != BMMQ::MachineTransitionOutcome::Failed) {
            return false;
        }
        return transition.failureStage == BMMQ::MachineTransitionFailureStage::Mutation ||
               transition.failureStage == BMMQ::MachineTransitionFailureStage::Resume;
    }

    [[nodiscard]] std::size_t computeAudioSafetyMarginSamples() const noexcept
    {
        const auto callbackChunk =
            static_cast<std::size_t>(std::max(config_.audioCallbackChunkSamples, 1));
        return std::max<std::size_t>(callbackChunk * 2u, kApuFrameSamples);
    }

    [[nodiscard]] static constexpr uint8_t buttonMask(BMMQ::InputButton button) noexcept
    {
        return BMMQ::inputButtonMask(button);
    }

    [[nodiscard]] static constexpr std::string_view buttonName(BMMQ::InputButton button) noexcept
    {
        switch (button) {
        case BMMQ::InputButton::Right:
            return "Right";
        case BMMQ::InputButton::Left:
            return "Left";
        case BMMQ::InputButton::Up:
            return "Up";
        case BMMQ::InputButton::Down:
            return "Down";
        case BMMQ::InputButton::Button1:
            return "Button1";
        case BMMQ::InputButton::Button2:
            return "Button2";
        case BMMQ::InputButton::Meta1:
            return "Meta1";
        case BMMQ::InputButton::Meta2:
            return "Meta2";
        }
        return "Unknown";
    }

    [[nodiscard]] static constexpr std::optional<BMMQ::InputButton> mapHostKey(BMMQ::SdlFrontendHostKey key) noexcept
    {
        switch (key) {
        case BMMQ::SdlFrontendHostKey::Right:
            return BMMQ::InputButton::Right;
        case BMMQ::SdlFrontendHostKey::Left:
            return BMMQ::InputButton::Left;
        case BMMQ::SdlFrontendHostKey::Up:
            return BMMQ::InputButton::Up;
        case BMMQ::SdlFrontendHostKey::Down:
            return BMMQ::InputButton::Down;
        case BMMQ::SdlFrontendHostKey::Z:
            return BMMQ::InputButton::Button1;
        case BMMQ::SdlFrontendHostKey::X:
            return BMMQ::InputButton::Button2;
        case BMMQ::SdlFrontendHostKey::Backspace:
            return BMMQ::InputButton::Meta1;
        case BMMQ::SdlFrontendHostKey::Return:
            return BMMQ::InputButton::Meta2;
        case BMMQ::SdlFrontendHostKey::Pause:
        case BMMQ::SdlFrontendHostKey::ThrottleToggle:
        case BMMQ::SdlFrontendHostKey::SingleStep:
        case BMMQ::SdlFrontendHostKey::SpeedUp:
        case BMMQ::SdlFrontendHostKey::SpeedDown:
        case BMMQ::SdlFrontendHostKey::Unknown:
            break;
        }
        return std::nullopt;
    }

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    [[nodiscard]] static std::optional<BMMQ::SdlFrontendHostKey> mapSdlKeyCode(SDL_Keycode key) noexcept
    {
        switch (key) {
        case SDLK_RIGHT:
            return BMMQ::SdlFrontendHostKey::Right;
        case SDLK_LEFT:
            return BMMQ::SdlFrontendHostKey::Left;
        case SDLK_UP:
            return BMMQ::SdlFrontendHostKey::Up;
        case SDLK_DOWN:
            return BMMQ::SdlFrontendHostKey::Down;
        case SDLK_z:
            return BMMQ::SdlFrontendHostKey::Z;
        case SDLK_x:
            return BMMQ::SdlFrontendHostKey::X;
        case SDLK_BACKSPACE:
            return BMMQ::SdlFrontendHostKey::Backspace;
        case SDLK_RETURN:
            return BMMQ::SdlFrontendHostKey::Return;
        case SDLK_p:
            return BMMQ::SdlFrontendHostKey::Pause;
        case SDLK_o:
            return BMMQ::SdlFrontendHostKey::ThrottleToggle;
        case SDLK_n:
            return BMMQ::SdlFrontendHostKey::SingleStep;
        case SDLK_RIGHTBRACKET:
            return BMMQ::SdlFrontendHostKey::SpeedUp;
        case SDLK_LEFTBRACKET:
            return BMMQ::SdlFrontendHostKey::SpeedDown;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::optional<BMMQ::SdlFrontendHostEvent> translateSdlEvent(const SDL_Event& event) noexcept
    {
        switch (event.type) {
        case SDL_QUIT:
            return BMMQ::SdlFrontendHostEvent{BMMQ::SdlFrontendHostEventType::Quit, BMMQ::SdlFrontendHostKey::Unknown, false};
        case SDL_KEYDOWN: {
            const auto key = mapSdlKeyCode(event.key.keysym.sym);
            if (!key.has_value()) {
                return std::nullopt;
            }
            return BMMQ::SdlFrontendHostEvent{BMMQ::SdlFrontendHostEventType::KeyDown, *key, event.key.repeat != 0};
        }
        case SDL_KEYUP: {
            const auto key = mapSdlKeyCode(event.key.keysym.sym);
            if (!key.has_value()) {
                return std::nullopt;
            }
            return BMMQ::SdlFrontendHostEvent{BMMQ::SdlFrontendHostEventType::KeyUp, *key, false};
        }
        default:
            return std::nullopt;
        }
    }
#endif

    void setButtonState(BMMQ::InputButton button, bool pressed)
    {
        const auto current = queuedDigitalInputMask_.load(std::memory_order_acquire);
        auto mask = static_cast<uint8_t>((current >= 0 ? static_cast<uint32_t>(current) : 0u) & 0x00FFu);
        const auto bit = buttonMask(button);
        const auto oldMask = mask;
        if (pressed) {
            mask = static_cast<uint8_t>(mask | bit);
        } else {
            mask = static_cast<uint8_t>(mask & static_cast<uint8_t>(~bit));
        }
        queuedDigitalInputMask_.store(static_cast<int32_t>(mask), std::memory_order_release);
        if (mask != oldMask) {
            publishQueuedInputToService();
            ++stats_.buttonTransitions;
            appendLog(std::string("sdl: button ") + std::string(buttonName(button)) + (pressed ? " pressed" : " released"));
        }
    }

    void publishQueuedInputToService()
    {
        if (inputService_ == nullptr || !config_.enableInput) {
            return;
        }

        const auto generation = std::max<uint64_t>(inputService_->currentGeneration(), 1u);
        const auto v = queuedDigitalInputMask_.load(std::memory_order_acquire);
        if (v >= 0) {
            inputService_->publishDigitalSnapshot(
                static_cast<BMMQ::InputButtonMask>(static_cast<uint32_t>(v) & 0x00FFu),
                generation);
            return;
        }

        inputService_->applyNeutralFallback(generation);
    }

    void requestQuit()
    {
        quitRequested_ = true;
        ++stats_.quitRequests;
        appendLog("sdl: quit requested");
    }

    void syncAudioTransportStats() const noexcept
    {
        if (audioService_ == nullptr) {
            return;
        }
        const auto engineStats = audioService_->engine().stats();
        const auto transportStats = audioService_->transportStats();
        const auto outputDeviceInfo = audioOutput_ != nullptr ? audioOutput_->deviceInfo() : BMMQ::AudioOutputDeviceInfo{};
        stats_.audioSourceSampleRate = audioService_->engine().config().sourceSampleRate;
        stats_.audioDeviceSampleRate = audioService_->engine().config().deviceSampleRate;
        stats_.audioCallbackChunkSamples =
            outputDeviceInfo.callbackChunkSamples != 0u
                ? outputDeviceInfo.callbackChunkSamples
                : static_cast<std::size_t>(std::max(config_.audioCallbackChunkSamples, 1));
        stats_.audioRingBufferCapacitySamples = audioService_->engine().bufferCapacitySamples();
        stats_.audioBufferedHighWaterSamples = engineStats.bufferedHighWaterSamples;
        stats_.audioCallbackCount = engineStats.callbackCount;
        stats_.audioSamplesDelivered = engineStats.samplesDelivered;
        stats_.audioUnderrunCount = engineStats.underrunCount;
        stats_.audioSilenceSamplesFilled = engineStats.silenceSamplesFilled;
        stats_.audioOverrunDropCount = engineStats.overrunDropCount;
        stats_.audioDroppedSamples = engineStats.droppedSamples;
        stats_.audioResamplingActive = engineStats.resamplingActive;
        stats_.audioResampleRatio = engineStats.resampleRatio;
        stats_.audioSourceSamplesPushed = engineStats.sourceSamplesPushed;
        stats_.audioResampleSourceSamplesConsumed = engineStats.sourceSamplesConsumed;
        stats_.audioResampleOutputSamplesProduced = engineStats.outputSamplesProduced;
        stats_.audioPipelineCapacitySkipCount = engineStats.pipelineCapacitySkipCount;
        stats_.audioReadyQueueDepth = transportStats.readyQueueDepth;
        stats_.audioReadyQueueHighWaterChunks = transportStats.readyQueueHighWaterChunks;
        stats_.audioTransportDrainCallbackCount = transportStats.drainCallbackCount;
        stats_.audioTransportUnderrunCount = transportStats.underrunCount;
        stats_.audioTransportSilenceSamplesFilled = transportStats.silenceSamplesFilled;
        stats_.audioTransportWorkerProducedBlocks = transportStats.workerProducedBlocks;
        stats_.audioTransportDroppedReadyBlocks = transportStats.droppedReadyBlocks;
        stats_.audioTransportWorkerWakeCount = transportStats.workerWakeCount;
        stats_.audioTransportWorkerCallbackWakeCount = transportStats.workerCallbackWakeCount;
        stats_.audioTransportWorkerEmulationWakeCount = transportStats.workerEmulationWakeCount;
        stats_.audioTransportWorkerTimeoutWakeCount = transportStats.workerTimeoutWakeCount;
        stats_.audioTransportStaleEpochDropCount = transportStats.staleEpochDropCount;
        stats_.audioTransportEpochBumpCount = transportStats.epochBumpCount;
        stats_.audioTransportPrimedTransitionCount = transportStats.primedTransitionCount;
        stats_.audioTransportLifecycleEpoch = transportStats.lifecycleEpoch;
        stats_.audioTransportPrimedForDrain = transportStats.primedForDrain;
        stats_.audioTransportDrainDurationSampleCount = transportStats.drainCallbackDurationSampleCount;
        stats_.audioTransportDrainDurationLastNanos = transportStats.drainCallbackDurationLastNanos;
        stats_.audioTransportDrainDurationHighWaterNanos = transportStats.drainCallbackDurationHighWaterNanos;
        stats_.audioTransportDrainDurationP50Nanos = transportStats.drainCallbackDurationP50Nanos;
        stats_.audioTransportDrainDurationP95Nanos = transportStats.drainCallbackDurationP95Nanos;
        stats_.audioTransportDrainDurationP99Nanos = transportStats.drainCallbackDurationP99Nanos;
        stats_.audioTransportDrainDurationP999Nanos = transportStats.drainCallbackDurationP999Nanos;
        stats_.audioTransportDrainDurationUnder50usCount = transportStats.drainCallbackDurationUnder50usCount;
        stats_.audioTransportDrainDuration50To100usCount = transportStats.drainCallbackDuration50To100usCount;
        stats_.audioTransportDrainDuration100To250usCount = transportStats.drainCallbackDuration100To250usCount;
        stats_.audioTransportDrainDuration250To500usCount = transportStats.drainCallbackDuration250To500usCount;
        stats_.audioTransportDrainDuration500usTo1msCount = transportStats.drainCallbackDuration500usTo1msCount;
        stats_.audioTransportDrainDuration1To2msCount = transportStats.drainCallbackDuration1To2msCount;
        stats_.audioTransportDrainDuration2To5msCount = transportStats.drainCallbackDuration2To5msCount;
        stats_.audioTransportDrainDuration5To10msCount = transportStats.drainCallbackDuration5To10msCount;
        stats_.audioTransportDrainDurationOver10msCount = transportStats.drainCallbackDurationOver10msCount;
        stats_.audioTransportWorkerEmulationWakeLatencySampleCount =
            transportStats.workerEmulationWakeLatencySampleCount;
        stats_.audioTransportWorkerEmulationWakeLatencyLastNs =
            transportStats.workerEmulationWakeLatencyLastNs;
        stats_.audioTransportWorkerEmulationWakeLatencyHighWaterNs =
            transportStats.workerEmulationWakeLatencyHighWaterNs;
        stats_.audioTransportWorkerEmulationWakeLatencyUnder100usCount =
            transportStats.workerEmulationWakeLatencyUnder100usCount;
        stats_.audioTransportWorkerEmulationWakeLatency100To500usCount =
            transportStats.workerEmulationWakeLatency100To500usCount;
        stats_.audioTransportWorkerEmulationWakeLatency500usTo1msCount =
            transportStats.workerEmulationWakeLatency500usTo1msCount;
        stats_.audioTransportWorkerEmulationWakeLatency1To2msCount =
            transportStats.workerEmulationWakeLatency1To2msCount;
        stats_.audioTransportWorkerEmulationWakeLatency2To5msCount =
            transportStats.workerEmulationWakeLatency2To5msCount;
        stats_.audioTransportWorkerEmulationWakeLatency5To10msCount =
            transportStats.workerEmulationWakeLatency5To10msCount;
        stats_.audioTransportWorkerEmulationWakeLatency10To20msCount =
            transportStats.workerEmulationWakeLatency10To20msCount;
        stats_.audioTransportWorkerEmulationWakeLatencyOver20msCount =
            transportStats.workerEmulationWakeLatencyOver20msCount;
        stats_.lastQueuedAudioBytes = queuedAudioBytes();
        stats_.peakQueuedAudioBytes = std::max<std::uint32_t>(
            stats_.peakQueuedAudioBytes,
            static_cast<std::uint32_t>(stats_.audioBufferedHighWaterSamples * sizeof(int16_t)));
    }

    void syncTimingStats() noexcept
    {
        if (timingService_ == nullptr) {
            return;
        }
        const auto timingStats = timingService_->stats();
        stats_.timingCycleDebt = timingStats.cycleDebt;
        stats_.timingWakeBurstSamples = timingStats.wakeBurstSamples;
        stats_.timingWakeBurstSliceLimitHitCount = timingStats.wakeBurstSliceLimitHitCount;
        stats_.timingWakeBurstCycleLimitHitCount = timingStats.wakeBurstCycleLimitHitCount;
        stats_.timingWakeBurstSlicesLast = timingStats.wakeBurstSlicesLast;
        stats_.timingWakeBurstSlicesHighWater = timingStats.wakeBurstSlicesHighWater;
        stats_.timingWakeBurstCyclesLast = timingStats.wakeBurstCyclesLast;
        stats_.timingWakeBurstCyclesHighWater = timingStats.wakeBurstCyclesHighWater;
        stats_.timingSleepCalls = timingStats.sleepCalls;
        stats_.timingSleepWakeEarlyCount = timingStats.sleepWakeEarlyCount;
        stats_.timingSleepWakeLateCount = timingStats.sleepWakeLateCount;
        stats_.timingSleepWakeJitterUnder100usCount = timingStats.sleepWakeJitterUnder100usCount;
        stats_.timingSleepWakeJitter100To500usCount = timingStats.sleepWakeJitter100To500usCount;
        stats_.timingSleepWakeJitter500usTo2msCount = timingStats.sleepWakeJitter500usTo2msCount;
        stats_.timingSleepWakeJitterOver2msCount = timingStats.sleepWakeJitterOver2msCount;
        stats_.timingSleepWakeLateStreakCurrent = timingStats.sleepWakeLateStreakCurrent;
        stats_.timingSleepWakeLateStreakHighWater = timingStats.sleepWakeLateStreakHighWater;
        stats_.timingSleepOvershootCount = timingStats.sleepOvershootCount;
        stats_.timingSleepOvershootHighWaterNanos = timingStats.sleepOvershootHighWater.count();
        stats_.timingSleepOvershootLastNanos = timingStats.sleepOvershootLast.count();
        stats_.timingFrontendTicksScheduled = timingStats.frontendTicksScheduled;
        stats_.timingFrontendTicksExecuted = timingStats.frontendTicksExecuted;
        stats_.timingFrontendTicksMerged = timingStats.frontendTicksMerged;
        stats_.timingFrontendTickDelayLastNanos = timingStats.frontendTickDelayLast.count();
        stats_.timingFrontendTickDelayHighWaterNanos = timingStats.frontendTickDelayHighWater.count();
        stats_.timingProfileName = BMMQ::timingPolicyProfileName(timingStats.activeProfile);
    }

    [[nodiscard]] bool shouldDeferVideoFrameForAudioLowWater() const noexcept
    {
        if (!config_.enableAudio || audioService_ == nullptr || !audioOutputReady() || !lastFrame_.has_value()) {
            return false;
        }
        if (videoService_ != nullptr &&
            (videoService_->hasCompleteScanlineFrame() || videoService_->hasPartialScanlineFrame())) {
            return false;
        }

        const auto safetyMarginSamples = computeAudioSafetyMarginSamples();
        return audioService_->engine().bufferedSamples() < safetyMarginSamples;
    }

    std::optional<BMMQ::VideoDebugFrameModel> snapshotVideoDebugModel(const BMMQ::MachineView& view)
    {
        ++stats_.videoDebugSnapshotsBuilt;
        ++stats_.videoStateSnapshots;
        const auto t0 = std::chrono::steady_clock::now();
        auto result = view.videoDebugFrameModel({
            .frameWidth = std::max(config_.frameWidth, 1),
            .frameHeight = std::max(config_.frameHeight, 1),
        });
        const auto elapsedNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0).count());
        stats_.videoDebugSnapshotDurationLastNs = elapsedNs;
        stats_.videoDebugSnapshotDurationHighWaterNs =
            std::max(stats_.videoDebugSnapshotDurationHighWaterNs, elapsedNs);
        return result;
    }

    // Variant that accepts a model pre-built outside sharedStateMutex_ to
    // avoid paying the VDP traversal cost while the lock is held.
    std::optional<BMMQ::VideoDebugFrameModel> snapshotVideoDebugModel(
        std::optional<BMMQ::VideoDebugFrameModel> prebuilt, const BMMQ::MachineView& view)
    {
        if (prebuilt.has_value()) {
            ++stats_.videoDebugSnapshotsBuilt;
            ++stats_.videoStateSnapshots;
            // Duration was paid outside the lock; leave timing stats at zero for
            // this path to distinguish from fully-synchronous builds.
            return prebuilt;
        }
        return snapshotVideoDebugModel(view);
    }

    std::optional<BMMQ::AudioStateView> snapshotAudioState(const BMMQ::MachineView& view)
    {
        ++stats_.audioStateSnapshotsBuilt;
        const auto t0 = std::chrono::steady_clock::now();
        auto result = view.audioState();
        const auto elapsedNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0).count());
        stats_.audioStateSnapshotDurationLastNs = elapsedNs;
        stats_.audioStateSnapshotDurationHighWaterNs =
            std::max(stats_.audioStateSnapshotDurationHighWaterNs, elapsedNs);
        return result;
    }

    [[nodiscard]] bool trySubmitRealtimeVideoPacket(const BMMQ::MachineEvent& event,
                                                    const BMMQ::MachineView& view)
    {
        if (videoService_ == nullptr || event.type == BMMQ::MachineEventType::VideoScanlineReady) {
            ++stats_.videoRealtimePacketsSkipped;
            return false;
        }

        auto packet = view.realtimeVideoPacket({
            .frameWidth = std::max(config_.frameWidth, 1),
            .frameHeight = std::max(config_.frameHeight, 1),
        });
        if (!packet.has_value()) {
            ++stats_.videoRealtimePacketsSkipped;
            return false;
        }
        if (packet->contractVersion != BMMQ::RealtimeVideoPacket::kContractVersion) {
            ++stats_.videoRealtimePacketsSkipped;
            return false;
        }

        packet->eventType = event.type;
        packet->generation = videoService_->engine().currentGeneration();
        if (!videoService_->submitRealtimeVideoPacket(event, *packet)) {
            ++stats_.videoRealtimePacketsSkipped;
            return false;
        }

        ++stats_.videoRealtimePacketsAccepted;
        accumulateVdpRenderBodyTiming(packet->vdpRenderBodyTiming);
        accumulateVdpMode4BackgroundAttributes(packet->vdpMode4BackgroundAttributes);
        accumulateVdpMode4SimpleBackground(packet->vdpMode4SimpleBackground);
        updateLastFrameFromPacket(*packet);
        if (event.type == BMMQ::MachineEventType::VBlank) {
            lastVideoDebugModel_ = debugModelFromRealtimePacket(*packet);
            scanlineVideoDebugModel_.reset();
        }
        return true;
    }

    [[nodiscard]] static BMMQ::VideoDebugFrameModel debugModelFromRealtimePacket(const BMMQ::RealtimeVideoPacket& packet)
    {
        BMMQ::VideoDebugFrameModel model;
        model.width = packet.width;
        model.height = packet.height;
        model.displayEnabled = packet.displayEnabled;
        model.inVBlank = packet.inVBlank;
        model.scanlineIndex = packet.scanlineIndex;
        model.argbPixels = packet.argbPixels;
        model.semantics.resize(model.argbPixels.size());
        return model;
    }

    // trySubmitPrebuiltRealtimeVideoPacket: called while sharedStateMutex_ is held.
    // The packet was built OUTSIDE the lock; this function only deposits it and updates bookkeeping.
    [[nodiscard]] bool trySubmitPrebuiltRealtimeVideoPacket(const BMMQ::MachineEvent& event,
                                                            BMMQ::RealtimeVideoPacket packet)
    {
        if (videoService_ == nullptr || event.type == BMMQ::MachineEventType::VideoScanlineReady) {
            ++stats_.videoRealtimePacketsSkipped;
            return false;
        }
        if (packet.contractVersion != BMMQ::RealtimeVideoPacket::kContractVersion) {
            ++stats_.videoRealtimePacketsSkipped;
            return false;
        }

        packet.eventType = event.type;
        packet.generation = videoService_->engine().currentGeneration();
        if (!videoService_->submitRealtimeVideoPacket(event, packet)) {
            ++stats_.videoRealtimePacketsSkipped;
            return false;
        }

        ++stats_.videoRealtimePacketsAccepted;
        accumulateVdpRenderBodyTiming(packet.vdpRenderBodyTiming);
        accumulateVdpMode4BackgroundAttributes(packet.vdpMode4BackgroundAttributes);
        accumulateVdpMode4SimpleBackground(packet.vdpMode4SimpleBackground);
        updateLastFrameFromPacket(packet);
        if (event.type == BMMQ::MachineEventType::VBlank) {
            lastVideoDebugModel_ = debugModelFromRealtimePacket(packet);
            scanlineVideoDebugModel_.reset();
        }
        return true;
    }

    void accumulateVdpRenderBodyTiming(const BMMQ::RealtimeVideoPacket::VdpRenderBodyTiming& timing) noexcept
    {
        if (timing.totalNs == 0u) {
            return;
        }
        ++stats_.videoVdpRenderBodySampleCount;
        stats_.videoVdpRenderBodyTotalNs += timing.totalNs;
        stats_.videoVdpRenderBodySetupNs += timing.setupNs;
        stats_.videoVdpRenderBodyBackgroundNs += timing.backgroundNs;
        stats_.videoVdpRenderBodySpriteProbeNs += timing.spriteProbeNs;
        stats_.videoVdpRenderBodySpriteOverlayNs += timing.spriteOverlayNs;
        stats_.videoVdpRenderBodyOtherNs += timing.otherNs;
    }

    void accumulateVdpMode4BackgroundAttributes(
        const BMMQ::RealtimeVideoPacket::VdpMode4BackgroundAttributeStats& attrs) noexcept
    {
        if (attrs.tileCellsProcessed == 0u) {
            return;
        }
        stats_.videoVdpMode4AttrTileCellsProcessed += attrs.tileCellsProcessed;
        stats_.videoVdpMode4AttrTileCellsFlipH += attrs.tileCellsFlipH;
        stats_.videoVdpMode4AttrTileCellsFlipV += attrs.tileCellsFlipV;
        stats_.videoVdpMode4AttrTileCellsPalette1 += attrs.tileCellsPalette1;
        stats_.videoVdpMode4AttrTileCellsPriority += attrs.tileCellsPriority;
        stats_.videoVdpMode4AttrTileCellsFixedTopRows += attrs.tileCellsFixedTopRows;
        stats_.videoVdpMode4AttrTileCellsFixedRightColumns += attrs.tileCellsFixedRightColumns;
        stats_.videoVdpMode4AttrTileCellsLeftBlankOrFineSkip += attrs.tileCellsLeftBlankOrFineSkip;
        stats_.videoVdpMode4AttrTileCellsCommonCaseEligible += attrs.tileCellsCommonCaseEligible;
        stats_.videoVdpMode4AttrCommonCaseEligiblePixelsWritten += attrs.commonCaseEligiblePixelsWritten;
    }

    void accumulateVdpMode4SimpleBackground(
        const BMMQ::RealtimeVideoPacket::VdpMode4SimpleBackgroundStats& stats) noexcept
    {
        if (stats.simplePathFrameCount == 0u &&
            stats.mode4SimplePathUsedCount == 0u &&
            stats.mode4GeneralPathUsedCount == 0u &&
            stats.tmsGraphicsPathUsedCount == 0u) {
            return;
        }
        stats_.videoVdpMode4SimplePathFrameCount += stats.simplePathFrameCount;
        stats_.videoVdpMode4SimplePathRowsRendered += stats.simplePathRowsRendered;
        stats_.videoVdpMode4SimplePathPixelsWritten += stats.simplePathPixelsWritten;
        stats_.videoVdpMode4SimplePathTileEntriesDecoded += stats.simplePathTileEntriesDecoded;
        stats_.videoVdpMode4SimplePathPatternRowsDecoded += stats.simplePathPatternRowsDecoded;
        stats_.videoVdpMode4SimplePathScrollXAlignedCount += stats.simplePathScrollXAlignedCount;
        stats_.videoVdpMode4SimplePathScrollYValueChanges += stats.simplePathScrollYValueChanges;
        stats_.videoVdpMode4SimplePathUniqueTileRowsSeen += stats.simplePathUniqueTileRowsSeen;
        stats_.videoVdpMode4SimplePathUsedCount += stats.mode4SimplePathUsedCount;
        stats_.videoVdpMode4GeneralPathUsedCount += stats.mode4GeneralPathUsedCount;
        stats_.videoTmsGraphicsPathUsedCount += stats.tmsGraphicsPathUsedCount;
    }

    BMMQ::VideoDebugFrameModel* modelForVideoEvent(const BMMQ::MachineEvent& event,
                                                   const BMMQ::MachineView& view,
                                                   std::optional<BMMQ::VideoDebugFrameModel> prebuilt = {})
    {
        if (event.type == BMMQ::MachineEventType::VideoScanlineReady) {
            return modelForScanlineEvent(view, std::move(prebuilt));
        }

        if (event.type == BMMQ::MachineEventType::VBlank &&
            videoService_ != nullptr &&
            (videoService_->hasCompleteScanlineFrame() || videoService_->hasPartialScanlineFrame()) &&
            scanlineVideoDebugModel_.has_value()) {
            scanlineVideoDebugModel_ = snapshotVideoDebugModel(std::move(prebuilt), view);
            if (!scanlineVideoDebugModel_.has_value()) {
                return nullptr;
            }
            if (debugSnapshotService_ != nullptr) {
                (void)debugSnapshotService_->submitVideoModel(scanlineVideoDebugModel_);
            }
            return &*scanlineVideoDebugModel_;
        }

        scanlineVideoDebugModel_.reset();
        lastVideoDebugModel_ = snapshotVideoDebugModel(std::move(prebuilt), view);
        if (!lastVideoDebugModel_.has_value()) {
            return nullptr;
        }
        if (debugSnapshotService_ != nullptr) {
            (void)debugSnapshotService_->submitVideoModel(lastVideoDebugModel_);
        }
        return &*lastVideoDebugModel_;
    }

    BMMQ::VideoDebugFrameModel* modelForScanlineEvent(const BMMQ::MachineView& view,
                                                      std::optional<BMMQ::VideoDebugFrameModel> prebuilt = {})
    {
        scanlineVideoDebugModel_ = snapshotVideoDebugModel(std::move(prebuilt), view);
        if (!scanlineVideoDebugModel_.has_value()) {
            return nullptr;
        }
        if (debugSnapshotService_ != nullptr) {
            (void)debugSnapshotService_->submitVideoModel(scanlineVideoDebugModel_);
        }
        return &*scanlineVideoDebugModel_;
    }

    [[nodiscard]] bool configureVideoService(bool coordinatorOwned = true)
    {
        if (videoService_ == nullptr) {
            return false;
        }
        const auto configureAction = [this]() -> BMMQ::MachineTransitionMutationResult {
            const bool configured = videoService_->configure({
                .frameWidth = std::max(config_.frameWidth, 1),
                .frameHeight = std::max(config_.frameHeight, 1),
                .mailboxDepthFrames = 2,
            });
            videoService_->setPresenterPolicy(config_.videoPresenterPolicy);
            const bool presenterConfigured = configured && videoService_->configurePresenter({
                .windowTitle = config_.windowTitle,
                .scale = static_cast<int>(std::min<std::uint32_t>(
                    std::max(config_.windowScale, 1u),
                    static_cast<std::uint32_t>(std::numeric_limits<int>::max()))),
                .frameWidth = std::max(config_.frameWidth, 1),
                .frameHeight = std::max(config_.frameHeight, 1),
                .mode = presenterModeForPolicy(config_.videoPresenterPolicy),
                .createHiddenWindowOnOpen = config_.createHiddenWindowOnInitialize,
                .showWindowOnPresent = config_.showWindowOnPresent,
            });
            bool presenterAttached = true;
            if (config_.enableVideo) {
                auto presenter = std::make_unique<BMMQ::SdlVideoPresenter>();
                videoPresenter_ = presenter.get();
                videoPresenter_->requestWindowVisibility(
                    windowVisibilityRequested_.load(std::memory_order_acquire));
                presenterAttached = presenterConfigured && videoService_->attachPresenter(std::move(presenter));
                if (!presenterAttached) {
                    videoPresenter_ = nullptr;
                }
            }
            const bool videoReady = presenterConfigured && presenterAttached;
            return BMMQ::MachineTransitionMutationResult{
                .success = videoReady,
                .videoReady = videoReady,
                .audioReady = true,
            };
        };
        if (coordinatorOwned && lifecycleCoordinator_ != nullptr) {
            return lifecycleCoordinator_->transitionConfigReconfigure(configureAction);
        }
        return configureAction().success;
    }

    bool ensureVideoPresenter(bool coordinatorOwned = true)
    {
        if (!config_.enableVideo) {
            return true;
        }
        if (coordinatorOwned && lifecycleCoordinator_ != nullptr) {
            ++stats_.lifecycleVideoPresenterEnsureCoordinatorCount;
        } else {
            ++stats_.lifecycleVideoPresenterEnsureDirectCount;
        }
        if (videoService_ == nullptr) {
            lastBackendError_ = "Video service unavailable";
            appendLog("sdl: video service unavailable");
            return false;
        }
        if (videoPresenter_ == nullptr) {
            if (!configureVideoService(coordinatorOwned)) {
                lastBackendError_ = "Video service configure failed";
                appendLog("sdl: " + lastBackendError_);
                return false;
            }
        }
        const auto resumeAction = [this]() -> BMMQ::MachineTransitionMutationResult {
            const bool resumed = videoService_ != nullptr && videoService_->resume();
            return BMMQ::MachineTransitionMutationResult{
                .success = resumed,
                .videoReady = resumed,
                .audioReady = true,
            };
        };
        const bool resumed = coordinatorOwned && lifecycleCoordinator_ != nullptr
                                 ? lifecycleCoordinator_->transitionVideoBackendRestart(resumeAction)
                                 : resumeAction().success;
        if (!resumed) {
            lastBackendError_ = videoService_->diagnostics().lastBackendError;
            appendLog("sdl: video presenter open failed: " + lastBackendError_);
            return false;
        }
        return true;
    }

    void syncVideoTransportStats() noexcept
    {
        if (videoService_ == nullptr) {
            return;
        }
        const auto& diagnostics = videoService_->diagnostics();
        stats_.videoFramesPublished = diagnostics.publishedFrameCount;
        stats_.videoPresentCount = diagnostics.presentCount;
        stats_.videoPresentFallbackCount = diagnostics.presentFallbackCount;
        stats_.videoMailboxDepth = diagnostics.mailboxDepth;
        stats_.videoMailboxHighWaterFrames = diagnostics.mailboxHighWaterMark;
        stats_.videoMailboxStaleDropCount = diagnostics.staleFrameDropCount;
        stats_.videoMailboxStaleDebugDropCount = diagnostics.staleDebugFrameDropCount;
        stats_.videoMailboxStaleRealtimeDropCount = diagnostics.staleRealtimeFrameDropCount;
        stats_.videoMailboxOverwriteCount = diagnostics.overwriteCount;
        stats_.videoMailboxOverwriteDebugCount = diagnostics.overwriteDebugFrameCount;
        stats_.videoMailboxOverwriteRealtimeCount = diagnostics.overwriteRealtimeFrameCount;
        stats_.videoLastPublishedGeneration = diagnostics.lastPublishedGeneration;
        stats_.videoLastPresentedGeneration = diagnostics.lastPresentedGeneration;
        stats_.configuredPresenterMode = diagnostics.configuredPresenterMode;
        stats_.configuredPresenterPolicy = diagnostics.configuredPresenterPolicy;
        stats_.activePresenterMode = diagnostics.activePresenterMode;
        stats_.videoPresenterUsedSoftwareFallback = diagnostics.presenterUsedSoftwareFallback;
        stats_.videoPresenterSoftwareFallbackCount = diagnostics.presenterSoftwareFallbackCount;
        stats_.videoPresenterLastFallbackReason = diagnostics.presenterLastFallbackReason;
        stats_.videoPresenterTextureRecreateCount = diagnostics.presenterTextureRecreateCount;
        stats_.videoPresenterTextureUploadCount = diagnostics.presenterTextureUploadCount;
        stats_.videoPresenterRenderCount = diagnostics.presenterRenderCount;
        stats_.videoPresenterRendererName = diagnostics.presenterRendererName;
        stats_.videoPublishedDebugFrameCount = diagnostics.publishedDebugFrameCount;
        stats_.videoPublishedRealtimeFrameCount = diagnostics.publishedRealtimeFrameCount;
        stats_.videoPublishedDebugPixelBytes = diagnostics.publishedDebugPixelBytes;
        stats_.videoPublishedRealtimePixelBytes = diagnostics.publishedRealtimePixelBytes;
        stats_.videoPublishedPixelBytes = diagnostics.publishedPixelBytes;
        stats_.videoPresentFreshFrameCount = diagnostics.presentFromFreshFrameCount;
        stats_.videoPresentFromDebugFrameCount = diagnostics.presentFromDebugFrameCount;
        stats_.videoPresentFromRealtimeFrameCount = diagnostics.presentFromRealtimeFrameCount;
        stats_.videoPresentFallbackBlankCount = diagnostics.presentFallbackBlankCount;
        stats_.videoPresentFallbackLastValidCount = diagnostics.presentFallbackLastValidCount;
        stats_.videoPresentGenerationGap0 = diagnostics.presentGenerationGap0;
        stats_.videoPresentGenerationGap1 = diagnostics.presentGenerationGap1;
        stats_.videoPresentGenerationGap2To3 = diagnostics.presentGenerationGap2To3;
        stats_.videoPresentGenerationGap4Plus = diagnostics.presentGenerationGap4Plus;
        stats_.videoStaleEpochDropCount = diagnostics.staleEpochDropCount;
        stats_.videoLifecycleEpochBumpCount = diagnostics.lifecycleEpochBumpCount;
        stats_.videoLifecycleEpoch = diagnostics.lifecycleEpoch;
        stats_.videoFrameAgeLastNs = diagnostics.frameAgeLastNs;
        stats_.videoFrameAgeHighWaterNs = diagnostics.frameAgeHighWaterNs;
        stats_.videoFrameAgeUnder50usCount = diagnostics.frameAgeUnder50usCount;
        stats_.videoFrameAge50To100usCount = diagnostics.frameAge50To100usCount;
        stats_.videoFrameAge100To250usCount = diagnostics.frameAge100To250usCount;
        stats_.videoFrameAge250To500usCount = diagnostics.frameAge250To500usCount;
        stats_.videoFrameAge500usTo1msCount = diagnostics.frameAge500usTo1msCount;
        stats_.videoFrameAge1To2msCount = diagnostics.frameAge1To2msCount;
        stats_.videoFrameAge2To5msCount = diagnostics.frameAge2To5msCount;
        stats_.videoFrameAge5To10msCount = diagnostics.frameAge5To10msCount;
        stats_.videoFrameAgeOver10msCount = diagnostics.frameAgeOver10msCount;
        stats_.videoPresenterPresentDurationLastNanos = diagnostics.presenterPresentDurationLastNanos;
        stats_.videoPresenterPresentDurationHighWaterNanos = diagnostics.presenterPresentDurationHighWaterNanos;
        stats_.videoPresenterPresentDurationP50Nanos = diagnostics.presenterPresentDurationP50Nanos;
        stats_.videoPresenterPresentDurationP95Nanos = diagnostics.presenterPresentDurationP95Nanos;
        stats_.videoPresenterPresentDurationP99Nanos = diagnostics.presenterPresentDurationP99Nanos;
        stats_.videoPresenterPresentDurationP999Nanos = diagnostics.presenterPresentDurationP999Nanos;
        stats_.videoPresenterPresentDurationSampleCount = diagnostics.presenterPresentDurationSampleCount;
        stats_.videoPresenterPresentDurationUnder50usCount =
            diagnostics.presenterPresentDurationUnder50usCount;
        stats_.videoPresenterPresentDuration50To100usCount =
            diagnostics.presenterPresentDuration50To100usCount;
        stats_.videoPresenterPresentDuration100To250usCount =
            diagnostics.presenterPresentDuration100To250usCount;
        stats_.videoPresenterPresentDuration250To500usCount =
            diagnostics.presenterPresentDuration250To500usCount;
        stats_.videoPresenterPresentDuration500usTo1msCount =
            diagnostics.presenterPresentDuration500usTo1msCount;
        stats_.videoPresenterPresentDuration1To2msCount =
            diagnostics.presenterPresentDuration1To2msCount;
        stats_.videoPresenterPresentDuration2To5msCount =
            diagnostics.presenterPresentDuration2To5msCount;
        stats_.videoPresenterPresentDuration5To10msCount =
            diagnostics.presenterPresentDuration5To10msCount;
        stats_.videoPresenterPresentDurationOver10msCount =
            diagnostics.presenterPresentDurationOver10msCount;
        if (videoPresenter_ != nullptr) {
            windowVisible_.store(videoPresenter_->windowVisible(), std::memory_order_release);
            windowVisibilityRequested_.store(
                videoPresenter_->windowVisibilityRequested(), std::memory_order_release);
        }
    }

    // Lightweight sync: only reads the two presenter-window fields used by the present decision.
    // Called every render-service loop iteration instead of the full syncVideoTransportStats().
    // Caller must hold sharedStateMutex_.
    void syncPresentDecisionState() noexcept
    {
        if (videoPresenter_ != nullptr) {
            windowVisible_.store(videoPresenter_->windowVisible(), std::memory_order_release);
            windowVisibilityRequested_.store(
                videoPresenter_->windowVisibilityRequested(), std::memory_order_release);
        }
    }

    // Update lastFrame_ shadow copy from a successfully submitted RealtimeVideoPacket.
    // Called from trySubmitRealtimeVideoPacket / trySubmitPrebuiltRealtimeVideoPacket
    // while sharedStateMutex_ is held, so the per-iteration engine poll in
    // syncVideoTransportStats() is no longer needed.
    void updateLastFrameFromPacket(const BMMQ::RealtimeVideoPacket& packet) noexcept
    {
        if (videoService_ == nullptr) {
            return;
        }
        BMMQ::SdlFrameBuffer compatFrame;
        compatFrame.width = packet.width;
        compatFrame.height = packet.height;
        compatFrame.generation = packet.generation;
        compatFrame.pixels = packet.argbPixels;
        lastFrame_ = std::move(compatFrame);
        lastSyncedVideoFramePublication_ = videoService_->engine().stats().publishedFrameCount;
    }

    void applyWindowVisibilityRequest() noexcept
    {
        if (videoPresenter_ != nullptr) {
            const bool requested = windowVisibilityRequested_.load(std::memory_order_acquire);
            videoPresenter_->requestWindowVisibility(requested);
            windowVisible_.store(videoPresenter_->windowVisible(), std::memory_order_release);
            return;
        }
        windowVisible_.store(windowVisibilityRequested_.load(std::memory_order_acquire),
                             std::memory_order_release);
    }

    void shutdownBackend() noexcept
    {
        stopRenderService();
        std::scoped_lock<std::mutex> lock(sharedStateMutex_);
        const auto shutdownAction = [this]() -> BMMQ::MachineTransitionMutationResult {
            BMMQ::MachineTransitionMutationResult result;
            if (audioService_ != nullptr) {
                audioService_->setBackendPausedOrClosed(true);
            }
            if (videoService_ != nullptr) {
                (void)videoService_->pause();
                (void)videoService_->detachPresenter();
                videoPresenter_ = nullptr;
            }
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
            if (audioOutput_ != nullptr) {
                audioOutput_->close();
            }
            if (initializedBackendFlags_ != 0u) {
                SDL_QuitSubSystem(initializedBackendFlags_);
                initializedBackendFlags_ = 0u;
            }
#endif
            if (audioService_ != nullptr && audioService_->canPerformReset()) {
                (void)audioService_->resetStats();
                (void)audioService_->resetStream();
            }
            backendReady_ = false;
            stats_.renderServiceState = renderServiceState_.load(std::memory_order_acquire);
            return result;
        };
        if (lifecycleCoordinator_ != nullptr) {
            (void)lifecycleCoordinator_->transitionConfigReconfigure(shutdownAction);
        } else {
            (void)shutdownAction();
        }
    }

    [[nodiscard]] bool hasAudibleChannelOneState(const BMMQ::AudioStateView& state) const noexcept
    {
        if (!state.soundEnabled()) {
            return false;
        }
        if (state.hasPcmSamples()) {
            constexpr std::size_t kAudibleProbeWindow = 64u;
            const auto hasNonZeroSample = [](auto begin, auto end) {
                return std::any_of(begin, end, [](int16_t sample) {
                    return sample != 0;
                });
            };

            const auto windowSize = std::min<std::size_t>(state.pcmSamples.size(), kAudibleProbeWindow);
            if (hasNonZeroSample(state.pcmSamples.begin(), state.pcmSamples.begin() + static_cast<std::ptrdiff_t>(windowSize))) {
                return true;
            }
            if (state.pcmSamples.size() > windowSize) {
                return hasNonZeroSample(state.pcmSamples.end() - static_cast<std::ptrdiff_t>(windowSize),
                                        state.pcmSamples.end());
            }
            return false;
        }
        if ((state.nr52 & 0x01u) == 0u && (state.nr14 & 0x80u) == 0u) {
            return false;
        }
        if ((state.nr50 & 0x77u) == 0u) {
            return false;
        }
        if ((state.nr51 & 0x11u) == 0u) {
            return false;
        }
        return ((state.nr12 >> 4) & 0x0Fu) != 0u;
    }

    bool ensureAudioDevice(bool coordinatorOwned = true)
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (!config_.enableAudio || audioOutputReady()) {
            return true;
        }

        constexpr int kSourceAudioSampleRate = 48000;
        if (audioService_ == nullptr) {
            lastBackendError_ = "Audio service unavailable";
            appendLog("sdl: audio service unavailable");
            return false;
        }
        const auto openAction = [this, kSourceAudioSampleRate]() -> BMMQ::MachineTransitionMutationResult {
            const auto sourceChannelCount = std::max<uint8_t>(audioService_->engine().config().channelCount, 1u);
            const auto sourceChannelMultiplier = static_cast<std::size_t>(sourceChannelCount);
            audioService_->setBackendPausedOrClosed(true);
            if (!audioService_->configureEngine({
                .sourceSampleRate = kSourceAudioSampleRate,
                .deviceSampleRate = kSourceAudioSampleRate,
                .channelCount = sourceChannelCount,
                .ringBufferCapacitySamples = config_.audioRingBufferCapacitySamples * sourceChannelMultiplier,
                .frameChunkSamples = kApuFrameSamples * sourceChannelMultiplier,
            })) {
                lastBackendError_ = "Audio engine configure rejected while backend active";
                appendLog("sdl: " + lastBackendError_);
                return BMMQ::MachineTransitionMutationResult{
                    .success = false,
                    .videoReady = true,
                    .audioReady = false,
                };
            }

            const auto normalizedBackend = normalizeAudioBackend(config_.audioBackend);
            if (audioOutput_ == nullptr || selectedAudioBackend_ != normalizedBackend) {
                if (audioOutput_ != nullptr) {
                    audioOutput_->close();
                }
                auto replacement = makeAudioOutputBackend(normalizedBackend);
                audioOutput_ = std::move(replacement);
                if (audioOutput_ != nullptr) {
                    selectedAudioBackend_ = normalizedBackend;
                }
            }
            if (audioOutput_ == nullptr) {
                lastBackendError_ = "Unsupported audio backend '" + config_.audioBackend + "'";
                appendLog("sdl: " + lastBackendError_);
                return BMMQ::MachineTransitionMutationResult{
                    .success = false,
                    .videoReady = true,
                    .audioReady = false,
                };
            }

            if (!audioOutput_->open(audioService_->engine(), {
                    .backend = selectedAudioBackend_,
                    .requestedSampleRate = kSourceAudioSampleRate,
                    .callbackChunkSamples = static_cast<std::size_t>(std::max(config_.audioCallbackChunkSamples, 1)) *
                                            sourceChannelMultiplier,
                    .channels = sourceChannelCount,
                    .testForcedDeviceSampleRate = config_.enableAudioResamplingDiagnostics
                                                    ? config_.testForcedAudioDeviceSampleRate
                                                    : 0,
                    .filePath = config_.audioOutputFilePath,
                    .appendToFile = config_.audioFileAppend,
                    .audioService = audioService_,
                })) {
                lastBackendError_ = std::string(audioOutput_->lastError());
                appendLog("sdl: audio device open failed: " + lastBackendError_);
                return BMMQ::MachineTransitionMutationResult{
                    .success = false,
                    .videoReady = true,
                    .audioReady = false,
                };
            }

            audioService_->setBackendPausedOrClosed(false);
            syncAudioTransportStats();
            appendLog("sdl: audio device opened at " + std::to_string(audioService_->engine().config().deviceSampleRate) + " Hz");
            return BMMQ::MachineTransitionMutationResult{
                .success = true,
                .videoReady = true,
                .audioReady = true,
            };
        };
        return coordinatorOwned && lifecycleCoordinator_ != nullptr
                   ? lifecycleCoordinator_->transitionAudioBackendRestart(openAction)
                   : openAction().success;
#else
        return false;
#endif
    }

    [[nodiscard]] static std::string normalizeAudioBackend(std::string backend)
    {
        if (backend.empty()) {
            return "sdl";
        }
        std::transform(backend.begin(), backend.end(), backend.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return backend;
    }

    [[nodiscard]] static std::unique_ptr<BMMQ::IAudioOutputBackend> makeAudioOutputBackend(const std::string& backend)
    {
        if (backend == "sdl") {
            return std::make_unique<BMMQ::SdlAudioOutputBackend>();
        }
        if (backend == "file") {
            return std::make_unique<BMMQ::FileAudioOutputBackend>();
        }
        if (backend == "dummy") {
            return std::make_unique<BMMQ::DummyAudioOutputBackend>();
        }
        return nullptr;
    }

    [[nodiscard]] BMMQ::SdlAudioPreviewBuffer buildAudioPreview(const BMMQ::AudioStateView& state)
    {
        BMMQ::SdlAudioPreviewBuffer preview;
        const int defaultSampleRate = audioService_ != nullptr ? audioService_->engine().config().deviceSampleRate : 48000;
        preview.sampleRate = state.sampleRate != 0u ? static_cast<int>(state.sampleRate) : defaultSampleRate;
        const auto sampleCount = std::max(config_.audioPreviewSampleCount, 8);
        preview.samples.resize(static_cast<std::size_t>(sampleCount), 0);

        if (state.hasPcmSamples()) {
            const auto copyCount = std::min<std::size_t>(preview.samples.size(), state.pcmSamples.size());
            const auto start = state.pcmSamples.size() - copyCount;
            std::copy_n(state.pcmSamples.begin() + static_cast<std::ptrdiff_t>(start),
                        copyCount,
                        preview.samples.begin());
            return preview;
        }

        if (!hasAudibleChannelOneState(state)) {
            return preview;
        }

        const int initialVolume = static_cast<int>((state.nr12 >> 4) & 0x0Fu);
        const int leftVolume = static_cast<int>((state.nr50 >> 4) & 0x07u) + 1;
        const int rightVolume = static_cast<int>(state.nr50 & 0x07u) + 1;
        const int mixedVolume = std::max(1, (leftVolume + rightVolume) / 2);
        const int amplitude = std::clamp(initialVolume * mixedVolume * 192, 0, 28000);

        const uint16_t frequencyBits = static_cast<uint16_t>(state.nr13)
                                     | static_cast<uint16_t>((state.nr14 & 0x07u) << 8u);
        const int divisor = std::max(1, 2048 - static_cast<int>(frequencyBits));
        const double frequencyHz = std::clamp(131072.0 / static_cast<double>(divisor), 32.0, 4096.0);

        double dutyCycle = 0.5;
        switch ((state.nr11 >> 6) & 0x03u) {
        case 0x00u:
            dutyCycle = 0.125;
            break;
        case 0x01u:
            dutyCycle = 0.25;
            break;
        case 0x02u:
            dutyCycle = 0.5;
            break;
        case 0x03u:
            dutyCycle = 0.75;
            break;
        }

        double phase = audioPhase_;
        const double phaseStep = std::clamp(frequencyHz / static_cast<double>(std::max(preview.sampleRate, 1)),
                                            1.0 / 4096.0,
                                            0.45);
        for (auto& sample : preview.samples) {
            sample = static_cast<int16_t>(phase < dutyCycle ? amplitude : -amplitude);
            phase += phaseStep;
            while (phase >= 1.0) {
                phase -= 1.0;
            }
        }
        audioPhase_ = phase;
        return preview;
    }

    [[nodiscard]] BMMQ::SdlAudioPreviewBuffer buildAudioPreview(const BMMQ::RealtimeAudioPacket& packet) const
    {
        BMMQ::SdlAudioPreviewBuffer preview;
        const int defaultSampleRate = audioService_ != nullptr ? audioService_->engine().config().deviceSampleRate : 48000;
        preview.sampleRate = packet.sampleRate != 0u ? static_cast<int>(packet.sampleRate) : defaultSampleRate;
        preview.channels = static_cast<int>(std::max<std::uint8_t>(packet.channelCount, 1u));
        const auto sampleCount = std::max(config_.audioPreviewSampleCount, 8);
        preview.samples.resize(static_cast<std::size_t>(sampleCount), 0);
        if (!packet.pcmSamples.empty()) {
            const auto copyCount = std::min<std::size_t>(preview.samples.size(), packet.pcmSamples.size());
            const auto start = packet.pcmSamples.size() - copyCount;
            std::copy_n(packet.pcmSamples.begin() + static_cast<std::ptrdiff_t>(start),
                        copyCount,
                        preview.samples.begin());
        }
        return preview;
    }

    BMMQ::SdlFrontendConfig config_;
    mutable BMMQ::SdlFrontendStats stats_;
    bool backendReady_ = false;
    std::optional<BMMQ::VideoDebugFrameModel> lastVideoDebugModel_;
    std::optional<BMMQ::VideoDebugFrameModel> scanlineVideoDebugModel_;
    std::optional<BMMQ::AudioStateView> lastAudioState_;
    std::optional<BMMQ::SdlAudioPreviewBuffer> lastAudioPreview_;
    std::optional<BMMQ::DigitalInputStateView> lastInputState_;
    std::optional<BMMQ::SdlFrameBuffer> lastFrame_;
    std::size_t lastSyncedVideoFramePublication_ = 0;
    bool frameDirty_ = false;
    // std::atomic<bool>: written under sharedStateMutex_ on multiple paths but read
    // lock-free inside the renderServiceWaitMutex_ condvar block (line ~1104) to
    // decide the sleep interval. Atomicity prevents the TSAN race caught in Phase 36B.
    std::atomic<bool> videoPresentDeferredForAudioLowWater_{false};
    // Atomic: -1 = no active mask (nullopt); 0-255 = active 8-bit button mask.
    // Written by render/SDL thread; read locklessly by emulation thread in
    // sampleDigitalInput(). The sentinel value -1 encodes std::nullopt semantics.
    std::atomic<int32_t> queuedDigitalInputMask_{-1};
    // Input sampling shadow atomics (Phase 34B).
    std::atomic<uint64_t> inputPollsAtomic_{0};
    std::atomic<uint64_t> inputSamplesProvidedAtomic_{0};
    // Event-pump shadow atomics (Phase 35B).
    // collectBackendEvents() increments eventPumpCallsAtomic_ and
    // backendEventsTranslatedAtomic_ lock-free on the render thread.
    // pumpBackendEvents() (headless/slow path) also increments these
    // via the stats fold in stats() rather than direct atomic writes,
    // keeping that slow path unchanged.
    std::atomic<uint64_t> eventPumpCallsAtomic_{0};
    std::atomic<uint64_t> backendEventsTranslatedAtomic_{0};
    std::atomic<uint64_t> renderServiceEventPumpCountAtomic_{0};
    // Phase 36A: counts VBlank/scanline frames whose render-service notification
    // (renderServiceFramePending_ store + condvar notify) was sent after
    // sharedStateMutex_ was released, reducing VBlank lock hold time.
    std::atomic<uint64_t> onVideoEventFrameNotifyOutsideLockCountAtomic_{0};
    std::atomic<uint64_t> videoDebugModelBuildSkipCountAtomic_{0};
    // Phase 36B render-service wait-block shadow atomics.
    // These are incremented inside the renderServiceWaitMutex_ condvar block (or
    // just before it, under no lock), so they cannot be written directly to stats_
    // without racing against stats() which reads stats_ under sharedStateMutex_.
    // stats() folds them in just before the return copy.
    std::atomic<uint64_t> renderServiceSleepCountAtomic_{0};
    std::atomic<uint64_t> renderServiceDeferredPresentFastSleepCountAtomic_{0};
    std::atomic<uint64_t> renderServiceFrameWakeCountAtomic_{0};
    std::atomic<uint64_t> renderServiceTimeoutWakeCountAtomic_{0};
    std::atomic<uint64_t> renderServiceSleepOvershootCountAtomic_{0};
    bool quitRequested_ = false;
    std::atomic<bool> windowVisible_{false};
    std::atomic<bool> windowVisibilityRequested_{false};
    std::string lastHostEventSummary_;
    std::string lastRenderSummary_;
    std::string lastBackendError_;
    uint32_t initializedBackendFlags_ = 0;
    double audioPhase_ = 0.0;
    uint64_t audioPreviewGeneration_ = 0;
    BMMQ::AudioService* audioService_ = nullptr;
    BMMQ::InputService* inputService_ = nullptr;
    BMMQ::VideoService* videoService_ = nullptr;
    BMMQ::TimingService* timingService_ = nullptr;
    BMMQ::MachineLifecycleCoordinator* lifecycleCoordinator_ = nullptr;
    BMMQ::SdlVideoPresenter* videoPresenter_ = nullptr;
    BMMQ::DebugSnapshotService* debugSnapshotService_ = nullptr;
    std::unique_ptr<BMMQ::IAudioOutputBackend> audioOutput_ = std::make_unique<BMMQ::SdlAudioOutputBackend>();
    std::string selectedAudioBackend_ = "sdl";
    mutable std::mutex sharedStateMutex_;
    std::atomic<BMMQ::SdlRenderServiceState> renderServiceState_{BMMQ::SdlRenderServiceState::Stopped};
    std::atomic<bool> renderServiceStopRequested_{false};
    std::atomic<bool> renderServiceFramePending_{false};
    std::thread renderServiceThread_{};
    std::mutex renderServiceWaitMutex_{};
    std::condition_variable renderServiceWakeCv_{};
    std::size_t lastLifecycleRecoveryAttemptServiceCall_ = 0;
    static constexpr std::size_t kLifecycleRecoveryCooldownCalls = 64u;
};

BMMQ::ISdlFrontendPlugin* createSdlFrontendPlugin(const BMMQ::SdlFrontendConfig* config)
{
    return new SdlFrontendPluginImpl(config != nullptr ? *config : BMMQ::SdlFrontendConfig{});
}

void destroySdlFrontendPlugin(BMMQ::ISdlFrontendPlugin* plugin) noexcept
{
    delete plugin;
}

const BMMQ::SdlFrontendPluginApiV1 kSdlFrontendPluginApi{
    sizeof(BMMQ::SdlFrontendPluginApiV1),
    BMMQ::kSdlFrontendPluginApiVersion,
    &createSdlFrontendPlugin,
    &destroySdlFrontendPlugin,
};

} // namespace

extern "C" const BMMQ::SdlFrontendPluginApiV1* bmmq_get_sdl_frontend_plugin_api_v1()
{
    return &kSdlFrontendPluginApi;
}
