#include "SdlVideoPresenter.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

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

namespace {

[[nodiscard]] int clampScaledDimension(int dimension, int scale) noexcept
{
    const auto scaled = static_cast<long long>(dimension) * static_cast<long long>(scale);
    const auto clamped = std::clamp(
        scaled,
        1LL,
        static_cast<long long>(std::numeric_limits<int>::max())
    );
    return static_cast<int>(clamped);
}

} // namespace

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
    diagnostics_ = {};
    diagnostics_.configuredMode = config_.mode;
    diagnostics_.activeMode = config_.mode;
    rendererNameStorage_.clear();
    diagnostics_.rendererName = rendererNameStorage_;

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

    const int width = clampScaledDimension(config_.frameWidth, config_.scale);
    const int height = clampScaledDimension(config_.frameHeight, config_.scale);
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

    if (!ensureRenderer(frame.width, frame.height)) {
        return false;
    }

    if (!ensureTexture(frame.width, frame.height)) {
        return false;
    }

    if (SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), frame.width * static_cast<int>(sizeof(uint32_t))) != 0) {
        const auto updateError = std::string(SDL_GetError());
        if (!fallbackToSoftwareRenderer(frame.width, frame.height, VideoPresenterFallbackReason::RuntimePresentFailure)) {
            lastError_ = updateError;
            return false;
        }
        if (SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), frame.width * static_cast<int>(sizeof(uint32_t))) != 0) {
            lastError_ = SDL_GetError();
            return false;
        }
    }
    ++diagnostics_.textureUploadCount;

    if (SDL_RenderClear(renderer_) != 0) {
        const auto clearError = std::string(SDL_GetError());
        if (!fallbackToSoftwareRenderer(frame.width, frame.height, VideoPresenterFallbackReason::RuntimePresentFailure)) {
            lastError_ = clearError;
            return false;
        }
        if (SDL_RenderClear(renderer_) != 0) {
            lastError_ = SDL_GetError();
            return false;
        }
    }
    if (SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0) {
        const auto copyError = std::string(SDL_GetError());
        if (!fallbackToSoftwareRenderer(frame.width, frame.height, VideoPresenterFallbackReason::RuntimePresentFailure)) {
            lastError_ = copyError;
            return false;
        }
        if (SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0) {
            lastError_ = SDL_GetError();
            return false;
        }
    }
    SDL_RenderPresent(renderer_);
    ++diagnostics_.presentCount;
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

VideoPresenterDiagnostics SdlVideoPresenter::diagnostics() const noexcept
{
    return diagnostics_;
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

bool SdlVideoPresenter::ensureRenderer(int frameWidth, int frameHeight) noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    if (renderer_ != nullptr) {
        return true;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    const auto assignRendererMetadata = [this]() {
        SDL_RendererInfo rendererInfo{};
        if (SDL_GetRendererInfo(renderer_, &rendererInfo) == 0 && rendererInfo.name != nullptr) {
            rendererNameStorage_ = rendererInfo.name;
        } else {
            rendererNameStorage_.clear();
        }
        diagnostics_.rendererName = rendererNameStorage_;
    };

    auto createSoftwareRenderer = [&]() -> bool {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (renderer_ == nullptr) {
            lastError_ = SDL_GetError();
            return false;
        }
        diagnostics_.activeMode = VideoPresenterMode::Software;
        SDL_RenderSetLogicalSize(renderer_, frameWidth, frameHeight);
        assignRendererMetadata();
        return true;
    };

    if (config_.mode == VideoPresenterMode::Software) {
        return createSoftwareRenderer();
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ != nullptr) {
        diagnostics_.activeMode = VideoPresenterMode::Hardware;
        SDL_RenderSetLogicalSize(renderer_, frameWidth, frameHeight);
        assignRendererMetadata();
        return true;
    }

    if (!createSoftwareRenderer()) {
        return false;
    }
    diagnostics_.usedSoftwareFallback = true;
    ++diagnostics_.softwareFallbackCount;
    diagnostics_.lastFallbackReason = VideoPresenterFallbackReason::HardwareRendererUnavailable;
    return true;
#else
    (void)frameWidth;
    (void)frameHeight;
    return false;
#endif
}

bool SdlVideoPresenter::fallbackToSoftwareRenderer(int frameWidth,
                                                   int frameHeight,
                                                   VideoPresenterFallbackReason reason) noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    if (window_ == nullptr) {
        return false;
    }
    if (renderer_ != nullptr && diagnostics_.activeMode == VideoPresenterMode::Software) {
        return false;
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

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (renderer_ == nullptr) {
        lastError_ = SDL_GetError();
        return false;
    }
    diagnostics_.activeMode = VideoPresenterMode::Software;
    diagnostics_.usedSoftwareFallback = true;
    ++diagnostics_.softwareFallbackCount;
    diagnostics_.lastFallbackReason = reason;
    SDL_RenderSetLogicalSize(renderer_, frameWidth, frameHeight);

    SDL_RendererInfo rendererInfo{};
    if (SDL_GetRendererInfo(renderer_, &rendererInfo) == 0 && rendererInfo.name != nullptr) {
        rendererNameStorage_ = rendererInfo.name;
    } else {
        rendererNameStorage_.clear();
    }
    diagnostics_.rendererName = rendererNameStorage_;
    return ensureTexture(frameWidth, frameHeight);
#else
    (void)frameWidth;
    (void)frameHeight;
    (void)reason;
    return false;
#endif
}

bool SdlVideoPresenter::ensureTexture(int frameWidth, int frameHeight) noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    if (texture_ != nullptr && textureWidth_ == frameWidth && textureHeight_ == frameHeight) {
        return true;
    }
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    SDL_RenderSetLogicalSize(renderer_, frameWidth, frameHeight);
    texture_ = SDL_CreateTexture(renderer_,
                                 SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 frameWidth,
                                 frameHeight);
    if (texture_ == nullptr) {
        lastError_ = SDL_GetError();
        return false;
    }
    textureWidth_ = frameWidth;
    textureHeight_ = frameHeight;
    ++diagnostics_.textureRecreateCount;
    return true;
#else
    (void)frameWidth;
    (void)frameHeight;
    return false;
#endif
}

} // namespace BMMQ
