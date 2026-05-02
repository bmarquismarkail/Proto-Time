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

#include "VisualDebugAdapter.hpp"
#include "VisualOverrideService.hpp"
#include "plugins/IoPlugin.hpp"
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

    [[nodiscard]] const VideoServiceDiagnostics& diagnostics() const noexcept
    {
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
        if (!caps.realtimeSafe || !caps.deterministic || caps.nonRealtimeOnly) {
            return false;
        }
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        captures_.push_back(std::move(capture));
        return true;
    }

    [[nodiscard]] bool submitVideoDebugModel(const MachineEvent& event, const VideoDebugFrameModel& model)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (event.type == MachineEventType::RomLoaded) {
            engine_.advanceGeneration();
            bumpLifecycleEpochLocked();
            resetScanlineCapture();
            return true;
        }

        if (event.type == MachineEventType::VideoScanlineReady) {
            captureScanline(event, model);
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
            auto frame = engine_.buildDebugFrame(model, generation);
            overlayCapturedScanlines(frame);
            resetScanlineCapture();
            return submitFrame(frame);
        }

        resetScanlineCapture();
        return submitFrame(engine_.buildDebugFrame(model, generation));
    }

    [[nodiscard]] bool submitRealtimeVideoPacket(const MachineEvent& event, const RealtimeVideoPacket& packet)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (event.type == MachineEventType::RomLoaded) {
            engine_.advanceGeneration();
            bumpLifecycleEpochLocked();
            resetScanlineCapture();
            return true;
        }

        if (packet.empty()) {
            syncEngineDiagnostics();
            return false;
        }

        if (event.type == MachineEventType::VideoScanlineReady) {
            syncEngineDiagnostics();
            return false;
        }

        const bool shouldPublishFrame =
            event.type == MachineEventType::VBlank ||
            engine_.lastValidFrame().has_value() == false ||
            !packet.displayEnabled ||
            event.type == MachineEventType::MemoryWriteObserved;
        if (!shouldPublishFrame) {
            syncEngineDiagnostics();
            return false;
        }

        if (event.type == MachineEventType::VBlank && hasCompleteScanlineFrameLocked()) {
            auto frame = *scanlineFrame_;
            resetScanlineCapture();
            return submitFrame(frame);
        }
        if (event.type == MachineEventType::VBlank && hasPartialScanlineFrameLocked()) {
            syncEngineDiagnostics();
            return false;
        }

        resetScanlineCapture();
        VideoPresentPacket present;
        present.width = packet.width;
        present.height = packet.height;
        present.generation = packet.generation != 0u ? packet.generation : engine_.currentGeneration();
        present.lifecycleEpoch = lifecycleEpoch_;
        present.source = VideoFrameSource::RealtimeSnapshot;
        present.pixels = packet.argbPixels;
        const auto submitResult = engine_.submitPresentPacket(present);
        syncEngineDiagnostics();
        return submitResult.accepted;
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
    // treat this as a successful headless present and skip the SDL call).
    // The sentinel VideoFramePacket with width==0 is used to signal Headless.
    // Caller must hold sharedStateMutex_ (or equivalent serialisation) for all
    // VideoService/VideoEngine state while this runs.
    [[nodiscard]] std::optional<VideoFramePacket> consumeAndProcessFrame()
    {
        std::optional<VideoPresentPacket> frame{};
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
        bool usedFallback = false;
        if (!frame.has_value()) {
            frame = engine_.fallbackFrame();
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

        for (auto& capture : captures_) {
            (void)capture->capture(processed);
        }

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
        syncEngineDiagnostics();
        if (presenter_ == nullptr) {
            setState(VideoLifecycleState::Headless);
            return std::nullopt;  // headless — no SDL call needed, caller treats as success
        }
        return processed;
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
        engine_.advanceGeneration();
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
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
        diagnostics_.publishedFrameCount = engineStats.publishedFrameCount;
        diagnostics_.staleFrameDropCount = engineStats.staleFrameDropCount;
        diagnostics_.staleDebugFrameDropCount = engineStats.staleDebugFrameDropCount;
        diagnostics_.staleRealtimeFrameDropCount = engineStats.staleRealtimeFrameDropCount;
        diagnostics_.overwriteCount = engineStats.overwriteCount;
        diagnostics_.overwriteDebugFrameCount = engineStats.overwriteDebugFrameCount;
        diagnostics_.overwriteRealtimeFrameCount = engineStats.overwriteRealtimeFrameCount;
        diagnostics_.mailboxDepth = engineStats.mailboxDepth;
        diagnostics_.mailboxHighWaterMark = engineStats.mailboxHighWaterMark;
        diagnostics_.publishedDebugFrameCount = engineStats.publishedDebugFrameCount;
        diagnostics_.publishedRealtimeFrameCount = engineStats.publishedRealtimeFrameCount;
        diagnostics_.publishedDebugPixelBytes = engineStats.publishedDebugPixelBytes;
        diagnostics_.publishedRealtimePixelBytes = engineStats.publishedRealtimePixelBytes;
        diagnostics_.publishedPixelBytes = engineStats.publishedPixelBytes;
        diagnostics_.lastPublishedGeneration = engineStats.lastPublishedGeneration;
        diagnostics_.lifecycleEpoch = lifecycleEpoch_;
        diagnostics_.lifecycleEpochBumpCount = lifecycleEpochBumpCount_;
        diagnostics_.configuredPresenterMode = presenterConfig_.mode;
        diagnostics_.configuredPresenterPolicy = presenterPolicy_;
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
        diagnostics_.frameAgeLastNs = engineStats.frameAgeLastNs;
        diagnostics_.frameAgeHighWaterNs = engineStats.frameAgeHighWaterNs;
        diagnostics_.frameAgeUnder50usCount = engineStats.frameAgeUnder50usCount;
        diagnostics_.frameAge50To100usCount = engineStats.frameAge50To100usCount;
        diagnostics_.frameAge100To250usCount = engineStats.frameAge100To250usCount;
        diagnostics_.frameAge250To500usCount = engineStats.frameAge250To500usCount;
        diagnostics_.frameAge500usTo1msCount = engineStats.frameAge500usTo1msCount;
        diagnostics_.frameAge1To2msCount = engineStats.frameAge1To2msCount;
        diagnostics_.frameAge2To5msCount = engineStats.frameAge2To5msCount;
        diagnostics_.frameAge5To10msCount = engineStats.frameAge5To10msCount;
        diagnostics_.frameAgeOver10msCount = engineStats.frameAgeOver10msCount;
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

    void captureScanline(const MachineEvent& event, const VideoDebugFrameModel& model)
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

        const auto sourceFrame = engine_.buildDebugFrame(model, engine_.currentGeneration());
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
        ++lifecycleEpochBumpCount_;
    }

    VideoEngine engine_{};
    VisualOverrideService* visualOverrideService_ = nullptr;
    const IVisualDebugAdapter* visualDebugAdapter_ = nullptr;
    VideoPresenterConfig presenterConfig_{};
    std::unique_ptr<IVideoPresenterPlugin> presenter_{};
    std::vector<std::unique_ptr<IVideoFrameProcessorPlugin>> processors_{};
    std::vector<std::unique_ptr<IVideoCapturePlugin>> captures_{};
    mutable VideoServiceDiagnostics diagnostics_{};
    VideoLifecycleState state_ = VideoLifecycleState::Headless;
    mutable std::mutex nonRealTimeMutex_;
    std::optional<VideoFramePacket> scanlineFrame_{};
    std::vector<bool> scanlinesCaptured_{};
    std::size_t scanlineCaptureCount_ = 0;
    uint64_t lifecycleEpoch_ = 1;
    std::size_t lifecycleEpochBumpCount_ = 0;
    VideoPresenterPolicy presenterPolicy_ = VideoPresenterPolicy::HardwarePreferredWithFallback;
    std::atomic<bool> enforceLifecycleContract_{false};
    std::atomic<std::size_t> lifecycleMutationScopeDepth_{0};
    mutable std::atomic<std::size_t> lifecycleContractDeniedCalls_{0};
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_SERVICE_HPP
