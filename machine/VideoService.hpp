#ifndef BMMQ_VIDEO_SERVICE_HPP
#define BMMQ_VIDEO_SERVICE_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "BackgroundTaskService.hpp"
#include "VisualDebugAdapter.hpp"
#include "VisualOverrideService.hpp"
#include "plugins/IoPlugin.hpp"
#include "plugins/video/RealtimeVideoMailbox.hpp"
#include "plugins/video/VideoEngine.hpp"
#include "plugins/video/VideoPlugin.hpp"

namespace BMMQ {

enum class VideoLifecycleState {
    Detached = 0,
    Paused,
    Active,
    Headless,
    Faulted,
};

struct VideoServiceDiagnostics {
    std::size_t presentFailureCount = 0;
    std::size_t compatibilityFallbackCount = 0;
    std::size_t presentFallbackCount = 0;
    std::size_t publishedFrameCount = 0;
    std::size_t presentCount = 0;
    std::size_t presentFromFreshFrameCount = 0;
    std::size_t staleFrameDropCount = 0;
    std::size_t staleDebugFrameDropCount = 0;
    std::size_t staleRealtimeFrameDropCount = 0;
    std::size_t overwriteCount = 0;
    std::size_t consumeCount = 0;
    std::size_t overwriteDebugFrameCount = 0;
    std::size_t overwriteRealtimeFrameCount = 0;
    bool headlessModeActive = true;
    std::string lastBackendError;
    std::size_t mailboxDepth = 0;
    std::size_t mailboxHighWaterMark = 0;
    VideoPresenterMode configuredPresenterMode = VideoPresenterMode::Auto;
    VideoPresenterPolicy configuredPresenterPolicy = VideoPresenterPolicy::HardwarePreferredWithFallback;
    VideoPresenterMode activePresenterMode = VideoPresenterMode::Auto;
    bool presenterUsedSoftwareFallback = false;
    std::size_t presenterSoftwareFallbackCount = 0;
    VideoPresenterFallbackReason presenterLastFallbackReason = VideoPresenterFallbackReason::None;
    std::size_t presenterTextureRecreateCount = 0;
    std::size_t presenterTextureUploadCount = 0;
    std::size_t presenterRenderCount = 0;
    std::string presenterRendererName;
    std::string simdBackendName;
    std::size_t presenterDirectIndexedFrameCount = 0;
    std::size_t presenterArgbFrameCount = 0;
    std::size_t presenterTextureLockCount = 0;
    std::uint32_t presenterRendererFlags = 0;
    bool presenterRendererAccelerated = false;
    bool presenterRenderTargetSupported = false;
    std::int64_t presenterExpansionDurationLastNanos = 0;
    std::int64_t presenterExpansionDurationHighWaterNanos = 0;
    std::int64_t presenterUploadDurationLastNanos = 0;
    std::int64_t presenterUploadDurationHighWaterNanos = 0;
    std::int64_t presenterRenderSubmitDurationLastNanos = 0;
    std::int64_t presenterRenderSubmitDurationHighWaterNanos = 0;
    std::int64_t presenterTotalDurationLastNanos = 0;
    std::int64_t presenterTotalDurationHighWaterNanos = 0;
    std::size_t presenterStageDurationSampleCount = 0;
    std::size_t publishedDebugFrameCount = 0;
    std::size_t publishedRealtimeFrameCount = 0;
    std::size_t publishedDebugPixelBytes = 0;
    std::size_t publishedRealtimePixelBytes = 0;
    std::size_t publishedPixelBytes = 0;
    std::size_t presentFromDebugFrameCount = 0;
    std::size_t presentFromRealtimeFrameCount = 0;
    std::size_t presentFallbackBlankCount = 0;
    std::size_t presentFallbackLastValidCount = 0;
    std::size_t presentGenerationGap0 = 0;
    std::size_t presentGenerationGap1 = 0;
    std::size_t presentGenerationGap2To3 = 0;
    std::size_t presentGenerationGap4Plus = 0;
    std::size_t staleEpochDropCount = 0;
    std::size_t lifecycleEpochBumpCount = 0;
    uint64_t lifecycleEpoch = 1;
    uint64_t lastPublishedGeneration = 0;
    uint64_t lastPresentedGeneration = 0;
    std::string activeBackendName;
    VideoLifecycleState state = VideoLifecycleState::Headless;
    std::uint64_t frameAgeLastNs = 0;
    std::uint64_t frameAgeHighWaterNs = 0;
    std::size_t frameAgeUnder50usCount = 0;
    std::size_t frameAge50To100usCount = 0;
    std::size_t frameAge100To250usCount = 0;
    std::size_t frameAge250To500usCount = 0;
    std::size_t frameAge500usTo1msCount = 0;
    std::size_t frameAge1To2msCount = 0;
    std::size_t frameAge2To5msCount = 0;
    std::size_t frameAge5To10msCount = 0;
    std::size_t frameAgeOver10msCount = 0;
    
    // Phase 39A: presenter present() latency percentiles (nanoseconds)
    std::int64_t presenterPresentDurationLastNanos = 0;
    std::int64_t presenterPresentDurationHighWaterNanos = 0;
    std::int64_t presenterPresentDurationP50Nanos = 0;
    std::int64_t presenterPresentDurationP95Nanos = 0;
    std::int64_t presenterPresentDurationP99Nanos = 0;
    std::int64_t presenterPresentDurationP999Nanos = 0;
    std::size_t presenterPresentDurationSampleCount = 0;
    
