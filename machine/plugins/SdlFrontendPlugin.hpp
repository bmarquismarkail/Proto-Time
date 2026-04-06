#ifndef BMMQ_SDL_FRONTEND_PLUGIN_HPP
#define BMMQ_SDL_FRONTEND_PLUGIN_HPP

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "LoggingPlugins.hpp"

#if defined(__has_include)
#  if __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#    define BMMQ_SDL_FRONTEND_HAS_SDL2 1
#  elif __has_include(<SDL.h>)
#    include <SDL.h>
#    define BMMQ_SDL_FRONTEND_HAS_SDL2 1
#  else
#    define BMMQ_SDL_FRONTEND_HAS_SDL2 0
#  endif
#else
#  define BMMQ_SDL_FRONTEND_HAS_SDL2 0
#endif

#ifndef BMMQ_SDL_FRONTEND_LINKED
#  define BMMQ_SDL_FRONTEND_LINKED 0
#endif

namespace BMMQ {

struct SdlFrontendConfig {
    std::string windowTitle = "T.I.M.E. SDL Frontend";
    int windowScale = 2;
    int frameWidth = 160;
    int frameHeight = 144;
    int audioPreviewSampleCount = 128;
    bool enableVideo = true;
    bool enableAudio = true;
    bool enableInput = true;
    bool autoInitializeBackend = false;
    bool createHiddenWindowOnInitialize = false;
    bool pumpBackendEventsOnInputSample = true;
    bool autoPresentOnVideoEvent = true;
    bool showWindowOnPresent = false;
};

struct SdlFrontendStats {
    std::size_t attachCount = 0;
    std::size_t detachCount = 0;
    std::size_t videoEvents = 0;
    std::size_t audioEvents = 0;
    std::size_t inputEvents = 0;
    std::size_t inputPolls = 0;
    std::size_t inputSamplesProvided = 0;
    std::size_t framesPrepared = 0;
    std::size_t framesPresented = 0;
    std::size_t renderAttempts = 0;
    std::size_t audioPreviewsBuilt = 0;
    std::size_t backendInitAttempts = 0;
    std::size_t buttonTransitions = 0;
    std::size_t quitRequests = 0;
    std::size_t hostEventsHandled = 0;
    std::size_t keyEventsHandled = 0;
    std::size_t eventPumpCalls = 0;
    std::size_t backendEventsTranslated = 0;
    std::size_t serviceCalls = 0;
};

enum class SdlFrontendButton : uint8_t {
    Right = 0x01u,
    Left = 0x02u,
    Up = 0x04u,
    Down = 0x08u,
    A = 0x10u,
    B = 0x20u,
    Select = 0x40u,
    Start = 0x80u,
};

enum class SdlFrontendHostEventType : uint8_t {
    None = 0,
    KeyDown = 1,
    KeyUp = 2,
    Quit = 3,
};

enum class SdlFrontendHostKey : uint8_t {
    Unknown = 0,
    Right,
    Left,
    Up,
    Down,
    Z,
    X,
    Backspace,
    Return,
};

struct SdlFrontendHostEvent {
    SdlFrontendHostEventType type = SdlFrontendHostEventType::None;
    SdlFrontendHostKey key = SdlFrontendHostKey::Unknown;
    bool repeat = false;
};

struct SdlFrameBuffer {
    int width = 160;
    int height = 144;
    std::vector<uint32_t> pixels;

    [[nodiscard]] bool empty() const noexcept
    {
        return pixels.empty();
    }

    [[nodiscard]] std::size_t pixelCount() const noexcept
    {
        return pixels.size();
    }
};

struct SdlAudioPreviewBuffer {
    int sampleRate = 48000;
    int channels = 1;
    std::vector<int16_t> samples;

    [[nodiscard]] bool empty() const noexcept
    {
        return samples.empty();
    }

