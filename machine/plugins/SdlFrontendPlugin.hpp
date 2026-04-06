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

namespace BMMQ {

struct SdlFrontendConfig {
    std::string windowTitle = "T.I.M.E. SDL Frontend";
    int windowScale = 2;
    int frameWidth = 160;
    int frameHeight = 144;
    bool enableVideo = true;
    bool enableAudio = true;
    bool enableInput = true;
    bool autoInitializeBackend = false;
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
    std::size_t backendInitAttempts = 0;
    std::size_t buttonTransitions = 0;
    std::size_t quitRequests = 0;
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
        return "SDL Frontend Plugin (Skeleton)";
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

    [[nodiscard]] const std::optional<DigitalInputStateView>& lastInputState() const noexcept
    {
        return lastInputState_;
    }

    [[nodiscard]] const std::optional<SdlFrameBuffer>& lastFrame() const noexcept
    {
        return lastFrame_;
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
        return "SDL2 (skeleton)";
#else
        return "SDL2 unavailable (skeleton mode)";
#endif
    }

    [[nodiscard]] bool backendReady() const noexcept
    {
        return backendReady_;
    }

    bool tryInitializeBackend()
    {
        ++stats_.backendInitAttempts;
#if BMMQ_SDL_FRONTEND_HAS_SDL2
        appendLog("sdl: SDL2 detected; backend initialization is not implemented in this skeleton yet");
#else
        appendLog("sdl: SDL2 headers not found; running in skeleton mode");
#endif
        backendReady_ = false;
        return backendReady_;
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
        backendReady_ = false;
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
            lastFrame_ = buildDebugFrame(*lastVideoState_);
            ++stats_.framesPrepared;
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

        std::string message = std::string("sdl: audio event=") + detail::machineEventTypeName(event.type);
        if (lastAudioState_.has_value()) {
            message += " nr12=" + detail::hexByte(lastAudioState_->nr12);
        }
        appendLog(std::move(message));
    }

    std::optional<uint32_t> sampleDigitalInput(const MachineView&) override
    {
        if (!config_.enableInput) {
            return std::nullopt;
        }
        ++stats_.inputPolls;
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

        for (std::size_t i = 0; i < pixelCount; ++i) {
            const uint8_t source = state.vram[i % state.vram.size()];
            const uint8_t shift = static_cast<uint8_t>((i & 0x03u) * 2u);
            const uint8_t shade = static_cast<uint8_t>((source >> shift) & 0x03u);
            frame.pixels[i] = paletteColor(shade);
        }
        return frame;
    }

    SdlFrontendConfig config_;
    SdlFrontendStats stats_;
    bool backendReady_ = false;
    std::optional<VideoStateView> lastVideoState_;
    std::optional<AudioStateView> lastAudioState_;
    std::optional<DigitalInputStateView> lastInputState_;
    std::optional<SdlFrameBuffer> lastFrame_;
    std::optional<uint32_t> queuedDigitalInputMask_;
    bool quitRequested_ = false;
};

} // namespace BMMQ

#endif // BMMQ_SDL_FRONTEND_PLUGIN_HPP
