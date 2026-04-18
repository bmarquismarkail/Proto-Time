#ifndef BMMQ_VIDEO_SERVICE_HPP
#define BMMQ_VIDEO_SERVICE_HPP

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    std::size_t droppedFrameCount = 0;
    std::size_t compatibilityFallbackCount = 0;
    bool headlessModeActive = true;
    std::string lastBackendError;
    std::size_t frameQueueHighWaterMark = 0;
    uint64_t lastPresentedGeneration = 0;
    std::string activeBackendName;
    VideoLifecycleState state = VideoLifecycleState::Headless;
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
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!configurationAllowed()) {
            return false;
        }
        engine_.configure(config);
        resetScanlineCapture();
        presenterConfig_.frameWidth = engine_.config().frameWidth;
        presenterConfig_.frameHeight = engine_.config().frameHeight;
        syncEngineDiagnostics();
        return true;
    }

    [[nodiscard]] bool configurePresenter(const VideoPresenterConfig& config)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!configurationAllowed()) {
            return false;
        }
        presenterConfig_ = config;
        presenterConfig_.frameWidth = std::max(presenterConfig_.frameWidth, 1);
        presenterConfig_.frameHeight = std::max(presenterConfig_.frameHeight, 1);
        return true;
    }

    [[nodiscard]] bool attachPresenter(std::unique_ptr<IVideoPresenterPlugin> presenter)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!configurationAllowed() || presenter == nullptr) {
            return false;
        }
        if (presenter_ != nullptr) {
            presenter_->close();
        }
        presenter_ = std::move(presenter);
        diagnostics_.activeBackendName = std::string(presenter_->name());
        diagnostics_.lastBackendError.clear();
        setState(VideoLifecycleState::Paused);
        return true;
    }

    [[nodiscard]] bool detachPresenter()
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (presenter_ != nullptr) {
            presenter_->close();
            presenter_.reset();
        }
        diagnostics_.activeBackendName.clear();
        diagnostics_.lastBackendError.clear();
        setState(VideoLifecycleState::Headless);
        return true;
    }

    [[nodiscard]] bool resume()
    {
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
        setState(VideoLifecycleState::Active);
        return true;
    }

    [[nodiscard]] bool pause()
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (presenter_ != nullptr) {
            presenter_->close();
            setState(VideoLifecycleState::Paused);
        } else {
            setState(VideoLifecycleState::Headless);
        }
        return true;
    }

    void setBackendActiveForTesting(bool active) noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        setState(active ? VideoLifecycleState::Active : VideoLifecycleState::Paused);
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

    [[nodiscard]] bool submitVideoState(const MachineEvent& event, const VideoStateView& state)
    {
        if (event.type == MachineEventType::RomLoaded) {
            engine_.advanceGeneration();
            resetScanlineCapture();
            return true;
        }

        if (event.type == MachineEventType::VideoScanlineReady) {
            captureScanline(state);
            syncEngineDiagnostics();
            return false;
        }

        const bool lcdControlWrite = event.type == MachineEventType::MemoryWriteObserved && event.address == 0xFF40u;
        const bool shouldBuildFrame =
            event.type == MachineEventType::VBlank ||
            engine_.lastValidFrame().has_value() == false ||
            !state.lcdEnabled() ||
            lcdControlWrite;
        if (!shouldBuildFrame) {
            syncEngineDiagnostics();
            return false;
        }

        const auto generation = engine_.currentGeneration();
        if (event.type == MachineEventType::VBlank && hasCompleteScanlineFrame()) {
            auto frame = *scanlineFrame_;
            resetScanlineCapture();
            return submitFrame(frame);
        }
        if (event.type == MachineEventType::VBlank && hasPartialScanlineFrame()) {
            auto frame = engine_.buildDebugFrame(state, generation);
            overlayCapturedScanlines(frame);
            resetScanlineCapture();
            return submitFrame(frame);
        }

        resetScanlineCapture();
        return submitFrame(engine_.buildDebugFrame(state, generation));
    }

    [[nodiscard]] bool submitFrame(const VideoFramePacket& frame)
    {
        const auto submitResult = engine_.submitFrame(frame);
        syncEngineDiagnostics();
        return submitResult.accepted;
    }

    [[nodiscard]] bool presentOneFrame()
    {
        auto frame = engine_.tryConsumeFrame();
        if (!frame.has_value()) {
            frame = engine_.fallbackFrame();
        }

        VideoFramePacket processed = *frame;
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
        syncEngineDiagnostics();
        if (presenter_ == nullptr) {
            setState(VideoLifecycleState::Headless);
            return true;
        }
        if (!presenter_->ready()) {
            diagnostics_.lastBackendError = "presenter not ready";
            ++diagnostics_.presentFailureCount;
            setState(VideoLifecycleState::Faulted);
            return false;
        }
        if (!presenter_->present(processed)) {
            diagnostics_.lastBackendError = std::string(presenter_->lastError());
            ++diagnostics_.presentFailureCount;
            setState(VideoLifecycleState::Faulted);
            return false;
        }
        diagnostics_.lastBackendError.clear();
        return true;
    }

    void advanceGeneration() noexcept
    {
        engine_.advanceGeneration();
        resetScanlineCapture();
        syncEngineDiagnostics();
    }

    [[nodiscard]] bool hasCompleteScanlineFrame() const noexcept
    {
        return scanlineFrame_.has_value() &&
               scanlineCaptureCount_ >= static_cast<std::size_t>(engine_.config().frameHeight);
    }

    [[nodiscard]] bool hasPartialScanlineFrame() const noexcept
    {
        return scanlineFrame_.has_value() && scanlineCaptureCount_ != 0u;
    }

private:
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
        diagnostics_.droppedFrameCount = engineStats.droppedFrameCount;
        diagnostics_.frameQueueHighWaterMark = engineStats.frameQueueHighWaterMark;
        diagnostics_.state = state_;
        diagnostics_.headlessModeActive = state_ == VideoLifecycleState::Headless || presenter_ == nullptr;
    }

    void resetScanlineCapture()
    {
        scanlineFrame_.reset();
        scanlinesCaptured_.clear();
        scanlineBackgroundColorIndices_.clear();
        scanlineCaptureCount_ = 0;
    }

    void captureScanline(const VideoStateView& state)
    {
        if (!state.lcdEnabled() || state.ly >= 144u) {
            resetScanlineCapture();
            return;
        }

        const int screenY = static_cast<int>(state.ly);
        if (screenY < 0 || screenY >= engine_.config().frameHeight) {
            return;
        }

        ensureScanlineFrame();
        if (!scanlineFrame_.has_value() || scanlinesCaptured_.empty()) {
            return;
        }

        engine_.renderScanline(*scanlineFrame_, state, screenY, scanlineBackgroundColorIndices_);
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

    VideoEngine engine_{};
    VisualOverrideService* visualOverrideService_ = nullptr;
    VideoPresenterConfig presenterConfig_{};
    std::unique_ptr<IVideoPresenterPlugin> presenter_{};
    std::vector<std::unique_ptr<IVideoFrameProcessorPlugin>> processors_{};
    std::vector<std::unique_ptr<IVideoCapturePlugin>> captures_{};
    mutable VideoServiceDiagnostics diagnostics_{};
    VideoLifecycleState state_ = VideoLifecycleState::Headless;
    mutable std::mutex nonRealTimeMutex_;
    std::optional<VideoFramePacket> scanlineFrame_{};
    std::vector<bool> scanlinesCaptured_{};
    std::vector<uint8_t> scanlineBackgroundColorIndices_{};
    std::size_t scanlineCaptureCount_ = 0;
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_SERVICE_HPP