    [[nodiscard]] std::size_t sampleCount() const noexcept
    {
        return samples.size();
    }
};

class SdlFrontendPlugin final : public IVideoPlugin,
                                public IAudioPlugin,
                                public IDigitalInputPlugin,
                                public LoggingPluginSupport {
public:
    explicit SdlFrontendPlugin(SdlFrontendConfig config = {})
        : config_(std::move(config)) {}

    std::string_view id() const override
    {
        return "bmmq.frontend.sdl";
    }

    std::string_view displayName() const override
    {
        return "SDL Frontend Plugin";
    }

    [[nodiscard]] const SdlFrontendConfig& config() const noexcept
    {
        return config_;
    }

    [[nodiscard]] const SdlFrontendStats& stats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] const std::vector<std::string>& diagnostics() const noexcept
    {
        return entries();
    }

    [[nodiscard]] const std::optional<VideoStateView>& lastVideoState() const noexcept
    {
        return lastVideoState_;
    }

    [[nodiscard]] const std::optional<AudioStateView>& lastAudioState() const noexcept
    {
        return lastAudioState_;
    }

    [[nodiscard]] const std::optional<SdlAudioPreviewBuffer>& lastAudioPreview() const noexcept
    {
        return lastAudioPreview_;
    }

    [[nodiscard]] const std::optional<DigitalInputStateView>& lastInputState() const noexcept
    {
        return lastInputState_;
    }

    [[nodiscard]] const std::optional<SdlFrameBuffer>& lastFrame() const noexcept
    {
        return lastFrame_;
    }

    [[nodiscard]] std::string_view lastRenderSummary() const noexcept
    {
        return lastRenderSummary_;
    }

    [[nodiscard]] bool windowVisible() const noexcept
    {
        return windowVisible_;
    }

    [[nodiscard]] bool windowVisibilityRequested() const noexcept
    {
        return windowVisibilityRequested_;
    }

    void requestWindowVisibility(bool visible)
    {
        windowVisibilityRequested_ = visible;
        appendLog(std::string("sdl: window visibility requested=") + (visible ? "visible" : "hidden"));
    }

    bool serviceFrontend()
    {
        ++stats_.serviceCalls;
        std::size_t handledEvents = 0;
        if (!config_.pumpBackendEventsOnInputSample) {
            handledEvents = pumpBackendEvents();
        }

        const bool hadFrame = config_.enableVideo && lastFrame_.has_value();
        const bool hadAudioPreview = config_.enableAudio && lastAudioPreview_.has_value();
        const bool visibilityChanged = windowVisible_ != windowVisibilityRequested_;

        bool presented = false;
        if (hadFrame) {
            presented = presentLatestFrame();
        }

        applyWindowVisibilityRequest();
        return handledEvents != 0 || hadFrame || hadAudioPreview || visibilityChanged || presented || quitRequested_;
    }

    void setQueuedDigitalInputMask(uint32_t pressedMask)
    {
        queuedDigitalInputMask_ = pressedMask & 0x00FFu;
        appendLog("sdl: queued input mask=" + detail::hexByte(static_cast<uint8_t>(*queuedDigitalInputMask_)));
    }

    void clearQueuedDigitalInputMask()
    {
        queuedDigitalInputMask_.reset();
        appendLog("sdl: cleared queued input mask");
    }

    [[nodiscard]] std::optional<uint32_t> queuedDigitalInputMask() const noexcept
    {
        return queuedDigitalInputMask_;
    }

    void pressButton(SdlFrontendButton button)
    {
        setButtonState(button, true);
    }

    void releaseButton(SdlFrontendButton button)
    {
        setButtonState(button, false);
    }

    void setButtonState(SdlFrontendButton button, bool pressed)
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

    [[nodiscard]] bool isButtonPressed(SdlFrontendButton button) const noexcept
    {
        if (!queuedDigitalInputMask_.has_value()) {
            return false;
        }
        return ((*queuedDigitalInputMask_) & buttonMask(button)) != 0;
    }

    void requestQuit()
    {
        quitRequested_ = true;
        ++stats_.quitRequests;
        appendLog("sdl: quit requested");
    }

    void clearQuitRequest() noexcept
    {
        quitRequested_ = false;
    }

    [[nodiscard]] bool quitRequested() const noexcept
    {
        return quitRequested_;
    }

    [[nodiscard]] std::string_view lastHostEventSummary() const noexcept
    {
        return lastHostEventSummary_;
    }

    [[nodiscard]] std::string_view lastBackendError() const noexcept
    {
        return lastBackendError_;
    }

