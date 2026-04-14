#include "../SdlFrontendPlugin.hpp"
#include "../../AudioService.hpp"
#include "../../VideoService.hpp"
#include "../audio_output/DummyAudioOutput.hpp"
#include "../audio_output/FileAudioOutput.hpp"
#include "../video/adapters/SdlVideoPresenter.hpp"
#include "SdlAudioOutput.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../Machine.hpp"
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

class SdlFrontendPluginImpl final : public BMMQ::ISdlFrontendPlugin,
                                    public BMMQ::LoggingPluginSupport {
public:
    explicit SdlFrontendPluginImpl(BMMQ::SdlFrontendConfig config = {})
        : config_(std::move(config)) {}

    [[nodiscard]] const BMMQ::SdlFrontendConfig& config() const noexcept override
    {
        return config_;
    }

    [[nodiscard]] const BMMQ::SdlFrontendStats& stats() const noexcept override
    {
        syncAudioTransportStats();
        return stats_;
    }

    [[nodiscard]] const std::vector<std::string>& diagnostics() const noexcept override
    {
        return entries();
    }

    [[nodiscard]] const std::optional<BMMQ::VideoStateView>& lastVideoState() const noexcept override
    {
        return lastVideoState_;
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
        return videoPresenter_ != nullptr ? videoPresenter_->windowVisible() : windowVisible_;
    }

    [[nodiscard]] bool windowVisibilityRequested() const noexcept override
    {
        return videoPresenter_ != nullptr ? videoPresenter_->windowVisibilityRequested() : windowVisibilityRequested_;
    }

    void requestWindowVisibility(bool visible) override
    {
        windowVisibilityRequested_ = visible;
        if (videoPresenter_ != nullptr) {
            videoPresenter_->requestWindowVisibility(visible);
        }
        appendLog(std::string("sdl: window visibility requested=") + (visible ? "visible" : "hidden"));
    }

    bool serviceFrontend() override
    {
        ++stats_.serviceCalls;
        std::size_t handledEvents = 0;
        if (!config_.pumpBackendEventsOnInputSample) {
            handledEvents = pumpBackendEvents();
        }

        syncVideoTransportStats();
        const bool hadFrame = config_.enableVideo && lastFrame_.has_value();
        const bool hasAudioState = config_.enableAudio && lastAudioState_.has_value();
        const bool hadAudioPreview = config_.enableAudio && lastAudioPreview_.has_value();
        const bool visibilityChanged = windowVisible_ != windowVisibilityRequested_;

        bool presented = false;
        if (hadFrame && (frameDirty_ || visibilityChanged)) {
            presented = presentLatestFrame();
        }

        const bool audioActive = hasAudioState || (audioService_ != nullptr && audioService_->engine().bufferedSamples() != 0u);
        applyWindowVisibilityRequest();
        return handledEvents != 0 || hadFrame || hasAudioState || hadAudioPreview || visibilityChanged || presented || audioActive || quitRequested_;
    }

    void setQueuedDigitalInputMask(uint32_t pressedMask) override
    {
        queuedDigitalInputMask_ = pressedMask & 0x00FFu;
        appendLog("sdl: queued input mask=" + BMMQ::detail::hexByte(static_cast<uint8_t>(*queuedDigitalInputMask_)));
    }

    void clearQueuedDigitalInputMask() override
    {
        queuedDigitalInputMask_.reset();
        appendLog("sdl: cleared queued input mask");
    }

    [[nodiscard]] std::optional<uint32_t> queuedDigitalInputMask() const noexcept override
    {
        return queuedDigitalInputMask_;
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
        if (!queuedDigitalInputMask_.has_value()) {
            return false;
        }
        return ((*queuedDigitalInputMask_) & buttonMask(button)) != 0;
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
            if (!mapped.has_value()) {
                lastHostEventSummary_ = "Unmapped host key";
                appendLog("sdl: unmapped host key ignored");
                return false;
            }

            ++stats_.keyEventsHandled;
            const bool pressed = event.type == BMMQ::SdlFrontendHostEventType::KeyDown;
            setButtonState(*mapped, pressed);
            lastHostEventSummary_ = std::string(buttonName(*mapped)) + (pressed ? " pressed from host" : " released from host");
            return true;
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

    [[nodiscard]] uint32_t queuedAudioBytes() const noexcept override
    {
        if (audioService_ == nullptr) {
            return 0u;
        }
        return static_cast<uint32_t>(audioService_->engine().queuedBytes());
    }

    bool tryInitializeBackend() override
    {
        ++stats_.backendInitAttempts;
        lastBackendError_.clear();

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (backendReady_) {
            appendLog("sdl: backend already initialized");
            return true;
        }

        uint32_t initFlags = SDL_INIT_EVENTS;
        if (config_.enableAudio) {
            initFlags |= SDL_INIT_AUDIO;
        }

        if (SDL_InitSubSystem(initFlags) != 0) {
            lastBackendError_ = SDL_GetError();
            appendLog("sdl: backend init failed: " + lastBackendError_);
            backendReady_ = false;
            initializedBackendFlags_ = 0;
            return false;
        }

        initializedBackendFlags_ = initFlags;
        if (config_.enableVideo && !ensureVideoPresenter()) {
            appendLog("sdl: continuing without live video output");
        }

        if (config_.enableAudio && !ensureAudioDevice()) {
            appendLog("sdl: continuing without live audio output");
        }

        backendReady_ = true;
        appendLog("sdl: backend initialized");
        return true;
#else
        appendLog("sdl: SDL2 backend unavailable; running in skeleton mode");
        backendReady_ = false;
        initializedBackendFlags_ = 0;
        return false;
#endif
    }

    std::size_t pumpBackendEvents() override
    {
        ++stats_.eventPumpCalls;
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
        stats_.backendEventsTranslated += handled;
        return handled;
#else
        return 0;
#endif
    }

    void onAttach(BMMQ::MutableMachineView& view) override
    {
        ++stats_.attachCount;
        audioService_ = &view.audioService();
        audioService_->setBackendPausedOrClosed(true);
        videoService_ = &view.videoService();
        configureVideoService();
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
        videoService_ = nullptr;
        videoPresenter_ = nullptr;
        windowVisible_ = false;
        appendLog("sdl: detached");
    }

    void onVideoEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        if (!config_.enableVideo) {
            return;
        }
        ++stats_.videoEvents;
        const bool lcdControlWrite = event.type == BMMQ::MachineEventType::MemoryWriteObserved && event.address == 0xFF40u;
        const bool shouldSampleVideoState =
            event.type == BMMQ::MachineEventType::VBlank ||
            !lastFrame_.has_value() ||
            lcdControlWrite;

        if (shouldSampleVideoState) {
            lastVideoState_ = view.videoState();
            if (lastVideoState_.has_value()) {
                if (videoService_ != nullptr && videoService_->submitVideoState(event, *lastVideoState_)) {
                    syncVideoTransportStats();
                    ++stats_.framesPrepared;
                    frameDirty_ = true;
                    if (config_.autoPresentOnVideoEvent) {
                        presentLatestFrame();
                    }
                }
            } else {
                lastFrame_.reset();
                frameDirty_ = false;
            }
        }

        if (event.type != BMMQ::MachineEventType::MemoryWriteObserved || shouldSampleVideoState) {
            std::string message = std::string("sdl: video event=") + BMMQ::detail::machineEventTypeName(event.type);
            if (lastVideoState_.has_value()) {
                message += " lcdc=" + BMMQ::detail::hexByte(lastVideoState_->lcdc);
            }
            if (lastFrame_.has_value()) {
                message += " frame=" + std::to_string(lastFrame_->width) + "x" + std::to_string(lastFrame_->height);
            }
            appendLog(std::move(message));
        }
    }

    void onAudioEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        if (!config_.enableAudio) {
            return;
        }
        ++stats_.audioEvents;
        lastAudioState_ = view.audioState();
        if (lastAudioState_.has_value()) {
            lastAudioPreview_ = buildAudioPreview(*lastAudioState_);
            ++stats_.audioPreviewsBuilt;
            ++audioPreviewGeneration_;
            if (audioService_ != nullptr) {
                audioService_->engine().appendRecentPcm(lastAudioState_->pcmSamples, lastAudioState_->frameCounter);
            }
        } else {
            lastAudioPreview_.reset();
            if (audioService_ != nullptr && audioService_->canPerformReset()) {
                (void)audioService_->resetStats();
                (void)audioService_->resetStream();
            }
        }

        std::string message = std::string("sdl: audio event=") + BMMQ::detail::machineEventTypeName(event.type);
        if (lastAudioState_.has_value()) {
            message += " nr12=" + BMMQ::detail::hexByte(lastAudioState_->nr12);
            message += " nr52=" + BMMQ::detail::hexByte(lastAudioState_->nr52);
        }
        if (lastAudioPreview_.has_value()) {
            message += " samples=" + std::to_string(lastAudioPreview_->sampleCount());
        }
        message += " buffered=" + std::to_string(bufferedAudioSamples()) + " samples";
        appendLog(std::move(message));
    }

    std::optional<uint32_t> sampleDigitalInput(const BMMQ::MachineView&) override
    {
        if (!config_.enableInput) {
            return std::nullopt;
        }
        ++stats_.inputPolls;
        if (config_.pumpBackendEventsOnInputSample) {
            pumpBackendEvents();
        }
        if (queuedDigitalInputMask_.has_value()) {
            ++stats_.inputSamplesProvided;
            return queuedDigitalInputMask_;
        }
        return std::nullopt;
    }

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
        auto mask = static_cast<uint8_t>(queuedDigitalInputMask_.value_or(0u) & 0x00FFu);
        const auto bit = buttonMask(button);
        const auto oldMask = mask;
        if (pressed) {
            mask = static_cast<uint8_t>(mask | bit);
        } else {
            mask = static_cast<uint8_t>(mask & static_cast<uint8_t>(~bit));
        }
        queuedDigitalInputMask_ = mask;
        if (mask != oldMask) {
            ++stats_.buttonTransitions;
            appendLog(std::string("sdl: button ") + std::string(buttonName(button)) + (pressed ? " pressed" : " released"));
        }
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
        stats_.lastQueuedAudioBytes = queuedAudioBytes();
        stats_.peakQueuedAudioBytes = std::max<std::uint32_t>(
            stats_.peakQueuedAudioBytes,
            static_cast<std::uint32_t>(stats_.audioBufferedHighWaterSamples * sizeof(int16_t)));
    }

    void configureVideoService()
    {
        if (videoService_ == nullptr) {
            return;
        }
        (void)videoService_->pause();
        (void)videoService_->configure({
            .frameWidth = std::max(config_.frameWidth, 1),
            .frameHeight = std::max(config_.frameHeight, 1),
            .queueCapacityFrames = 3,
        });
        (void)videoService_->configurePresenter({
            .windowTitle = config_.windowTitle,
            .scale = std::max(config_.windowScale, 1),
            .frameWidth = std::max(config_.frameWidth, 1),
            .frameHeight = std::max(config_.frameHeight, 1),
            .createHiddenWindowOnOpen = true,
            .showWindowOnPresent = config_.showWindowOnPresent,
        });
        if (config_.enableVideo) {
            auto presenter = std::make_unique<BMMQ::SdlVideoPresenter>();
            videoPresenter_ = presenter.get();
            videoPresenter_->requestWindowVisibility(windowVisibilityRequested_);
            (void)videoService_->attachPresenter(std::move(presenter));
        }
    }

    bool ensureVideoPresenter()
    {
        if (!config_.enableVideo) {
            return true;
        }
        if (videoService_ == nullptr) {
            lastBackendError_ = "Video service unavailable";
            appendLog("sdl: video service unavailable");
            return false;
        }
        if (videoPresenter_ == nullptr) {
            configureVideoService();
        }
        if (!videoService_->resume()) {
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
        if (const auto& frame = videoService_->engine().lastValidFrame(); frame.has_value()) {
            BMMQ::SdlFrameBuffer compatFrame;
            compatFrame.width = frame->width;
            compatFrame.height = frame->height;
            compatFrame.pixels = frame->pixels;
            lastFrame_ = std::move(compatFrame);
        }
        if (videoPresenter_ != nullptr) {
            windowVisible_ = videoPresenter_->windowVisible();
            windowVisibilityRequested_ = videoPresenter_->windowVisibilityRequested();
        }
    }

    bool presentLatestFrame()
    {
        ++stats_.renderAttempts;
        if (!lastFrame_.has_value()) {
            lastRenderSummary_ = "No frame available";
            return false;
        }

        if (videoService_ == nullptr) {
            lastRenderSummary_ = "Video service unavailable";
            return false;
        }
        if (!backendReady_ || videoService_->state() != BMMQ::VideoLifecycleState::Active) {
            lastRenderSummary_ = "Frame prepared but backend not ready";
            return false;
        }

        if (config_.showWindowOnPresent) {
            requestWindowVisibility(true);
        }

        const bool presented = videoService_->presentOneFrame();
        syncVideoTransportStats();
        if (!presented) {
            lastBackendError_ = videoService_->diagnostics().lastBackendError;
            lastRenderSummary_ = lastBackendError_.empty() ? "Video presentation failed" : lastBackendError_;
            appendLog("sdl: video presentation failed: " + lastRenderSummary_);
            return false;
        }
        ++stats_.framesPresented;
        frameDirty_ = false;
        lastRenderSummary_ = "Presented frame " + std::to_string(lastFrame_->width) + "x" + std::to_string(lastFrame_->height);
        return true;
    }

    void applyWindowVisibilityRequest() noexcept
    {
        if (videoPresenter_ != nullptr) {
            videoPresenter_->requestWindowVisibility(windowVisibilityRequested_);
            windowVisible_ = videoPresenter_->windowVisible();
            return;
        }
        windowVisible_ = windowVisibilityRequested_;
    }

    void shutdownBackend() noexcept
    {
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
        if (audioService_ != nullptr) {
            (void)audioService_->resetStats();
            (void)audioService_->resetStream();
        }
        backendReady_ = false;
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

    bool ensureAudioDevice()
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
        audioService_->setBackendPausedOrClosed(true);
        if (!audioService_->configureEngine({
            .sourceSampleRate = kSourceAudioSampleRate,
            .deviceSampleRate = kSourceAudioSampleRate,
            .ringBufferCapacitySamples = config_.audioRingBufferCapacitySamples,
            .frameChunkSamples = kApuFrameSamples,
        })) {
            lastBackendError_ = "Audio engine configure rejected while backend active";
            appendLog("sdl: " + lastBackendError_);
            return false;
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
            return false;
        }

        if (!audioOutput_->open(audioService_->engine(), {
                .backend = selectedAudioBackend_,
                .requestedSampleRate = kSourceAudioSampleRate,
                .callbackChunkSamples = static_cast<std::size_t>(std::max(config_.audioCallbackChunkSamples, 1)),
                .channels = 1,
                .testForcedDeviceSampleRate = config_.enableAudioResamplingDiagnostics
                                                ? config_.testForcedAudioDeviceSampleRate
                                                : 0,
                .filePath = config_.audioOutputFilePath,
                .appendToFile = config_.audioFileAppend,
                .audioService = audioService_,
            })) {
            lastBackendError_ = std::string(audioOutput_->lastError());
            appendLog("sdl: audio device open failed: " + lastBackendError_);
            return false;
        }

        audioService_->setBackendPausedOrClosed(false);

        syncAudioTransportStats();
        appendLog("sdl: audio device opened at " + std::to_string(audioService_->engine().config().deviceSampleRate) + " Hz");
        return true;
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

    BMMQ::SdlFrontendConfig config_;
    mutable BMMQ::SdlFrontendStats stats_;
    bool backendReady_ = false;
    std::optional<BMMQ::VideoStateView> lastVideoState_;
    std::optional<BMMQ::AudioStateView> lastAudioState_;
    std::optional<BMMQ::SdlAudioPreviewBuffer> lastAudioPreview_;
    std::optional<BMMQ::DigitalInputStateView> lastInputState_;
    std::optional<BMMQ::SdlFrameBuffer> lastFrame_;
    bool frameDirty_ = false;
    std::optional<uint32_t> queuedDigitalInputMask_;
    bool quitRequested_ = false;
    bool windowVisible_ = false;
    bool windowVisibilityRequested_ = false;
    std::string lastHostEventSummary_;
    std::string lastRenderSummary_;
    std::string lastBackendError_;
    uint32_t initializedBackendFlags_ = 0;
    double audioPhase_ = 0.0;
    uint64_t audioPreviewGeneration_ = 0;
    BMMQ::AudioService* audioService_ = nullptr;
    BMMQ::VideoService* videoService_ = nullptr;
    BMMQ::SdlVideoPresenter* videoPresenter_ = nullptr;
    std::unique_ptr<BMMQ::IAudioOutputBackend> audioOutput_ = std::make_unique<BMMQ::SdlAudioOutputBackend>();
    std::string selectedAudioBackend_ = "sdl";
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
