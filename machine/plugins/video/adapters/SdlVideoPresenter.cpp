#include "SdlVideoPresenter.hpp"

#include <algorithm>
#include <cstdint>

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

namespace BMMQ {

SdlVideoPresenter::~SdlVideoPresenter()
{
    close();
}

std::string_view SdlVideoPresenter::name() const noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    return "SDL2 video";
#else
    return "SDL2 video unavailable";
#endif
}

VideoPluginCapabilities SdlVideoPresenter::capabilities() const noexcept
{
    return {
        .realtimeSafe = false,
        .frameSizePreserving = true,
        .snapshotAware = true,
        .deterministic = false,
        .headlessSafe = false,
        .requiresHostThreadAffinity = true,
    };
}

bool SdlVideoPresenter::open(const VideoPresenterConfig& config)
{
    config_ = config;
    config_.frameWidth = std::max(config_.frameWidth, 1);
    config_.frameHeight = std::max(config_.frameHeight, 1);
    config_.scale = std::max(config_.scale, 1);
    lastError_.clear();

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    if (ready_) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        lastError_ = SDL_GetError();
        ready_ = false;
        return false;
    }
    initializedBackendFlags_ = SDL_INIT_VIDEO | SDL_INIT_EVENTS;

    const int width = std::max(config_.frameWidth * config_.scale, 1);
    const int height = std::max(config_.frameHeight * config_.scale, 1);
    const uint32_t windowFlags = config_.createHiddenWindowOnOpen ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    window_ = SDL_CreateWindow(config_.windowTitle.c_str(),
                               SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED,
                               width,
                               height,
                               windowFlags);
    if (window_ == nullptr) {
        lastError_ = SDL_GetError();
        close();
        return false;
    }
    windowVisible_ = !config_.createHiddenWindowOnOpen;
    windowVisibilityRequested_ = windowVisible_;
    ready_ = true;
    return true;
#else
    lastError_ = "SDL2 video backend unavailable";
    ready_ = false;
    return false;
#endif
}

void SdlVideoPresenter::close() noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
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
    ready_ = false;
    windowVisible_ = false;
}

bool SdlVideoPresenter::ready() const noexcept
{
    return ready_;
}

bool SdlVideoPresenter::present(const VideoFramePacket& frame) noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    if (!ready_ || window_ == nullptr) {
        lastError_ = "video presenter not ready";
        return false;
    }
    if (frame.empty()) {
        lastError_ = "empty video frame";
        return false;
    }

    if (config_.showWindowOnPresent) {
        requestWindowVisibility(true);
    }
    if (windowVisibilityRequested_ != windowVisible_) {
        if (windowVisibilityRequested_) {
            SDL_ShowWindow(window_);
        } else {
            SDL_HideWindow(window_);
        }
        windowVisible_ = windowVisibilityRequested_;
    }

    if (renderer_ == nullptr) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (renderer_ == nullptr) {
            lastError_ = SDL_GetError();
            return false;
        }
        SDL_RenderSetLogicalSize(renderer_, frame.width, frame.height);
    }

    if (texture_ == nullptr || textureWidth_ != frame.width || textureHeight_ != frame.height) {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        texture_ = SDL_CreateTexture(renderer_,
                                     SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     frame.width,
                                     frame.height);
        if (texture_ == nullptr) {
            lastError_ = SDL_GetError();
            return false;
        }
        textureWidth_ = frame.width;
        textureHeight_ = frame.height;
    }

    if (SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), frame.width * static_cast<int>(sizeof(uint32_t))) != 0) {
        lastError_ = SDL_GetError();
        return false;
    }

    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
    lastError_.clear();
    return true;
#else
    (void)frame;
    lastError_ = "SDL2 video backend unavailable";
    return false;
#endif
}

std::string_view SdlVideoPresenter::lastError() const noexcept
{
    return lastError_;
}

bool SdlVideoPresenter::windowVisible() const noexcept
{
    return windowVisible_;
}

void SdlVideoPresenter::requestWindowVisibility(bool visible) noexcept
{
    windowVisibilityRequested_ = visible;
}

bool SdlVideoPresenter::windowVisibilityRequested() const noexcept
{
    return windowVisibilityRequested_;
}

} // namespace BMMQ
