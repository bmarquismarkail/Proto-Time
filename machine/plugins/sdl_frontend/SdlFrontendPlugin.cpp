#include "../SdlFrontendPlugin.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
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

struct AudioTransportCounters {
    std::atomic<std::size_t> bufferedHighWaterSamples{0};
    std::atomic<std::size_t> callbackCount{0};
    std::atomic<std::size_t> samplesDelivered{0};
    std::atomic<std::size_t> underrunCount{0};
    std::atomic<std::size_t> silenceSamplesFilled{0};
    std::atomic<std::size_t> overrunDropCount{0};
    std::atomic<std::size_t> droppedSamples{0};
    std::atomic<std::size_t> sourceSamplesPushed{0};
    std::atomic<std::size_t> resampleSourceSamplesConsumed{0};
    std::atomic<std::size_t> resampleOutputSamplesProduced{0};
};

struct AudioTransportBuffer {
    std::vector<int16_t> samples;
    std::atomic<std::size_t> readIndex{0};
    std::atomic<std::size_t> writeIndex{0};
    std::size_t capacity = 0;
};

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
        return windowVisible_;
    }

    [[nodiscard]] bool windowVisibilityRequested() const noexcept override
    {
        return windowVisibilityRequested_;
    }

    void requestWindowVisibility(bool visible) override
    {
        windowVisibilityRequested_ = visible;
        appendLog(std::string("sdl: window visibility requested=") + (visible ? "visible" : "hidden"));
    }

    bool serviceFrontend() override
    {
        ++stats_.serviceCalls;
        std::size_t handledEvents = 0;
        if (!config_.pumpBackendEventsOnInputSample) {
            handledEvents = pumpBackendEvents();
        }

        const bool hadFrame = config_.enableVideo && lastFrame_.has_value();
        const bool hasAudioState = config_.enableAudio && lastAudioState_.has_value();
        const bool hadAudioPreview = config_.enableAudio && lastAudioPreview_.has_value();
        const bool visibilityChanged = windowVisible_ != windowVisibilityRequested_;

        bool presented = false;
        if (hadFrame && (frameDirty_ || visibilityChanged)) {
            presented = presentLatestFrame();
        }

        const bool audioActive = hasAudioState || bufferedAudioSamples() != 0u;
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
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        return audioDevice_ != 0;
#else
        return false;
#endif
    }

    [[nodiscard]] std::size_t bufferedAudioSamples() const noexcept override
    {
        const auto buffered = audioBufferedSamples();
        syncAudioTransportStats();
        return buffered;
    }

    [[nodiscard]] uint32_t queuedAudioBytes() const noexcept override
    {
        return static_cast<uint32_t>(audioBufferedSamples() * sizeof(int16_t));
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
        if (config_.createHiddenWindowOnInitialize && config_.enableVideo) {
            initFlags |= SDL_INIT_VIDEO;
        }

        if (SDL_InitSubSystem(initFlags) != 0) {
            lastBackendError_ = SDL_GetError();
            appendLog("sdl: backend init failed: " + lastBackendError_);
            backendReady_ = false;
            initializedBackendFlags_ = 0;
            return false;
        }

        initializedBackendFlags_ = initFlags;
        if ((initFlags & SDL_INIT_VIDEO) != 0u) {
            const int width = std::max(config_.frameWidth * std::max(config_.windowScale, 1), 1);
            const int height = std::max(config_.frameHeight * std::max(config_.windowScale, 1), 1);
            window_ = SDL_CreateWindow(config_.windowTitle.c_str(),
                                       SDL_WINDOWPOS_UNDEFINED,
                                       SDL_WINDOWPOS_UNDEFINED,
                                       width,
                                       height,
                                       SDL_WINDOW_HIDDEN);
            if (window_ == nullptr) {
                lastBackendError_ = SDL_GetError();
                appendLog("sdl: hidden window creation failed: " + lastBackendError_);
                shutdownBackend();
                return false;
            }
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

    void onAttach(const BMMQ::MachineView&) override
    {
        ++stats_.attachCount;
        appendLog("sdl: attached");
        if (config_.autoInitializeBackend) {
            tryInitializeBackend();
        }
    }

    void onDetach(const BMMQ::MachineView&) override
    {
        ++stats_.detachCount;
        shutdownBackend();
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
                const bool shouldRefreshFrame =
                    event.type == BMMQ::MachineEventType::VBlank ||
                    !lastFrame_.has_value() ||
                    !lastVideoState_->lcdEnabled() ||
                    lcdControlWrite;
                if (shouldRefreshFrame) {
                    lastFrame_ = buildDebugFrame(*lastVideoState_);
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
            appendAudioSamples(*lastAudioState_);
        } else {
            lastAudioPreview_.reset();
            resetAudioTransportState();
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

    [[nodiscard]] std::size_t audioBufferedSamples() const noexcept
    {
        const auto capacity = audioTransport_.capacity;
        if (capacity == 0u) {
            return 0u;
        }

        const auto readIndex = audioTransport_.readIndex.load(std::memory_order_acquire);
        const auto writeIndex = audioTransport_.writeIndex.load(std::memory_order_acquire);
        if (writeIndex >= readIndex) {
            return writeIndex - readIndex;
        }
        return capacity - (readIndex - writeIndex);
    }

    void syncAudioTransportStats() const noexcept
    {
        stats_.audioSourceSampleRate = sourceAudioSampleRate_;
        stats_.audioDeviceSampleRate = audioSampleRate_;
        stats_.audioCallbackChunkSamples = static_cast<std::size_t>(std::max(config_.audioCallbackChunkSamples, 1));
        stats_.audioRingBufferCapacitySamples = audioTransport_.capacity;
        stats_.audioBufferedHighWaterSamples = audioCounters_.bufferedHighWaterSamples.load(std::memory_order_relaxed);
        stats_.audioCallbackCount = audioCounters_.callbackCount.load(std::memory_order_relaxed);
        stats_.audioSamplesDelivered = audioCounters_.samplesDelivered.load(std::memory_order_relaxed);
        stats_.audioUnderrunCount = audioCounters_.underrunCount.load(std::memory_order_relaxed);
        stats_.audioSilenceSamplesFilled = audioCounters_.silenceSamplesFilled.load(std::memory_order_relaxed);
        stats_.audioOverrunDropCount = audioCounters_.overrunDropCount.load(std::memory_order_relaxed);
        stats_.audioDroppedSamples = audioCounters_.droppedSamples.load(std::memory_order_relaxed);
        stats_.audioResamplingActive = audioSampleRate_ != sourceAudioSampleRate_;
        stats_.audioResampleRatio = audioResampler_.ratio();
        stats_.audioSourceSamplesPushed = audioCounters_.sourceSamplesPushed.load(std::memory_order_relaxed);
        stats_.audioResampleSourceSamplesConsumed = audioCounters_.resampleSourceSamplesConsumed.load(std::memory_order_relaxed);
        stats_.audioResampleOutputSamplesProduced = audioCounters_.resampleOutputSamplesProduced.load(std::memory_order_relaxed);
        stats_.lastQueuedAudioBytes = queuedAudioBytes();
        stats_.peakQueuedAudioBytes = std::max<std::uint32_t>(
            stats_.peakQueuedAudioBytes,
            static_cast<std::uint32_t>(stats_.audioBufferedHighWaterSamples * sizeof(int16_t)));
    }

    void resetAudioTransportCounters() noexcept
    {
        audioCounters_.bufferedHighWaterSamples.store(0u, std::memory_order_relaxed);
        audioCounters_.callbackCount.store(0u, std::memory_order_relaxed);
        audioCounters_.samplesDelivered.store(0u, std::memory_order_relaxed);
        audioCounters_.underrunCount.store(0u, std::memory_order_relaxed);
        audioCounters_.silenceSamplesFilled.store(0u, std::memory_order_relaxed);
        audioCounters_.overrunDropCount.store(0u, std::memory_order_relaxed);
        audioCounters_.droppedSamples.store(0u, std::memory_order_relaxed);
        audioCounters_.sourceSamplesPushed.store(0u, std::memory_order_relaxed);
        audioCounters_.resampleSourceSamplesConsumed.store(0u, std::memory_order_relaxed);
        audioCounters_.resampleOutputSamplesProduced.store(0u, std::memory_order_relaxed);
    }

    void resetAudioTransportState() noexcept
    {
        audioTransport_.readIndex.store(0u, std::memory_order_release);
        audioTransport_.writeIndex.store(0u, std::memory_order_release);
        audioResampler_.reset();
        lastQueuedAudioFrameCounter_ = 0;
        lastQueuedAudioPreviewGeneration_ = 0;
        syncAudioTransportStats();
    }

    void initializeAudioTransport()
    {
        const auto requestedCapacity = std::max<std::size_t>(config_.audioRingBufferCapacitySamples, kApuFrameSamples);
        audioTransport_.samples.assign(requestedCapacity, 0);
        audioTransport_.capacity = audioTransport_.samples.size();
        resetAudioTransportCounters();
        resetAudioTransportState();
    }

    void noteBufferedHighWater(std::size_t bufferedSamples) noexcept
    {
        auto previous = audioCounters_.bufferedHighWaterSamples.load(std::memory_order_relaxed);
        while (bufferedSamples > previous &&
               !audioCounters_.bufferedHighWaterSamples.compare_exchange_weak(
                   previous, bufferedSamples, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    void pushAudioSample(int16_t sample) noexcept
    {
        if (audioTransport_.capacity == 0u) {
            return;
        }

        while (true) {
            auto readIndex = audioTransport_.readIndex.load(std::memory_order_acquire);
            const auto writeIndex = audioTransport_.writeIndex.load(std::memory_order_relaxed);
            const auto nextWriteIndex = (writeIndex + 1u) % audioTransport_.capacity;
            if (nextWriteIndex == readIndex) {
                const auto nextReadIndex = (readIndex + 1u) % audioTransport_.capacity;
                if (!audioTransport_.readIndex.compare_exchange_weak(
                        readIndex, nextReadIndex, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    continue;
                }
                audioCounters_.overrunDropCount.fetch_add(1u, std::memory_order_relaxed);
                audioCounters_.droppedSamples.fetch_add(1u, std::memory_order_relaxed);
            }

            audioTransport_.samples[writeIndex] = sample;
            audioTransport_.writeIndex.store(nextWriteIndex, std::memory_order_release);
            audioCounters_.sourceSamplesPushed.fetch_add(1u, std::memory_order_relaxed);
            noteBufferedHighWater(audioBufferedSamples());
            return;
        }
    }

    [[nodiscard]] bool peekAudioSample(std::size_t offset, int16_t& sample) const noexcept
    {
        const auto capacity = audioTransport_.capacity;
        if (capacity == 0u) {
            return false;
        }

        const auto readIndex = audioTransport_.readIndex.load(std::memory_order_acquire);
        const auto writeIndex = audioTransport_.writeIndex.load(std::memory_order_acquire);
        const auto available = writeIndex >= readIndex
                                 ? (writeIndex - readIndex)
                                 : (capacity - (readIndex - writeIndex));
        if (offset >= available) {
            return false;
        }

        const auto index = (readIndex + offset) % capacity;
        sample = audioTransport_.samples[index];
        return true;
    }

    void consumeAudioSamples(std::size_t count) noexcept
    {
        const auto capacity = audioTransport_.capacity;
        if (capacity == 0u || count == 0u) {
            return;
        }

        const auto readIndex = audioTransport_.readIndex.load(std::memory_order_acquire);
        const auto writeIndex = audioTransport_.writeIndex.load(std::memory_order_acquire);
        const auto available = writeIndex >= readIndex
                                 ? (writeIndex - readIndex)
                                 : (capacity - (readIndex - writeIndex));
        const auto consumed = std::min(count, available);
        if (consumed == 0u) {
            return;
        }

        const auto nextReadIndex = (readIndex + consumed) % capacity;
        audioTransport_.readIndex.store(nextReadIndex, std::memory_order_release);
    }

    void appendAudioSamples(const BMMQ::AudioStateView& state)
    {
        if (!state.hasPcmSamples()) {
            return;
        }

        const bool resetRequired =
            lastQueuedAudioFrameCounter_ == 0u ||
            state.frameCounter < lastQueuedAudioFrameCounter_;
        std::size_t desiredSampleCount = 0u;
        if (resetRequired) {
            desiredSampleCount = std::min<std::size_t>(state.pcmSamples.size(), kApuFrameSamples);
        } else if (state.frameCounter > lastQueuedAudioFrameCounter_) {
            const auto frameDelta = state.frameCounter - lastQueuedAudioFrameCounter_;
            desiredSampleCount = static_cast<std::size_t>(std::min<uint64_t>(
                static_cast<uint64_t>(state.pcmSamples.size()),
                frameDelta * static_cast<uint64_t>(kApuFrameSamples)));
        }

        if (desiredSampleCount == 0u) {
            return;
        }

        if (resetRequired) {
            resetAudioTransportState();
        }

        const auto startIndex = state.pcmSamples.size() - desiredSampleCount;
        for (std::size_t i = startIndex; i < state.pcmSamples.size(); ++i) {
            pushAudioSample(state.pcmSamples[i]);
        }
        lastQueuedAudioFrameCounter_ = state.frameCounter;
        syncAudioTransportStats();
    }

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    static void sdlAudioCallback(void* userdata, Uint8* stream, int len)
    {
        if (userdata == nullptr || stream == nullptr || len <= 0) {
            return;
        }

        static_cast<SdlFrontendPluginImpl*>(userdata)->drainAudioCallback(stream, len);
    }

    void drainAudioCallback(Uint8* stream, int len) noexcept
    {
        auto* out = reinterpret_cast<int16_t*>(stream);
        const auto requestedSamples = static_cast<std::size_t>(len / static_cast<int>(sizeof(int16_t)));

        audioCounters_.callbackCount.fetch_add(1u, std::memory_order_relaxed);
        std::size_t delivered = 0u;

        const auto renderStats = audioResampler_.render(
            std::span<int16_t>(out, requestedSamples),
            [this](std::size_t offset, int16_t& sample) noexcept {
                return peekAudioSample(offset, sample);
            },
            [this](std::size_t consumed) noexcept {
                const auto available = audioBufferedSamples();
                const auto actual = std::min(consumed, available);
                consumeAudioSamples(actual);
                return actual;
            });

        delivered = renderStats.outputSamplesProduced - renderStats.silenceSamplesFilled;
        if (renderStats.silenceSamplesFilled != 0u) {
            audioCounters_.underrunCount.fetch_add(1u, std::memory_order_relaxed);
            audioCounters_.silenceSamplesFilled.fetch_add(renderStats.silenceSamplesFilled, std::memory_order_relaxed);
        }
        audioCounters_.samplesDelivered.fetch_add(delivered, std::memory_order_relaxed);
        audioCounters_.resampleSourceSamplesConsumed.fetch_add(renderStats.sourceSamplesConsumed, std::memory_order_relaxed);
        audioCounters_.resampleOutputSamplesProduced.fetch_add(renderStats.outputSamplesProduced, std::memory_order_relaxed);
    }
#endif

    bool presentLatestFrame()
    {
        ++stats_.renderAttempts;
        if (!lastFrame_.has_value()) {
            lastRenderSummary_ = "No frame available";
            return false;
        }

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (!backendReady_) {
            lastRenderSummary_ = "Frame prepared but backend not ready";
            return false;
        }
        if (window_ == nullptr) {
            lastRenderSummary_ = "Frame prepared without window";
            return false;
        }

        if (config_.showWindowOnPresent) {
            requestWindowVisibility(true);
            applyWindowVisibilityRequest();
        }

        if (renderer_ == nullptr) {
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
            if (renderer_ == nullptr) {
                lastBackendError_ = SDL_GetError();
                lastRenderSummary_ = "Renderer creation failed";
                appendLog("sdl: renderer creation failed: " + lastBackendError_);
                return false;
            }
            SDL_RenderSetLogicalSize(renderer_, lastFrame_->width, lastFrame_->height);
        }

        if (texture_ == nullptr || textureWidth_ != lastFrame_->width || textureHeight_ != lastFrame_->height) {
            if (texture_ != nullptr) {
                SDL_DestroyTexture(texture_);
                texture_ = nullptr;
            }
            texture_ = SDL_CreateTexture(renderer_,
                                         SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         lastFrame_->width,
                                         lastFrame_->height);
            if (texture_ == nullptr) {
                lastBackendError_ = SDL_GetError();
                lastRenderSummary_ = "Texture creation failed";
                appendLog("sdl: texture creation failed: " + lastBackendError_);
                return false;
            }
            textureWidth_ = lastFrame_->width;
            textureHeight_ = lastFrame_->height;
        }

        if (SDL_UpdateTexture(texture_, nullptr, lastFrame_->pixels.data(), lastFrame_->width * static_cast<int>(sizeof(uint32_t))) != 0) {
            lastBackendError_ = SDL_GetError();
            lastRenderSummary_ = "Texture update failed";
            appendLog("sdl: texture update failed: " + lastBackendError_);
            return false;
        }

        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
        ++stats_.framesPresented;
        frameDirty_ = false;
        lastRenderSummary_ = "Presented frame " + std::to_string(lastFrame_->width) + "x" + std::to_string(lastFrame_->height);
        return true;
#else
        lastRenderSummary_ = "Frame prepared in skeleton mode";
        return false;
#endif
    }

    void applyWindowVisibilityRequest() noexcept
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (window_ != nullptr) {
            if (windowVisibilityRequested_) {
                SDL_ShowWindow(window_);
            } else {
                SDL_HideWindow(window_);
            }
        }
#endif
        windowVisible_ = windowVisibilityRequested_;
    }

    void shutdownBackend() noexcept
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (audioDevice_ != 0) {
            SDL_CloseAudioDevice(audioDevice_);
            audioDevice_ = 0;
        }
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        textureWidth_ = 0;
        textureHeight_ = 0;
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (initializedBackendFlags_ != 0u) {
            SDL_QuitSubSystem(initializedBackendFlags_);
            initializedBackendFlags_ = 0u;
        }
#endif
        resetAudioTransportCounters();
        resetAudioTransportState();
        backendReady_ = false;
    }

    static uint32_t paletteColor(uint8_t shade) noexcept
    {
        switch (shade & 0x03u) {
        case 0:
            return 0xFFE0F8D0u;
        case 1:
            return 0xFF88C070u;
        case 2:
            return 0xFF346856u;
        default:
            return 0xFF081820u;
        }
    }

    [[nodiscard]] static uint8_t mapPaletteShade(uint8_t palette, uint8_t colorIndex) noexcept
    {
        return static_cast<uint8_t>((palette >> (colorIndex * 2u)) & 0x03u);
    }

    [[nodiscard]] static uint8_t readVramByte(const BMMQ::VideoStateView& state, uint16_t address) noexcept
    {
        if (address < 0x8000u || address >= 0xA000u) {
            return 0xFFu;
        }
        const auto index = static_cast<std::size_t>(address - 0x8000u);
        if (index >= state.vram.size()) {
            return 0xFFu;
        }
        return state.vram[index];
    }

    [[nodiscard]] static uint8_t readOamByte(const BMMQ::VideoStateView& state, std::size_t index) noexcept
    {
        if (index >= state.oam.size()) {
            return 0xFFu;
        }
        return state.oam[index];
    }

    [[nodiscard]] static uint8_t sampleTileColorIndex(const BMMQ::VideoStateView& state,
                                                      uint8_t tileIndex,
                                                      bool unsignedTileData,
                                                      uint8_t tileX,
                                                      uint8_t tileY) noexcept
    {
        uint16_t tileAddress = 0x8000u;
        if (unsignedTileData) {
            tileAddress = static_cast<uint16_t>(0x8000u + static_cast<uint16_t>(tileIndex) * 16u);
        } else {
            tileAddress = static_cast<uint16_t>(0x9000 + static_cast<int16_t>(static_cast<int8_t>(tileIndex)) * 16);
        }

        const auto rowAddress = static_cast<uint16_t>(tileAddress + static_cast<uint16_t>(tileY) * 2u);
        const auto low = readVramByte(state, rowAddress);
        const auto high = readVramByte(state, static_cast<uint16_t>(rowAddress + 1u));
        const auto bit = static_cast<uint8_t>(7u - (tileX & 0x07u));
        return static_cast<uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
    }

    [[nodiscard]] static uint8_t backgroundColorIndex(const BMMQ::VideoStateView& state, int screenX, int screenY) noexcept
    {
        const bool windowEnabled = (state.lcdc & 0x20u) != 0u;
        const int windowLeft = static_cast<int>(state.wx) - 7;
        const bool useWindow = windowEnabled && screenY >= static_cast<int>(state.wy) && screenX >= windowLeft;

        int mapX = (screenX + static_cast<int>(state.scx)) & 0xFF;
        int mapY = (screenY + static_cast<int>(state.scy)) & 0xFF;
        uint16_t mapBase = (state.lcdc & 0x08u) != 0u ? 0x9C00u : 0x9800u;

        if (useWindow) {
            mapBase = (state.lcdc & 0x40u) != 0u ? 0x9C00u : 0x9800u;
            mapX = std::max(0, screenX - windowLeft);
            mapY = std::max(0, screenY - static_cast<int>(state.wy));
        }

        const auto tileMapAddress = static_cast<uint16_t>(
            mapBase + static_cast<uint16_t>(((mapY >> 3) & 0x1Fu) * 32 + ((mapX >> 3) & 0x1Fu)));
        const auto tileIndex = readVramByte(state, tileMapAddress);
        const bool unsignedTileData = (state.lcdc & 0x10u) != 0u;
        return sampleTileColorIndex(state,
                                    tileIndex,
                                    unsignedTileData,
                                    static_cast<uint8_t>(mapX & 0x07),
                                    static_cast<uint8_t>(mapY & 0x07));
    }

    void compositeSprites(BMMQ::SdlFrameBuffer& frame,
                          const BMMQ::VideoStateView& state,
                          std::span<const uint8_t> backgroundColorIndices) const
    {
        if ((state.lcdc & 0x02u) == 0u || state.oam.empty()) {
            return;
        }

        const bool tallSprites = (state.lcdc & 0x04u) != 0u;
        const int spriteHeight = tallSprites ? 16 : 8;
        for (int spriteIndex = 39; spriteIndex >= 0; --spriteIndex) {
            const auto base = static_cast<std::size_t>(spriteIndex) * 4u;
            if (base + 3u >= state.oam.size()) {
                continue;
            }

            const int spriteY = static_cast<int>(readOamByte(state, base)) - 16;
            const int spriteX = static_cast<int>(readOamByte(state, base + 1u)) - 8;
            uint8_t tileIndex = readOamByte(state, base + 2u);
            const uint8_t attributes = readOamByte(state, base + 3u);
            if (spriteX <= -8 || spriteX >= frame.width || spriteY <= -spriteHeight || spriteY >= frame.height) {
                continue;
            }

            if (tallSprites) {
                tileIndex = static_cast<uint8_t>(tileIndex & 0xFEu);
            }

            const bool xFlip = (attributes & 0x20u) != 0u;
            const bool yFlip = (attributes & 0x40u) != 0u;
            const bool behindBackground = (attributes & 0x80u) != 0u;
            const uint8_t palette = (attributes & 0x10u) != 0u ? state.obp1 : state.obp0;

            for (int localY = 0; localY < spriteHeight; ++localY) {
                const int screenY = spriteY + localY;
                if (screenY < 0 || screenY >= frame.height) {
                    continue;
                }

                int spriteRow = yFlip ? (spriteHeight - 1 - localY) : localY;
                uint8_t effectiveTile = tileIndex;
                if (tallSprites && spriteRow >= 8) {
                    effectiveTile = static_cast<uint8_t>(tileIndex + 1u);
                    spriteRow -= 8;
                }

                for (int localX = 0; localX < 8; ++localX) {
                    const int screenX = spriteX + localX;
                    if (screenX < 0 || screenX >= frame.width) {
                        continue;
                    }

                    const auto pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(frame.width)
                                          + static_cast<std::size_t>(screenX);
                    const uint8_t spriteColumn = static_cast<uint8_t>(xFlip ? (7 - localX) : localX);
                    const uint8_t colorIndex = sampleTileColorIndex(state,
                                                                    effectiveTile,
                                                                    true,
                                                                    spriteColumn,
                                                                    static_cast<uint8_t>(spriteRow));
                    if (colorIndex == 0u) {
                        continue;
                    }
                    if (behindBackground && backgroundColorIndices[pixelIndex] != 0u) {
                        continue;
                    }

                    const uint8_t shade = mapPaletteShade(palette, colorIndex);
                    frame.pixels[pixelIndex] = paletteColor(shade);
                }
            }
        }
    }

    [[nodiscard]] BMMQ::SdlFrameBuffer buildDebugFrame(const BMMQ::VideoStateView& state) const
    {
        BMMQ::SdlFrameBuffer frame;
        frame.width = std::max(config_.frameWidth, 1);
        frame.height = std::max(config_.frameHeight, 1);

        const auto pixelCount = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
        frame.pixels.resize(pixelCount, paletteColor(0));
        if (state.vram.empty()) {
            return frame;
        }

        const bool lcdEnabled = state.lcdEnabled();
        const bool backgroundEnabled = (state.lcdc & 0x01u) != 0u;
        std::vector<uint8_t> backgroundColorIndices(pixelCount, 0u);
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width)
                                      + static_cast<std::size_t>(x);
                if (!lcdEnabled) {
                    frame.pixels[pixelIndex] = paletteColor(0);
                    continue;
                }

                uint8_t shade = 0;
                if (backgroundEnabled) {
                    const auto colorIndex = backgroundColorIndex(state, x, y);
                    backgroundColorIndices[pixelIndex] = colorIndex;
                    shade = mapPaletteShade(state.bgp, colorIndex);
                }
                frame.pixels[pixelIndex] = paletteColor(shade);
            }
        }

        if (lcdEnabled) {
            compositeSprites(frame, state, std::span<const uint8_t>(backgroundColorIndices.data(), backgroundColorIndices.size()));
        }
        return frame;
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
        if (!config_.enableAudio || audioDevice_ != 0) {
            return true;
        }

        initializeAudioTransport();
        audioResampler_.configure(sourceAudioSampleRate_, sourceAudioSampleRate_);

        SDL_AudioSpec desired{};
        desired.freq = sourceAudioSampleRate_;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = static_cast<Uint16>(std::max(config_.audioCallbackChunkSamples, 1));
        desired.callback = &SdlFrontendPluginImpl::sdlAudioCallback;
        desired.userdata = this;

        SDL_AudioSpec obtained{};
        audioDevice_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
        if (audioDevice_ == 0) {
            lastBackendError_ = SDL_GetError();
            appendLog("sdl: audio device open failed: " + lastBackendError_);
            return false;
        }

        audioSampleRate_ = obtained.freq > 0 ? obtained.freq : desired.freq;
        if (config_.enableAudioResamplingDiagnostics && config_.testForcedAudioDeviceSampleRate > 0) {
            audioSampleRate_ = config_.testForcedAudioDeviceSampleRate;
        }
        if (obtained.format != AUDIO_S16SYS || obtained.channels != 1) {
            lastBackendError_ = "SDL audio device format mismatch";
            appendLog("sdl: audio device format mismatch");
            SDL_CloseAudioDevice(audioDevice_);
            audioDevice_ = 0;
            resetAudioTransportState();
            return false;
        }
        audioResampler_.configure(sourceAudioSampleRate_, audioSampleRate_);

        syncAudioTransportStats();
        SDL_PauseAudioDevice(audioDevice_, 0);
        appendLog("sdl: audio device opened at " + std::to_string(audioSampleRate_) + " Hz");
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] BMMQ::SdlAudioPreviewBuffer buildAudioPreview(const BMMQ::AudioStateView& state)
    {
        BMMQ::SdlAudioPreviewBuffer preview;
        preview.sampleRate = state.sampleRate != 0u ? static_cast<int>(state.sampleRate) : audioSampleRate_;
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
    int sourceAudioSampleRate_ = 48000;
    int audioSampleRate_ = 48000;
    double audioPhase_ = 0.0;
    uint64_t audioPreviewGeneration_ = 0;
    uint64_t lastQueuedAudioFrameCounter_ = 0;
    uint64_t lastQueuedAudioPreviewGeneration_ = 0;
    BMMQ::SdlAudioResampler audioResampler_{48000, 48000};
    AudioTransportCounters audioCounters_{};
    AudioTransportBuffer audioTransport_{};
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_AudioDeviceID audioDevice_ = 0;
#endif
    int textureWidth_ = 0;
    int textureHeight_ = 0;
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
