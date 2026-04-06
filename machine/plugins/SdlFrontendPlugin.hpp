#ifndef BMMQ_SDL_FRONTEND_PLUGIN_HPP
#define BMMQ_SDL_FRONTEND_PLUGIN_HPP

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

        std::string message = std::string("sdl: video event=") + detail::machineEventTypeName(event.type);
        if (lastVideoState_.has_value()) {
            message += " lcdc=" + detail::hexByte(lastVideoState_->lcdc);
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
    SdlFrontendConfig config_;
    SdlFrontendStats stats_;
    bool backendReady_ = false;
    std::optional<VideoStateView> lastVideoState_;
    std::optional<AudioStateView> lastAudioState_;
    std::optional<DigitalInputStateView> lastInputState_;
};

} // namespace BMMQ

#endif // BMMQ_SDL_FRONTEND_PLUGIN_HPP
