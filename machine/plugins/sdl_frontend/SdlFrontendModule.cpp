#include "machine/plugins/abi/TimePluginAbi.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

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

class SdlFrontend final {
public:
    SdlFrontend(const TimeFrontendHostApiV1& host, const TimeFrontendConfigV1& config)
        : host_(host), title_(config.window_title != nullptr ? config.window_title : "Proto-Time"),
          scale_(std::max(config.window_scale, 1u)),
          frameWidth_(std::max(config.frame_width, 1)),
          frameHeight_(std::max(config.frame_height, 1)), flags_(config.flags)
    {
        stats_.struct_size = sizeof(stats_);
    }

    ~SdlFrontend() { shutdown(); }

    bool initialize()
    {
        if (ready_) return true;
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            fail(SDL_GetError());
            return false;
        }
        initializedFlags_ = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
        if ((flags_ & TIME_FRONTEND_CONFIG_ENABLE_VIDEO_V1) != 0u) {
            const auto width = frameWidth_ * static_cast<int>(scale_);
            const auto height = frameHeight_ * static_cast<int>(scale_);
            const auto windowFlags = (flags_ & TIME_FRONTEND_CONFIG_CREATE_HIDDEN_V1) != 0u
                ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
            window_ = SDL_CreateWindow(title_.c_str(), SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, width, height,
                                       static_cast<Uint32>(windowFlags));
            if (window_ == nullptr) {
                fail(SDL_GetError());
                shutdown();
                return false;
            }
            renderer_ = SDL_CreateRenderer(window_, -1,
                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (renderer_ == nullptr) {
                renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
            }
            if (renderer_ == nullptr) {
                fail(SDL_GetError());
                shutdown();
                return false;
            }
            SDL_RendererInfo info{};
            if (SDL_GetRendererInfo(renderer_, &info) == 0) {
                rendererName_ = info.name != nullptr ? info.name : "SDL2 renderer";
                if ((info.flags & SDL_RENDERER_ACCELERATED) != 0u) {
                    stats_.renderer_flags |= TIME_FRONTEND_RENDERER_ACCELERATED_V1;
                }
                if ((info.flags & SDL_RENDERER_PRESENTVSYNC) != 0u) {
                    stats_.renderer_flags |= TIME_FRONTEND_RENDERER_VSYNC_V1;
                }
                if ((info.flags & SDL_RENDERER_SOFTWARE) != 0u) {
                    stats_.renderer_flags |= TIME_FRONTEND_RENDERER_SOFTWARE_V1;
                }
            }
            windowVisible_ = (windowFlags & SDL_WINDOW_SHOWN) != 0u;
        }
        ready_ = true;
        stats_.backend_ready = 1;
        stats_.window_visible = windowVisible_ ? 1 : 0;
        log("SDL frontend initialized");
        return true;
#else
        fail("SDL2 frontend backend unavailable");
        return false;
#endif
    }

    void shutdown() noexcept
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (initializedFlags_ != 0u) {
            SDL_QuitSubSystem(initializedFlags_);
            initializedFlags_ = 0u;
        }
#endif
        ready_ = false;
        windowVisible_ = false;
        stats_.backend_ready = 0;
        stats_.window_visible = 0;
    }

    bool service()
    {
        ++stats_.service_calls;
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (!ready_) return false;
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            ++stats_.events_processed;
            if (event.type == SDL_QUIT) {
                stats_.quit_requested = 1;
                if (host_.request_quit != nullptr) host_.request_quit(host_.host_context);
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                inputMask_ = 0u;
                publishInput();
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                handleKey(event.key.keysym.sym, event.type == SDL_KEYDOWN, event.key.repeat != 0);
            }
        }
        return true;
#else
        return false;