    [[nodiscard]] std::string backendStatusSummary() const
    {
        std::string summary = std::string(backendName());
        summary += backendReady_ ? " ready" : " not-ready";
        if (!lastBackendError_.empty()) {
            summary += " error=" + lastBackendError_;
        }
        if (!lastRenderSummary_.empty()) {
            summary += " render='" + lastRenderSummary_ + "'";
        }
        return summary;
    }

    bool presentLatestFrame()
    {
        ++stats_.renderAttempts;
        if (!lastFrame_.has_value()) {
            lastRenderSummary_ = "No frame available";
            return false;
        }

#if BMMQ_SDL_FRONTEND_HAS_SDL2 && BMMQ_SDL_FRONTEND_LINKED
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
        lastRenderSummary_ = "Presented frame " + std::to_string(lastFrame_->width) + "x" + std::to_string(lastFrame_->height);
        return true;
#else
        lastRenderSummary_ = "Frame prepared in skeleton mode";
        return false;
#endif
    }

    bool handleHostEvent(const SdlFrontendHostEvent& event)
    {
        ++stats_.hostEventsHandled;
        switch (event.type) {
        case SdlFrontendHostEventType::Quit:
            lastHostEventSummary_ = "Quit requested";
            requestQuit();
            return true;
        case SdlFrontendHostEventType::KeyDown:
        case SdlFrontendHostEventType::KeyUp: {
            if (event.repeat && event.type == SdlFrontendHostEventType::KeyDown) {
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
            const bool pressed = event.type == SdlFrontendHostEventType::KeyDown;
            setButtonState(*mapped, pressed);
            lastHostEventSummary_ = std::string(buttonName(*mapped)) + (pressed ? " pressed from host" : " released from host");
            return true;
        }
        case SdlFrontendHostEventType::None:
            break;
        }

        lastHostEventSummary_ = "No host event";
        return false;
    }

    [[nodiscard]] static constexpr bool compiledWithSdl() noexcept
    {
#if BMMQ_SDL_FRONTEND_HAS_SDL2
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] std::string_view backendName() const noexcept
    {
#if BMMQ_SDL_FRONTEND_HAS_SDL2
        return "SDL2 frontend";
#else
        return "SDL2 unavailable";
#endif
    }

    [[nodiscard]] bool backendReady() const noexcept
    {
        return backendReady_;
    }

    bool tryInitializeBackend()
    {
        ++stats_.backendInitAttempts;
        lastBackendError_.clear();

#if BMMQ_SDL_FRONTEND_HAS_SDL2 && BMMQ_SDL_FRONTEND_LINKED
        if (backendReady_) {
            appendLog("sdl: backend already initialized");
            return true;
        }

        uint32_t initFlags = SDL_INIT_EVENTS;
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

    std::size_t pumpBackendEvents()
    {
        ++stats_.eventPumpCalls;
#if BMMQ_SDL_FRONTEND_HAS_SDL2 && BMMQ_SDL_FRONTEND_LINKED
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

    void onAttach(const MachineView&) override
    {
        ++stats_.attachCount;
        appendLog("sdl: attached");
        if (config_.autoInitializeBackend) {
            tryInitializeBackend();
        }
    }

    void onDetach(const MachineView&) override
    {
        ++stats_.detachCount;
        shutdownBackend();
        windowVisible_ = false;
        appendLog("sdl: detached");
    }

    void onVideoEvent(const MachineEvent& event, const MachineView& view) override
    {
        if (!config_.enableVideo) {
            return;
        }
        ++stats_.videoEvents;
        lastVideoState_ = view.videoState();
        if (lastVideoState_.has_value()) {
            const bool shouldRefreshFrame =
                event.type == MachineEventType::VBlank ||
                !lastFrame_.has_value() ||
                !lastVideoState_->lcdEnabled();
            if (shouldRefreshFrame) {
                lastFrame_ = buildDebugFrame(*lastVideoState_);
                ++stats_.framesPrepared;
                if (config_.autoPresentOnVideoEvent) {
                    presentLatestFrame();
                }
            }
        } else {
            lastFrame_.reset();
        }

        std::string message = std::string("sdl: video event=") + detail::machineEventTypeName(event.type);
        if (lastVideoState_.has_value()) {
            message += " lcdc=" + detail::hexByte(lastVideoState_->lcdc);
        }
        if (lastFrame_.has_value()) {
            message += " frame=" + std::to_string(lastFrame_->width) + "x" + std::to_string(lastFrame_->height);
        }
        appendLog(std::move(message));
    }

    void onAudioEvent(const MachineEvent& event, const MachineView& view) override
    {
        if (!config_.enableAudio) {
            return;
        }
        ++stats_.audioEvents;
        lastAudioState_ = view.audioState();
        if (lastAudioState_.has_value()) {
            lastAudioPreview_ = buildAudioPreview(*lastAudioState_);
            ++stats_.audioPreviewsBuilt;
        } else {
            lastAudioPreview_.reset();
        }

        std::string message = std::string("sdl: audio event=") + detail::machineEventTypeName(event.type);
        if (lastAudioState_.has_value()) {
            message += " nr12=" + detail::hexByte(lastAudioState_->nr12);
        }
        if (lastAudioPreview_.has_value()) {
            message += " samples=" + std::to_string(lastAudioPreview_->sampleCount());
        }
        appendLog(std::move(message));
    }

    std::optional<uint32_t> sampleDigitalInput(const MachineView&) override
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

    void onDigitalInputEvent(const MachineEvent& event, const MachineView& view) override
    {
        if (!config_.enableInput) {
            return;
        }
        ++stats_.inputEvents;
        lastInputState_ = view.digitalInputState();

        std::string message = std::string("sdl: input event=") + detail::machineEventTypeName(event.type);
        if (lastInputState_.has_value()) {
            message += " pressed=" + detail::hexByte(lastInputState_->pressedMask);
        }
        appendLog(std::move(message));
    }

private:
    [[nodiscard]] static constexpr uint8_t buttonMask(SdlFrontendButton button) noexcept
    {
        return static_cast<uint8_t>(button);
    }

    [[nodiscard]] static constexpr std::string_view buttonName(SdlFrontendButton button) noexcept
    {
        switch (button) {
        case SdlFrontendButton::Right:
            return "Right";
        case SdlFrontendButton::Left:
            return "Left";
        case SdlFrontendButton::Up:
            return "Up";
        case SdlFrontendButton::Down:
            return "Down";
        case SdlFrontendButton::A:
            return "A";
        case SdlFrontendButton::B:
            return "B";
        case SdlFrontendButton::Select:
            return "Select";
        case SdlFrontendButton::Start:
            return "Start";
        }
        return "Unknown";
    }

    [[nodiscard]] static constexpr std::optional<SdlFrontendButton> mapHostKey(SdlFrontendHostKey key) noexcept
    {
        switch (key) {
        case SdlFrontendHostKey::Right:
            return SdlFrontendButton::Right;
        case SdlFrontendHostKey::Left:
            return SdlFrontendButton::Left;
        case SdlFrontendHostKey::Up:
            return SdlFrontendButton::Up;
        case SdlFrontendHostKey::Down:
            return SdlFrontendButton::Down;
        case SdlFrontendHostKey::Z:
            return SdlFrontendButton::A;
        case SdlFrontendHostKey::X:
            return SdlFrontendButton::B;
        case SdlFrontendHostKey::Backspace:
            return SdlFrontendButton::Select;
        case SdlFrontendHostKey::Return:
            return SdlFrontendButton::Start;
        case SdlFrontendHostKey::Unknown:
            break;
        }
        return std::nullopt;
    }

#if BMMQ_SDL_FRONTEND_HAS_SDL2 && BMMQ_SDL_FRONTEND_LINKED
    [[nodiscard]] static std::optional<SdlFrontendHostKey> mapSdlKeyCode(SDL_Keycode key) noexcept
    {
        switch (key) {
        case SDLK_RIGHT:
            return SdlFrontendHostKey::Right;
        case SDLK_LEFT:
            return SdlFrontendHostKey::Left;
        case SDLK_UP:
            return SdlFrontendHostKey::Up;
        case SDLK_DOWN:
            return SdlFrontendHostKey::Down;
        case SDLK_z:
            return SdlFrontendHostKey::Z;
        case SDLK_x:
            return SdlFrontendHostKey::X;
        case SDLK_BACKSPACE:
            return SdlFrontendHostKey::Backspace;
        case SDLK_RETURN:
            return SdlFrontendHostKey::Return;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::optional<SdlFrontendHostEvent> translateSdlEvent(const SDL_Event& event) noexcept
    {
        switch (event.type) {
        case SDL_QUIT:
            return SdlFrontendHostEvent{SdlFrontendHostEventType::Quit, SdlFrontendHostKey::Unknown, false};
        case SDL_KEYDOWN: {
            const auto key = mapSdlKeyCode(event.key.keysym.sym);
            if (!key.has_value()) {
                return std::nullopt;
            }
            return SdlFrontendHostEvent{SdlFrontendHostEventType::KeyDown, *key, event.key.repeat != 0};
        }
        case SDL_KEYUP: {
            const auto key = mapSdlKeyCode(event.key.keysym.sym);
            if (!key.has_value()) {
                return std::nullopt;
            }
            return SdlFrontendHostEvent{SdlFrontendHostEventType::KeyUp, *key, false};
        }
        default:
            return std::nullopt;
        }
    }
#endif

    void applyWindowVisibilityRequest() noexcept
    {
#if BMMQ_SDL_FRONTEND_HAS_SDL2 && BMMQ_SDL_FRONTEND_LINKED
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
#if BMMQ_SDL_FRONTEND_HAS_SDL2 && BMMQ_SDL_FRONTEND_LINKED
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

    [[nodiscard]] static uint8_t readVramByte(const VideoStateView& state, uint16_t address) noexcept
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

    [[nodiscard]] static uint8_t readOamByte(const VideoStateView& state, std::size_t index) noexcept
    {
        if (index >= state.oam.size()) {
            return 0xFFu;
        }
        return state.oam[index];
    }

    [[nodiscard]] static uint8_t sampleTileColorIndex(const VideoStateView& state,
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

    [[nodiscard]] static uint8_t backgroundColorIndex(const VideoStateView& state, int screenX, int screenY) noexcept
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

    void compositeSprites(SdlFrameBuffer& frame,
                          const VideoStateView& state,
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

    [[nodiscard]] SdlFrameBuffer buildDebugFrame(const VideoStateView& state) const
    {
        SdlFrameBuffer frame;
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

    [[nodiscard]] SdlAudioPreviewBuffer buildAudioPreview(const AudioStateView& state) const
    {
        SdlAudioPreviewBuffer preview;
        const auto sampleCount = std::max(config_.audioPreviewSampleCount, 8);
        preview.samples.reserve(static_cast<std::size_t>(sampleCount));

        const int volumeNibble = std::max(1, static_cast<int>((state.nr12 >> 4) & 0x0Fu));
        const int amplitude = volumeNibble * 1024;
        const int periodSeed = 8 + static_cast<int>(state.nr13) + (static_cast<int>(state.nr14 & 0x07u) << 8);
        for (int i = 0; i < sampleCount; ++i) {
            const int phase = (i + periodSeed) % 32;
            const int16_t sample = static_cast<int16_t>(phase < 16 ? amplitude : -amplitude);
            preview.samples.push_back(sample);
        }
        return preview;
    }

    SdlFrontendConfig config_;
    SdlFrontendStats stats_;
    bool backendReady_ = false;
    std::optional<VideoStateView> lastVideoState_;
    std::optional<AudioStateView> lastAudioState_;
    std::optional<SdlAudioPreviewBuffer> lastAudioPreview_;
    std::optional<DigitalInputStateView> lastInputState_;
    std::optional<SdlFrameBuffer> lastFrame_;
    std::optional<uint32_t> queuedDigitalInputMask_;
    bool quitRequested_ = false;
    bool windowVisible_ = false;
    bool windowVisibilityRequested_ = false;
    std::string lastHostEventSummary_;
    std::string lastRenderSummary_;
    std::string lastBackendError_;
    uint32_t initializedBackendFlags_ = 0;
#if BMMQ_SDL_FRONTEND_HAS_SDL2 && BMMQ_SDL_FRONTEND_LINKED
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
#endif
    int textureWidth_ = 0;
    int textureHeight_ = 0;
};

} // namespace BMMQ

#endif // BMMQ_SDL_FRONTEND_PLUGIN_HPP