    // Duration histogram buckets (mirrors audio callback structure)
    std::size_t presenterPresentDurationUnder50usCount = 0;
    std::size_t presenterPresentDuration50To100usCount = 0;
    std::size_t presenterPresentDuration100To250usCount = 0;
    std::size_t presenterPresentDuration250To500usCount = 0;
    std::size_t presenterPresentDuration500usTo1msCount = 0;
    std::size_t presenterPresentDuration1To2msCount = 0;
    std::size_t presenterPresentDuration2To5msCount = 0;
    std::size_t presenterPresentDuration5To10msCount = 0;
    std::size_t presenterPresentDurationOver10msCount = 0;
    std::size_t buildDebugFrameCallCount = 0;
    std::uint64_t buildDebugFrameTotalNs = 0;
    std::size_t buildDebugFrameRealtimeReasonCount = 0;
    std::size_t buildDebugFrameDebugReasonCount = 0;
    std::size_t buildDebugFrameFallbackReasonCount = 0;
    std::size_t buildDebugFrameUnknownReasonCount = 0;
    std::size_t buildDebugFrameDebugConsumerActiveCount = 0;
    std::size_t buildDebugFrameDebugConsumerInactiveCount = 0;
    std::size_t visualOverrideLookupSampleCount = 0;
    std::uint64_t visualOverrideLookupTotalNs = 0;
    std::uint64_t visualOverrideLookupHighWaterNs = 0;
    std::size_t visualOverrideApplySampleCount = 0;
    std::uint64_t visualOverrideApplyTotalNs = 0;
    std::uint64_t visualOverrideApplyHighWaterNs = 0;
    std::size_t videoDebugFrameBuildSkippedNoConsumerCount = 0;
    std::size_t videoDebugFrameBuildExecutedCount = 0;
    std::size_t captureBackgroundSubmitCount = 0;
    std::size_t captureBackgroundFallbackCount = 0;
};

class VideoService {
public:
    VideoService() = default;

    explicit VideoService(VideoEngineConfig config)
        : engine_(std::move(config))
    {
        presenterConfig_.frameWidth = engine_.config().frameWidth;
        presenterConfig_.frameHeight = engine_.config().frameHeight;
    }

    [[nodiscard]] VideoEngine& engine() noexcept
    {
        return engine_;
    }

    [[nodiscard]] const VideoEngine& engine() const noexcept
    {
        return engine_;
    }

    void setVisualOverrideService(VisualOverrideService* service) noexcept
    {
        visualOverrideService_ = service;
        engine_.setVisualOverrideService(service);
    }

    void setBackgroundTaskService(BackgroundTaskService* service) noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        backgroundTaskService_ = service;
    }

    void setVisualDebugAdapter(const IVisualDebugAdapter* adapter) noexcept
    {
        visualDebugAdapter_ = adapter;
    }

    [[nodiscard]] const IVisualDebugAdapter* visualDebugAdapter() const noexcept
    {
        return visualDebugAdapter_;
    }

    [[nodiscard]] VideoLifecycleState state() const noexcept
    {
        return state_;
    }