#endif
    }

    bool present(const TimeFrontendFrameV1& frame)
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (!ready_ || renderer_ == nullptr || frame.pixels == nullptr ||
            frame.pixel_format != TIME_FRONTEND_PIXEL_ARGB8888_V1 ||
            frame.width <= 0 || frame.height <= 0) {
            ++stats_.present_failures;
            return false;
        }
        const auto started = std::chrono::steady_clock::now();
        if (!ensureTexture(frame.width, frame.height)) {
            ++stats_.present_failures;
            return false;
        }
        if (SDL_UpdateTexture(texture_, nullptr, frame.pixels,
                              static_cast<int>(frame.row_stride_bytes)) != 0) {
            fail(SDL_GetError());
            ++stats_.present_failures;
            return false;
        }
        ++stats_.texture_upload_count;
        if (SDL_RenderClear(renderer_) != 0 ||
            SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0) {
            fail(SDL_GetError());
            ++stats_.present_failures;
            return false;
        }
        SDL_RenderPresent(renderer_);
        if ((flags_ & TIME_FRONTEND_CONFIG_SHOW_ON_PRESENT_V1) != 0u && !windowVisible_) {
            SDL_ShowWindow(window_);
            windowVisible_ = true;
            stats_.window_visible = 1;
        }
        ++stats_.frames_presented;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        stats_.present_duration_last_ns = elapsed;
        stats_.present_duration_high_water_ns =
            std::max(stats_.present_duration_high_water_ns, elapsed);
        return true;
#else
        (void)frame;
        ++stats_.present_failures;
        return false;
#endif
    }

    void setVisible(bool visible)
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (window_ != nullptr) {
            if (visible) SDL_ShowWindow(window_); else SDL_HideWindow(window_);
        }
#endif
        windowVisible_ = visible;
        stats_.window_visible = visible ? 1 : 0;
    }

    const char* backendName() const noexcept
    {
        return rendererName_.empty() ? "SDL2 frontend" : rendererName_.c_str();
    }
    const char* lastError() const noexcept { return lastError_.c_str(); }
    TimeFrontendStatsV1 stats() const noexcept { return stats_; }

private:
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    bool ensureTexture(int width, int height)
    {
        if (texture_ != nullptr && textureWidth_ == width && textureHeight_ == height) return true;
        if (texture_ != nullptr) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING, width, height);
        if (texture_ == nullptr) {
            fail(SDL_GetError());
            return false;
        }
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE);
        textureWidth_ = width;
        textureHeight_ = height;
        ++stats_.texture_recreate_count;
        return true;
    }

    static uint32_t buttonForKey(SDL_Keycode key) noexcept
    {
        switch (key) {
        case SDLK_RIGHT: return TIME_FRONTEND_INPUT_RIGHT_V1;
        case SDLK_LEFT: return TIME_FRONTEND_INPUT_LEFT_V1;
        case SDLK_UP: return TIME_FRONTEND_INPUT_UP_V1;
        case SDLK_DOWN: return TIME_FRONTEND_INPUT_DOWN_V1;
        case SDLK_z: return TIME_FRONTEND_INPUT_BUTTON1_V1;
        case SDLK_x: return TIME_FRONTEND_INPUT_BUTTON2_V1;
        case SDLK_BACKSPACE: return TIME_FRONTEND_INPUT_META1_V1;
        case SDLK_RETURN: return TIME_FRONTEND_INPUT_META2_V1;
        default: return 0u;
        }
    }

    void handleKey(SDL_Keycode key, bool pressed, bool repeat)
    {
        const auto button = buttonForKey(key);
        if (button != 0u) {
            if (pressed) inputMask_ |= button; else inputMask_ &= ~button;
            publishInput();
            return;
        }
        if (!pressed || repeat || host_.request_control == nullptr) return;
        uint32_t action = 0u;
        if (key == SDLK_p) action = TIME_FRONTEND_CONTROL_TOGGLE_PAUSE_V1;
        else if (key == SDLK_o) action = TIME_FRONTEND_CONTROL_TOGGLE_THROTTLE_V1;
        else if (key == SDLK_n) action = TIME_FRONTEND_CONTROL_SINGLE_STEP_V1;
        else if (key == SDLK_RIGHTBRACKET) action = TIME_FRONTEND_CONTROL_SPEED_UP_V1;
        else if (key == SDLK_LEFTBRACKET) action = TIME_FRONTEND_CONTROL_SPEED_DOWN_V1;
        if (action != 0u) host_.request_control(host_.host_context, action);
    }