    // Thread-safe diagnostics snapshot. Callers do not need to hold
    // external serialization; this method acquires the service mutex.
    [[nodiscard]] VideoServiceDiagnostics diagnostics() const
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        syncEngineDiagnostics();
        return diagnostics_;
    }

    [[nodiscard]] bool configure(const VideoEngineConfig& config)
    {
        if (!allowLifecycleMutation()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!configurationAllowed()) {
            return false;
        }
        engine_.configure(config);
        realtimeMailbox_.resetQuiescent(true);
        realtimeGeneration_.store(engine_.currentGeneration(), std::memory_order_release);
        bumpLifecycleEpochLocked();
        resetScanlineCapture();
        presenterConfig_.frameWidth = engine_.config().frameWidth;
        presenterConfig_.frameHeight = engine_.config().frameHeight;
        syncEngineDiagnostics();
        return true;
    }

    [[nodiscard]] bool configurePresenter(const VideoPresenterConfig& config)
    {
        if (!allowLifecycleMutation()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!configurationAllowed()) {
            return false;
        }
        presenterConfig_ = config;
        presenterConfig_.frameWidth = std::max(presenterConfig_.frameWidth, 1);
        presenterConfig_.frameHeight = std::max(presenterConfig_.frameHeight, 1);
        diagnostics_.configuredPresenterMode = presenterConfig_.mode;
        bumpLifecycleEpochLocked();
        return true;
    }

    void setPresenterPolicy(VideoPresenterPolicy policy) noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        presenterPolicy_ = policy;
        diagnostics_.configuredPresenterPolicy = policy;
    }

    [[nodiscard]] VideoPresenterPolicy presenterPolicy() const noexcept
    {
        return presenterPolicy_;
    }

    [[nodiscard]] bool attachPresenter(std::unique_ptr<IVideoPresenterPlugin> presenter)
    {
        if (!allowLifecycleMutation()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!configurationAllowed() || presenter == nullptr) {
            return false;
        }
        if (presenter_ != nullptr) {
            presenter_->close();
        }
        bumpLifecycleEpochLocked();
        presenter_ = std::move(presenter);
        diagnostics_.activeBackendName = std::string(presenter_->name());
        diagnostics_.lastBackendError.clear();
        setState(VideoLifecycleState::Paused);
        return true;
    }

    [[nodiscard]] bool detachPresenter()
    {
        if (!allowLifecycleMutation()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (presenter_ != nullptr) {
            presenter_->close();
            presenter_.reset();
        }
        bumpLifecycleEpochLocked();
        diagnostics_.activeBackendName.clear();
        diagnostics_.lastBackendError.clear();
        setState(VideoLifecycleState::Headless);
        return true;
    }

    [[nodiscard]] bool resume()
    {
        if (!allowLifecycleMutation()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (presenter_ == nullptr) {
            setState(VideoLifecycleState::Headless);
            return true;
        }
        if (!presenter_->ready() && !presenter_->open(presenterConfig_)) {
            diagnostics_.lastBackendError = std::string(presenter_->lastError());
            setState(VideoLifecycleState::Faulted);
            return false;
        }
        diagnostics_.activeBackendName = std::string(presenter_->name());
        diagnostics_.lastBackendError.clear();
        bumpLifecycleEpochLocked();
        setState(VideoLifecycleState::Active);
        return true;
    }

    [[nodiscard]] bool pause()
    {
        if (!allowLifecycleMutation()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (presenter_ != nullptr) {
            presenter_->close();
            bumpLifecycleEpochLocked();
            setState(VideoLifecycleState::Paused);
        } else {
            setState(VideoLifecycleState::Headless);
        }
        return true;
    }

    void setBackendActiveForTesting(bool active) noexcept
    {
        if (!allowLifecycleMutation()) {
            return;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        setState(active ? VideoLifecycleState::Active : VideoLifecycleState::Paused);
    }

    void setLifecycleContractEnforced(bool enforced) noexcept
    {
        enforceLifecycleContract_.store(enforced, std::memory_order_release);
    }

    void beginLifecycleMutationScope() noexcept
    {
        lifecycleMutationScopeDepth_.fetch_add(1u, std::memory_order_acq_rel);
    }

    void endLifecycleMutationScope() noexcept
    {
        const auto depth = lifecycleMutationScopeDepth_.load(std::memory_order_acquire);
        if (depth != 0u) {
            lifecycleMutationScopeDepth_.fetch_sub(1u, std::memory_order_acq_rel);
        }
    }

    [[nodiscard]] std::size_t lifecycleContractDeniedCalls() const noexcept
    {
        return lifecycleContractDeniedCalls_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool addProcessor(std::unique_ptr<IVideoFrameProcessorPlugin> processor)
    {
        if (processor == nullptr) {
            return false;
        }
        const auto caps = processor->capabilities();
        if (!isLiveCompatible(caps)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (state_ == VideoLifecycleState::Active && !caps.hotSwappable) {
            return false;
        }
        processors_.push_back(std::move(processor));
        return true;
    }

    [[nodiscard]] bool addCapture(std::unique_ptr<IVideoCapturePlugin> capture)
    {
        if (capture == nullptr) {
            return false;
        }
        const auto caps = capture->capabilities();
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        const bool inlineSafe = caps.realtimeSafe && caps.deterministic && !caps.nonRealtimeOnly;
        const bool backgroundSafe =
            backgroundTaskService_ != nullptr && caps.deterministic && !caps.requiresHostThreadAffinity;
        if (!inlineSafe && !backgroundSafe) {
            return false;
        }
        captures_.push_back(CaptureRegistration{
            std::shared_ptr<IVideoCapturePlugin>(std::move(capture)),
            !inlineSafe,
        });
        return true;
    }

    [[nodiscard]] bool submitVideoDebugModel(const MachineEvent& event,
                                             const VideoDebugFrameModel& model,
                                             bool debugConsumerActive = false)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (event.type == MachineEventType::RomLoaded) {
            engine_.advanceGeneration();
            bumpLifecycleEpochLocked();
            resetScanlineCapture();
            return true;
        }

        if (event.type == MachineEventType::VideoScanlineReady) {
            if (!debugConsumerActive) {
                ++videoDebugFrameBuildSkippedNoConsumerCount_;
                resetScanlineCapture();
                syncEngineDiagnostics();
                return false;
            }
            captureScanline(event, model, debugConsumerActive);
            syncEngineDiagnostics();
            return false;
        }

        if (!debugConsumerActive) {
            ++videoDebugFrameBuildSkippedNoConsumerCount_;
            resetScanlineCapture();
            syncEngineDiagnostics();
            return false;
        }

        const bool shouldBuildFrame =
            event.type == MachineEventType::VBlank ||
            engine_.lastValidFrame().has_value() == false ||
            !model.displayEnabled ||
            event.type == MachineEventType::MemoryWriteObserved;
        if (!shouldBuildFrame) {
            syncEngineDiagnostics();
            return false;
        }

        const auto generation = engine_.currentGeneration();
        if (event.type == MachineEventType::VBlank && hasCompleteScanlineFrameLocked()) {
            auto frame = *scanlineFrame_;
            resetScanlineCapture();
            return submitFrame(frame);
        }
        if (event.type == MachineEventType::VBlank && hasPartialScanlineFrameLocked()) {
            ++videoDebugFrameBuildExecutedCount_;
            auto frame = engine_.buildDebugFrame(model,
                                                 generation,
                                                 VideoEngine::BuildDebugFrameReason::Realtime,
                                                 debugConsumerActive);
            overlayCapturedScanlines(frame);
            resetScanlineCapture();
            return submitFrame(frame);
        }

        resetScanlineCapture();
        ++videoDebugFrameBuildExecutedCount_;
        return submitFrame(engine_.buildDebugFrame(model,
                                                   generation,
                                                   VideoEngine::BuildDebugFrameReason::Debug,
                                                   debugConsumerActive));
    }

    [[nodiscard]] bool submitRealtimeVideoPacket(const MachineEvent& event,
                                                 RealtimeVideoSubmission submission)
    {
        if (event.type == MachineEventType::RomLoaded) {
            std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
            engine_.advanceGeneration();
            realtimeGeneration_.store(engine_.currentGeneration(), std::memory_order_release);
            realtimeMailbox_.resetQuiescent(false);
            bumpLifecycleEpochLocked();
            resetScanlineCapture();
            return true;
        }
        if (event.type != MachineEventType::VBlank) {
            return false;
        }
        return publishRealtimeVideoPacket(std::move(submission));
    }

    // Emulation-lane steady-state publication. This function intentionally
    // performs only validation, scalar metadata assignment, vector moves, and
    // atomic mailbox operations. Lifecycle/configuration code remains on the
    // non-real-time mutex-protected control plane.
    [[nodiscard]] bool publishRealtimeVideoPacket(RealtimeVideoSubmission submission) noexcept
    {
        auto& packet = submission.packet;
        if (packet.eventType != MachineEventType::VBlank || packet.empty() ||
            packet.contractVersion != RealtimeVideoPacket::kContractVersion) {
            return false;
        }
        if (packet.generation == 0u) {
            packet.generation = realtimeGeneration_.load(std::memory_order_acquire);
        }
        return realtimeMailbox_.publish(
            std::move(submission),
            realtimeLifecycleEpoch_.load(std::memory_order_acquire));
    }

    [[nodiscard]] bool hasPendingRealtimeFrame() const noexcept
    {
        return realtimeMailbox_.hasPending();
    }

    // Render-lane diagnostics handoff. The compact pixel payload is deliberately
    // omitted; timing metadata is consumed under the frontend's render lock.
    [[nodiscard]] std::optional<RealtimeVideoDiagnostics> takeConsumedRealtimeMetadata() noexcept
    {
        auto metadata = std::move(lastConsumedRealtimeMetadata_);
        lastConsumedRealtimeMetadata_.reset();
        return metadata;
    }

    [[nodiscard]] bool submitFrame(const VideoFramePacket& frame)
    {
        auto adjusted = frame;
        adjusted.lifecycleEpoch = lifecycleEpoch_;
        const auto submitResult = engine_.submitFrame(adjusted);
        syncEngineDiagnostics();
        return submitResult.accepted;
    }

    // consumeAndProcessFrame: consume the next frame from the engine, run the
    // processor pipeline, and return the processed packet ready for presentation.
    // Returns std::nullopt when in Headless mode (no presenter — caller should
    // treat this as a successful headless present and skip the backend call).
    // The sentinel VideoFramePacket with width==0 is used to signal Headless.
    // Caller must hold sharedStateMutex_ (or equivalent serialisation) for all
    // VideoService/VideoEngine state while this runs.
    [[nodiscard]] std::optional<VideoFramePacket> consumeAndProcessFrame()
    {
        std::optional<VideoPresentPacket> frame{};
        while (auto published = realtimeMailbox_.tryConsumeLatest()) {
            if (published->lifecycleEpoch !=
                realtimeLifecycleEpoch_.load(std::memory_order_acquire)) {
                ++diagnostics_.staleEpochDropCount;
                continue;
            }
            lastConsumedRealtimeMetadata_ = std::move(published->diagnostics);
            VideoPresentPacket present;
            present.width = published->packet.width;
            present.height = published->packet.height;
            present.source = VideoFrameSource::RealtimeSnapshot;
            present.generation = published->packet.generation;
            present.lifecycleEpoch = published->lifecycleEpoch;
            present.publishedAtNs = published->publishedAtNs;
            present.surface = std::move(published->packet.surface);
            frame = std::move(present);
            break;
        }
        if (!frame.has_value()) {
            while (true) {
                frame = engine_.tryConsumeLatestFrame();
                if (!frame.has_value()) {
                    break;
                }
                if (frame->lifecycleEpoch == lifecycleEpoch_) {
                    break;
                }
                ++diagnostics_.staleEpochDropCount;
            }
        }
        bool usedFallback = false;
        if (!frame.has_value()) {
            if (lastProcessedFrame_.has_value()) {
                frame = makePresentPacket(*lastProcessedFrame_);
                frame->source = VideoFrameSource::LastValidFallback;
            } else {
                frame = engine_.fallbackFrame();
            }
            frame->lifecycleEpoch = lifecycleEpoch_;
            ++diagnostics_.presentFallbackCount;
            if (frame->source == VideoFrameSource::BlankFallback) {
                ++diagnostics_.presentFallbackBlankCount;
            } else {
                ++diagnostics_.presentFallbackLastValidCount;
            }
            usedFallback = true;
        } else {
            ++diagnostics_.presentFromFreshFrameCount;
            if (frame->source == VideoFrameSource::RealtimeSnapshot) {
                ++diagnostics_.presentFromRealtimeFrameCount;
            } else {
                ++diagnostics_.presentFromDebugFrameCount;
            }
        }

        VideoFramePacket processed = makeFramePacket(std::move(*frame));
        const bool presenterAcceptsIndexed = presenter_ != nullptr &&
            presenter_->capabilities().acceptsIndexedSurface;
        const bool requiresArgb = !processed.surface.validForDimensions(processed.width, processed.height) ||
            !presenterAcceptsIndexed || !processors_.empty() || !captures_.empty();
        if (requiresArgb && !materializeVideoFrameArgb(processed)) {
            ++diagnostics_.compatibilityFallbackCount;
            return std::nullopt;
        }
        for (auto& processor : processors_) {
            if (!isLiveCompatible(processor->capabilities())) {
                ++diagnostics_.compatibilityFallbackCount;
                continue;
            }
            VideoFramePacket output;
            if (!processor->process(processed, output) ||
                output.width != processed.width ||
                output.height != processed.height ||
                output.pixelCount() != processed.pixelCount()) {
                ++diagnostics_.compatibilityFallbackCount;
                continue;
            }
            processed = std::move(output);
        }

        dispatchCaptures(processed);

        if (!usedFallback) {
            lastProcessedFrame_ = processed;
        }

        syncEngineDiagnostics();
        diagnostics_.lastPresentedGeneration = processed.generation;
        const auto publishedGeneration = diagnostics_.lastPublishedGeneration;
        if (publishedGeneration >= processed.generation) {
            const auto generationGap = static_cast<std::size_t>(publishedGeneration - processed.generation);
            if (generationGap == 0u) {
                ++diagnostics_.presentGenerationGap0;
            } else if (generationGap == 1u) {
                ++diagnostics_.presentGenerationGap1;
            } else if (generationGap <= 3u) {
                ++diagnostics_.presentGenerationGap2To3;
            } else {
                ++diagnostics_.presentGenerationGap4Plus;
            }
        } else if (!usedFallback) {
            ++diagnostics_.presentGenerationGap0;
        }
        ++diagnostics_.presentCount;
        if (presenter_ == nullptr) {
            setState(VideoLifecycleState::Headless);
            return std::nullopt;  // headless: no backend call needed; caller treats as success
        }
        return processed;
    }

    // Headless/test host path: drain and reconstruct the latest frame even when
    // no presenter is attached. Production renderers use consumeAndProcessFrame
    // directly; this compatibility accessor keeps frontend inspection useful
    // without moving reconstruction back onto the emulation lane.
    [[nodiscard]] std::optional<VideoFramePacket> consumeHeadlessFrame()
    {
        if (auto processed = consumeAndProcessFrame()) {
            (void)materializeVideoFrameArgb(*processed);
            return processed;
        }
        auto fallback = lastProcessedFrame_;
        if (fallback.has_value()) {
            (void)materializeVideoFrameArgb(*fallback);
        }
        return fallback;
    }

    // recordPresentOutcome: update diagnostics and lifecycle state after an
    // outside-lock present call. Must be called under sharedStateMutex_.
    void recordPresentOutcome(bool ok, std::string_view error)
    {
        if (ok) {
            diagnostics_.lastBackendError.clear();
        } else {
            diagnostics_.lastBackendError = std::string(error);
            ++diagnostics_.presentFailureCount;
            setState(VideoLifecycleState::Faulted);
        }
    }

    [[nodiscard]] bool presentOneFrame()
    {
        auto processed = consumeAndProcessFrame();
        if (!processed.has_value()) {
            // Headless: consumeAndProcessFrame already set state; treat as success.
            return true;
        }
        if (!presenter_->ready()) {
            recordPresentOutcome(false, "presenter not ready");
            return false;
        }
        if (!presenter_->present(*processed)) {
            recordPresentOutcome(false, presenter_->lastError());
            return false;
        }
        recordPresentOutcome(true, {});
        return true;
    }

    void advanceGeneration() noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        engine_.advanceGeneration();
        realtimeGeneration_.store(engine_.currentGeneration(), std::memory_order_release);
        realtimeMailbox_.resetQuiescent(false);
        bumpLifecycleEpochLocked();
        resetScanlineCapture();
        syncEngineDiagnostics();
    }

    [[nodiscard]] bool hasCompleteScanlineFrame() const noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        return hasCompleteScanlineFrameLocked();
    }

    [[nodiscard]] bool hasPartialScanlineFrame() const noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        return hasPartialScanlineFrameLocked();
    }

    void bumpLifecycleEpochBarrier() noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        bumpLifecycleEpochLocked();
    }

private:
    [[nodiscard]] bool allowLifecycleMutation() const noexcept
    {
        if (!enforceLifecycleContract_.load(std::memory_order_acquire)) {
            return true;
        }
        if (lifecycleMutationScopeDepth_.load(std::memory_order_acquire) != 0u) {
            return true;
        }
        lifecycleContractDeniedCalls_.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }

    [[nodiscard]] bool configurationAllowed() const noexcept
    {
        return state_ == VideoLifecycleState::Detached ||
               state_ == VideoLifecycleState::Paused ||
               state_ == VideoLifecycleState::Headless ||
               state_ == VideoLifecycleState::Faulted;
    }

    [[nodiscard]] static bool isLiveCompatible(const VideoPluginCapabilities& caps) noexcept
    {
        return caps.realtimeSafe &&
               caps.frameSizePreserving &&
               caps.deterministic &&
               !caps.nonRealtimeOnly &&
               !caps.requiresHostThreadAffinity;
    }

    void setState(VideoLifecycleState state) noexcept
    {
        state_ = state;
        diagnostics_.state = state_;
        diagnostics_.headlessModeActive = state_ == VideoLifecycleState::Headless || presenter_ == nullptr;
    }

    void syncEngineDiagnostics() const noexcept
    {
        const auto engineStats = engine_.stats();
        const auto realtimeStats = realtimeMailbox_.stats();
        diagnostics_.publishedFrameCount =
            engineStats.publishedFrameCount + realtimeStats.publishedFrameCount;
        diagnostics_.staleFrameDropCount = engineStats.staleFrameDropCount;
        diagnostics_.staleDebugFrameDropCount = engineStats.staleDebugFrameDropCount;
        diagnostics_.staleRealtimeFrameDropCount = engineStats.staleRealtimeFrameDropCount;
        diagnostics_.overwriteCount = engineStats.overwriteCount + realtimeStats.overwriteCount;
        diagnostics_.consumeCount =
            engineStats.consumedFrameCount + realtimeStats.consumedFrameCount;
        diagnostics_.overwriteDebugFrameCount = engineStats.overwriteDebugFrameCount;
        diagnostics_.overwriteRealtimeFrameCount =
            engineStats.overwriteRealtimeFrameCount + realtimeStats.overwriteCount;
        diagnostics_.mailboxDepth = engineStats.mailboxDepth + realtimeStats.mailboxDepth;
        diagnostics_.mailboxHighWaterMark = std::max(
            engineStats.mailboxHighWaterMark, realtimeStats.mailboxHighWaterMark);
        diagnostics_.publishedDebugFrameCount = engineStats.publishedDebugFrameCount;
        diagnostics_.publishedRealtimeFrameCount =
            engineStats.publishedRealtimeFrameCount + realtimeStats.publishedFrameCount;
        diagnostics_.publishedDebugPixelBytes = engineStats.publishedDebugPixelBytes;
        diagnostics_.publishedRealtimePixelBytes =
            engineStats.publishedRealtimePixelBytes + realtimeStats.publishedPixelBytes;
        diagnostics_.publishedPixelBytes =
            engineStats.publishedPixelBytes + realtimeStats.publishedPixelBytes;
        diagnostics_.lastPublishedGeneration = std::max(
            engineStats.lastPublishedGeneration, realtimeStats.lastPublishedGeneration);
        diagnostics_.lifecycleEpoch = lifecycleEpoch_;
        diagnostics_.lifecycleEpochBumpCount = lifecycleEpochBumpCount_;
        diagnostics_.configuredPresenterMode = presenterConfig_.mode;
        diagnostics_.configuredPresenterPolicy = presenterPolicy_;
        diagnostics_.simdBackendName = SimdPixelOps::active_backend_name();
        if (presenter_ != nullptr) {
            const auto presenterDiagnostics = presenter_->diagnostics();
            diagnostics_.activePresenterMode = presenterDiagnostics.activeMode;
            diagnostics_.presenterUsedSoftwareFallback = presenterDiagnostics.usedSoftwareFallback;
            diagnostics_.presenterSoftwareFallbackCount = presenterDiagnostics.softwareFallbackCount;
            diagnostics_.presenterLastFallbackReason = presenterDiagnostics.lastFallbackReason;
            diagnostics_.presenterTextureRecreateCount = presenterDiagnostics.textureRecreateCount;
            diagnostics_.presenterTextureUploadCount = presenterDiagnostics.textureUploadCount;
            diagnostics_.presenterRenderCount = presenterDiagnostics.presentCount;
            diagnostics_.presenterRendererName = std::string(presenterDiagnostics.rendererName);
            diagnostics_.presenterDirectIndexedFrameCount = presenterDiagnostics.directIndexedFrameCount;
            diagnostics_.presenterArgbFrameCount = presenterDiagnostics.argbFrameCount;
            diagnostics_.presenterTextureLockCount = presenterDiagnostics.textureLockCount;
            diagnostics_.presenterRendererFlags = presenterDiagnostics.rendererFlags;
            diagnostics_.presenterRendererAccelerated = presenterDiagnostics.rendererAccelerated;
            diagnostics_.presenterRenderTargetSupported = presenterDiagnostics.renderTargetSupported;
            diagnostics_.presenterExpansionDurationLastNanos = presenterDiagnostics.expansionDurationLastNanos;
            diagnostics_.presenterExpansionDurationHighWaterNanos = presenterDiagnostics.expansionDurationHighWaterNanos;
            diagnostics_.presenterUploadDurationLastNanos = presenterDiagnostics.uploadDurationLastNanos;
            diagnostics_.presenterUploadDurationHighWaterNanos = presenterDiagnostics.uploadDurationHighWaterNanos;
            diagnostics_.presenterRenderSubmitDurationLastNanos = presenterDiagnostics.renderSubmitDurationLastNanos;
            diagnostics_.presenterRenderSubmitDurationHighWaterNanos = presenterDiagnostics.renderSubmitDurationHighWaterNanos;
            diagnostics_.presenterTotalDurationLastNanos = presenterDiagnostics.totalDurationLastNanos;
            diagnostics_.presenterTotalDurationHighWaterNanos = presenterDiagnostics.totalDurationHighWaterNanos;
            diagnostics_.presenterStageDurationSampleCount = presenterDiagnostics.stageDurationSampleCount;
            
                    // Phase 39A: copy presenter timing metrics
                    diagnostics_.presenterPresentDurationLastNanos = presenterDiagnostics.presenterPresentDurationLastNanos;
                    diagnostics_.presenterPresentDurationHighWaterNanos = presenterDiagnostics.presenterPresentDurationHighWaterNanos;
                    diagnostics_.presenterPresentDurationP50Nanos = presenterDiagnostics.presenterPresentDurationP50Nanos;
                    diagnostics_.presenterPresentDurationP95Nanos = presenterDiagnostics.presenterPresentDurationP95Nanos;
                    diagnostics_.presenterPresentDurationP99Nanos = presenterDiagnostics.presenterPresentDurationP99Nanos;
                    diagnostics_.presenterPresentDurationP999Nanos = presenterDiagnostics.presenterPresentDurationP999Nanos;
                    diagnostics_.presenterPresentDurationSampleCount = presenterDiagnostics.presenterPresentDurationSampleCount;
                    diagnostics_.presenterPresentDurationUnder50usCount = presenterDiagnostics.presenterPresentDurationUnder50usCount;
                    diagnostics_.presenterPresentDuration50To100usCount = presenterDiagnostics.presenterPresentDuration50To100usCount;
                    diagnostics_.presenterPresentDuration100To250usCount = presenterDiagnostics.presenterPresentDuration100To250usCount;
                    diagnostics_.presenterPresentDuration250To500usCount = presenterDiagnostics.presenterPresentDuration250To500usCount;
                    diagnostics_.presenterPresentDuration500usTo1msCount = presenterDiagnostics.presenterPresentDuration500usTo1msCount;
                    diagnostics_.presenterPresentDuration1To2msCount = presenterDiagnostics.presenterPresentDuration1To2msCount;
                    diagnostics_.presenterPresentDuration2To5msCount = presenterDiagnostics.presenterPresentDuration2To5msCount;
                    diagnostics_.presenterPresentDuration5To10msCount = presenterDiagnostics.presenterPresentDuration5To10msCount;
                    diagnostics_.presenterPresentDurationOver10msCount = presenterDiagnostics.presenterPresentDurationOver10msCount;
        }
        diagnostics_.state = state_;
        diagnostics_.headlessModeActive = state_ == VideoLifecycleState::Headless || presenter_ == nullptr;
        diagnostics_.frameAgeLastNs = realtimeStats.consumedFrameCount != 0u
            ? realtimeStats.frameAgeLastNs : engineStats.frameAgeLastNs;
        diagnostics_.frameAgeHighWaterNs = std::max(
            engineStats.frameAgeHighWaterNs, realtimeStats.frameAgeHighWaterNs);
        diagnostics_.frameAgeUnder50usCount =
            engineStats.frameAgeUnder50usCount + realtimeStats.frameAgeUnder50usCount;
        diagnostics_.frameAge50To100usCount =
            engineStats.frameAge50To100usCount + realtimeStats.frameAge50To100usCount;
        diagnostics_.frameAge100To250usCount =
            engineStats.frameAge100To250usCount + realtimeStats.frameAge100To250usCount;
        diagnostics_.frameAge250To500usCount =
            engineStats.frameAge250To500usCount + realtimeStats.frameAge250To500usCount;
        diagnostics_.frameAge500usTo1msCount =
            engineStats.frameAge500usTo1msCount + realtimeStats.frameAge500usTo1msCount;
        diagnostics_.frameAge1To2msCount =
            engineStats.frameAge1To2msCount + realtimeStats.frameAge1To2msCount;
        diagnostics_.frameAge2To5msCount =
            engineStats.frameAge2To5msCount + realtimeStats.frameAge2To5msCount;
        diagnostics_.frameAge5To10msCount =
            engineStats.frameAge5To10msCount + realtimeStats.frameAge5To10msCount;
        diagnostics_.frameAgeOver10msCount =
            engineStats.frameAgeOver10msCount + realtimeStats.frameAgeOver10msCount;
        diagnostics_.buildDebugFrameCallCount = engineStats.buildDebugFrameCallCount;
        diagnostics_.buildDebugFrameTotalNs = engineStats.buildDebugFrameTotalNs;
        diagnostics_.buildDebugFrameRealtimeReasonCount = engineStats.buildDebugFrameRealtimeReasonCount;
        diagnostics_.buildDebugFrameDebugReasonCount = engineStats.buildDebugFrameDebugReasonCount;
        diagnostics_.buildDebugFrameFallbackReasonCount = engineStats.buildDebugFrameFallbackReasonCount;
        diagnostics_.buildDebugFrameUnknownReasonCount = engineStats.buildDebugFrameUnknownReasonCount;
        diagnostics_.buildDebugFrameDebugConsumerActiveCount = engineStats.buildDebugFrameDebugConsumerActiveCount;
        diagnostics_.buildDebugFrameDebugConsumerInactiveCount =
            engineStats.buildDebugFrameDebugConsumerInactiveCount;
        diagnostics_.visualOverrideLookupSampleCount = engineStats.visualOverrideLookupSampleCount;
        diagnostics_.visualOverrideLookupTotalNs = engineStats.visualOverrideLookupTotalNs;
        diagnostics_.visualOverrideLookupHighWaterNs = engineStats.visualOverrideLookupHighWaterNs;
        diagnostics_.visualOverrideApplySampleCount = engineStats.visualOverrideApplySampleCount;
        diagnostics_.visualOverrideApplyTotalNs = engineStats.visualOverrideApplyTotalNs;
        diagnostics_.visualOverrideApplyHighWaterNs = engineStats.visualOverrideApplyHighWaterNs;
        diagnostics_.videoDebugFrameBuildSkippedNoConsumerCount =
            videoDebugFrameBuildSkippedNoConsumerCount_;
        diagnostics_.videoDebugFrameBuildExecutedCount = videoDebugFrameBuildExecutedCount_;
    }

    void dispatchCaptures(const VideoFramePacket& frame)
    {
        if (captures_.empty()) {
            return;
        }

        if (backgroundTaskService_ != nullptr) {
            auto captures = captures_;
            auto captureDispatchMutex = captureDispatchMutex_;
            auto frameCopy = frame;
            const bool queued = backgroundTaskService_->submit(BackgroundJobCategory::VideoCapture, [
                captures = std::move(captures),
                captureDispatchMutex = std::move(captureDispatchMutex),
                frameCopy = std::move(frameCopy)]() mutable {
                    std::lock_guard<std::mutex> lock(*captureDispatchMutex);
                    for (const auto& capture : captures) {
                        (void)capture.plugin->capture(frameCopy);
                    }
                });
            if (queued) {
                ++diagnostics_.captureBackgroundSubmitCount;
                return;
            }
            ++diagnostics_.captureBackgroundFallbackCount;
            return;
        }

        std::lock_guard<std::mutex> lock(*captureDispatchMutex_);
        for (const auto& capture : captures_) {
            if (!capture.backgroundOnly) {
                (void)capture.plugin->capture(frame);
            }
        }
    }

    [[nodiscard]] bool hasCompleteScanlineFrameLocked() const noexcept
    {
        return scanlineFrame_.has_value() &&
               scanlineCaptureCount_ >= static_cast<std::size_t>(engine_.config().frameHeight);
    }

    [[nodiscard]] bool hasPartialScanlineFrameLocked() const noexcept
    {
        return scanlineFrame_.has_value() && scanlineCaptureCount_ != 0u;
    }

    void resetScanlineCapture()
    {
        scanlineFrame_.reset();
        scanlinesCaptured_.clear();
        scanlineCaptureCount_ = 0;
    }

    void captureScanline(const MachineEvent& event,
                         const VideoDebugFrameModel& model,
                         bool debugConsumerActive = false)
    {
        if (!model.displayEnabled || model.inVBlank || !model.scanlineIndex.has_value()) {
            resetScanlineCapture();
            return;
        }

        const int screenY = static_cast<int>(event.value);
        if (screenY < 0 || screenY >= engine_.config().frameHeight) {
            return;
        }

        ensureScanlineFrame();
        if (!scanlineFrame_.has_value() || scanlinesCaptured_.empty()) {
            return;
        }

        const auto sourceFrame = engine_.buildDebugFrame(model,
                                                         engine_.currentGeneration(),
                                                         VideoEngine::BuildDebugFrameReason::Debug,
                                                         debugConsumerActive);
        ++videoDebugFrameBuildExecutedCount_;
        if (sourceFrame.width != scanlineFrame_->width || sourceFrame.height != scanlineFrame_->height) {
            return;
        }
        const auto rowStart = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(sourceFrame.width);
        const auto rowEnd = rowStart + static_cast<std::size_t>(sourceFrame.width);
        if (rowEnd > sourceFrame.pixels.size() || rowEnd > scanlineFrame_->pixels.size()) {
            return;
        }
        std::copy(sourceFrame.pixels.begin() + static_cast<std::ptrdiff_t>(rowStart),
                  sourceFrame.pixels.begin() + static_cast<std::ptrdiff_t>(rowEnd),
                  scanlineFrame_->pixels.begin() + static_cast<std::ptrdiff_t>(rowStart));
        const auto lineIndex = static_cast<std::size_t>(screenY);
        if (lineIndex < scanlinesCaptured_.size() && !scanlinesCaptured_[lineIndex]) {
            scanlinesCaptured_[lineIndex] = true;
            ++scanlineCaptureCount_;
        }
    }

    void ensureScanlineFrame()
    {
        const auto width = engine_.config().frameWidth;
        const auto height = engine_.config().frameHeight;
        const auto generation = engine_.currentGeneration();
        if (scanlineFrame_.has_value() &&
            scanlineFrame_->width == width &&
            scanlineFrame_->height == height &&
            scanlineFrame_->generation == generation &&
            scanlinesCaptured_.size() == static_cast<std::size_t>(height)) {
            return;
        }

        scanlineFrame_ = makeBlankVideoFrame(width, height, generation);
        scanlineFrame_->source = VideoFrameSource::MachineSnapshot;
        scanlineFrame_->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0xFFE0F8D0u);
        scanlinesCaptured_.assign(static_cast<std::size_t>(height), false);
        scanlineCaptureCount_ = 0;
    }

    void overlayCapturedScanlines(VideoFramePacket& frame) const
    {
        if (!scanlineFrame_.has_value() ||
            scanlineFrame_->width != frame.width ||
            scanlineFrame_->height != frame.height ||
            scanlineFrame_->pixelCount() != frame.pixelCount()) {
            return;
        }

        const auto width = static_cast<std::size_t>(frame.width);
        for (std::size_t y = 0; y < scanlinesCaptured_.size(); ++y) {
            if (!scanlinesCaptured_[y]) {
                continue;
            }
            const auto lineStart = y * width;
            std::copy_n(scanlineFrame_->pixels.begin() + static_cast<std::ptrdiff_t>(lineStart),
                        width,
                        frame.pixels.begin() + static_cast<std::ptrdiff_t>(lineStart));
        }
    }

    void bumpLifecycleEpochLocked() noexcept
    {
        ++lifecycleEpoch_;
        realtimeLifecycleEpoch_.store(lifecycleEpoch_, std::memory_order_release);
        ++lifecycleEpochBumpCount_;
        lastConsumedRealtimeMetadata_.reset();
        lastProcessedFrame_.reset();
    }

    struct CaptureRegistration {
        std::shared_ptr<IVideoCapturePlugin> plugin;
        bool backgroundOnly = false;
    };

    VideoEngine engine_{};
    RealtimeVideoMailbox realtimeMailbox_{};
    VisualOverrideService* visualOverrideService_ = nullptr;
    BackgroundTaskService* backgroundTaskService_ = nullptr;
    const IVisualDebugAdapter* visualDebugAdapter_ = nullptr;
    VideoPresenterConfig presenterConfig_{};
    std::unique_ptr<IVideoPresenterPlugin> presenter_{};
    std::vector<std::unique_ptr<IVideoFrameProcessorPlugin>> processors_{};
    std::vector<CaptureRegistration> captures_{};
    std::shared_ptr<std::mutex> captureDispatchMutex_ = std::make_shared<std::mutex>();
    mutable VideoServiceDiagnostics diagnostics_{};
    VideoLifecycleState state_ = VideoLifecycleState::Headless;
    mutable std::mutex nonRealTimeMutex_;
    std::optional<VideoFramePacket> scanlineFrame_{};
    std::vector<bool> scanlinesCaptured_{};
    std::size_t scanlineCaptureCount_ = 0;
    uint64_t lifecycleEpoch_ = 1;
    std::atomic<uint64_t> realtimeLifecycleEpoch_{1u};
    std::atomic<uint64_t> realtimeGeneration_{0u};
    std::size_t lifecycleEpochBumpCount_ = 0;
    VideoPresenterPolicy presenterPolicy_ = VideoPresenterPolicy::HardwarePreferredWithFallback;
    std::atomic<bool> enforceLifecycleContract_{false};
    std::atomic<std::size_t> lifecycleMutationScopeDepth_{0};
    mutable std::atomic<std::size_t> lifecycleContractDeniedCalls_{0};
    std::size_t videoDebugFrameBuildSkippedNoConsumerCount_ = 0;
    std::size_t videoDebugFrameBuildExecutedCount_ = 0;
    std::optional<RealtimeVideoDiagnostics> lastConsumedRealtimeMetadata_{};
    std::optional<VideoFramePacket> lastProcessedFrame_{};
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_SERVICE_HPP