#endif

    void publishInput() const
    {
        if (host_.publish_digital_input != nullptr) {
            host_.publish_digital_input(host_.host_context, inputMask_);
        }
    }
    void log(const char* message) const
    {
        if (host_.log_message != nullptr) host_.log_message(host_.host_context, 1u, message);
    }
    void fail(const char* message)
    {
        lastError_ = message != nullptr ? message : "unknown SDL frontend error";
        log(lastError_.c_str());
    }

    TimeFrontendHostApiV1 host_{};
    std::string title_;
    uint32_t scale_ = 1u;
    int frameWidth_ = 160;
    int frameHeight_ = 144;
    uint32_t flags_ = 0u;
    uint32_t inputMask_ = 0u;
    bool ready_ = false;
    bool windowVisible_ = false;
    std::string rendererName_;
    std::string lastError_;
    TimeFrontendStatsV1 stats_{};
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    Uint32 initializedFlags_ = 0u;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
#endif
};

void* createFrontend(const TimeFrontendHostApiV1* host, const TimeFrontendConfigV1* config)
{
    if (host == nullptr || config == nullptr ||
        host->struct_size < sizeof(TimeFrontendHostApiV1) ||
        host->abi_version != TIME_PLUGIN_ABI_VERSION_V1 ||
        config->struct_size < sizeof(TimeFrontendConfigV1)) return nullptr;
    try { return new SdlFrontend(*host, *config); } catch (...) { return nullptr; }
}
void destroyFrontend(void* instance) { delete static_cast<SdlFrontend*>(instance); }
int32_t initializeFrontend(void* instance)
{
    try { return instance != nullptr && static_cast<SdlFrontend*>(instance)->initialize(); }
    catch (...) { return 0; }
}
void shutdownFrontend(void* instance)
{
    try { if (instance != nullptr) static_cast<SdlFrontend*>(instance)->shutdown(); }
    catch (...) {}
}
int32_t serviceFrontend(void* instance)
{
    try { return instance != nullptr && static_cast<SdlFrontend*>(instance)->service(); }
    catch (...) { return 0; }
}
int32_t presentFrontend(void* instance, const TimeFrontendFrameV1* frame)
{
    try {
        return instance != nullptr && frame != nullptr &&
            frame->struct_size >= sizeof(TimeFrontendFrameV1) &&
            static_cast<SdlFrontend*>(instance)->present(*frame);
    } catch (...) { return 0; }
}
void setVisible(void* instance, int32_t visible)
{
    try { if (instance != nullptr) static_cast<SdlFrontend*>(instance)->setVisible(visible != 0); }
    catch (...) {}
}
const char* backendName(const void* instance)
{
    return instance != nullptr ? static_cast<const SdlFrontend*>(instance)->backendName() : "";
}
const char* lastError(const void* instance)
{
    return instance != nullptr ? static_cast<const SdlFrontend*>(instance)->lastError() : "";
}
int32_t queryStats(const void* instance, TimeFrontendStatsV1* stats)
{
    if (stats == nullptr || stats->struct_size < sizeof(TimeFrontendStatsV1)) return 0;
    if (instance == nullptr) return 0;
    *stats = static_cast<const SdlFrontend*>(instance)->stats();
    return 1;
}

const TimeFrontendApiV1 frontendApi{
    sizeof(TimeFrontendApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    TIME_FRONTEND_CAPABILITY_VIDEO_V1 | TIME_FRONTEND_CAPABILITY_DIGITAL_INPUT_V1 |
        TIME_FRONTEND_CAPABILITY_WINDOW_V1,
    &createFrontend, &destroyFrontend, &initializeFrontend, &shutdownFrontend,
    &serviceFrontend, &presentFrontend, &setVisible, &backendName, &lastError, &queryStats};

const TimePluginDescriptorV1 frontendDescriptor{
    sizeof(TimePluginDescriptorV1), TIME_PLUGIN_KIND_FRONTEND_V1,
    "bmmq.frontend.sdl", "SDL Frontend Plugin", &frontendApi, sizeof(frontendApi)};

const TimePluginDescriptorV1* pluginAt(uint32_t index)
{
    return index == 0u ? &frontendDescriptor : nullptr;
}

const TimePluginModuleV1 moduleDescriptor{
    sizeof(TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "bmmq.module.sdl-frontend", "Proto-Time SDL Frontend Module", 1u, &pluginAt};

} // namespace

extern "C" TIME_PLUGIN_EXPORT const TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &moduleDescriptor;
}
