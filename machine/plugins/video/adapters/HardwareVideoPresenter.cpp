#include "HardwareVideoPresenter.hpp"

#include <algorithm>
#include <cstdint>
#include <chrono>
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

HardwareVideoPresenter::~HardwareVideoPresenter()
{
    close();
}

std::string_view HardwareVideoPresenter::name() const noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    return "SDL2 hardware";
#else
    return "SDL2 hardware unavailable";
#endif
}

VideoPluginCapabilities HardwareVideoPresenter::capabilities() const noexcept
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

bool HardwareVideoPresenter::open(const VideoPresenterConfig& config)
{
    config_ = config;
    config_.frameWidth = std::max(config_.frameWidth, 1);
    config_.frameHeight = std::max(config_.frameHeight, 1);
    config_.scale = std::max(config_.scale, 1);
    lastError_.clear();
    {
        std::lock_guard<std::mutex> lk(diagMutex_);
        diagnostics_ = {};
        diagnostics_.configuredMode = config_.mode;
        diagnostics_.activeMode = VideoPresenterMode::Hardware;
        rendererNameStorage_.clear();
        diagnostics_.rendererName = rendererNameStorage_;
    }

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
    windowVisible_.store(!config_.createHiddenWindowOnOpen, std::memory_order_relaxed);
    windowVisibilityRequested_.store(windowVisible_.load(std::memory_order_relaxed), std::memory_order_relaxed);

    // Ensure a renderer is available before the first frame upload.
    if (!ensureRenderer(config_.frameWidth, config_.frameHeight)) {
        close();
        return false;
    }

    ready_ = true;
    return true;
#else
    lastError_ = "SDL2 video backend unavailable";
    ready_ = false;
    return false;
#endif
}

void HardwareVideoPresenter::close() noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    if (uploadTexture_ != nullptr) {
        SDL_DestroyTexture(uploadTexture_);
        uploadTexture_ = nullptr;
    }
    if (renderTarget_ != nullptr) {
        SDL_DestroyTexture(renderTarget_);
        renderTarget_ = nullptr;
    }
    textureWidth_ = 0;
    textureHeight_ = 0;
    renderTargetAvailable_ = false;
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
    windowVisible_.store(false, std::memory_order_release);
}

bool HardwareVideoPresenter::ready() const noexcept
{
    return ready_;
}

bool HardwareVideoPresenter::present(const VideoFramePacket& frame) noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    // diagMutex_ covers all diagnostics_ mutations in this function and its
    // callees (ensureRenderer, ensureTextures, fallbackToSoftwareRenderer).
    std::lock_guard<std::mutex> diagLock(diagMutex_);
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
    if (windowVisibilityRequested_.load(std::memory_order_relaxed) != windowVisible_.load(std::memory_order_relaxed)) {
        if (windowVisibilityRequested_.load(std::memory_order_relaxed)) {
            SDL_ShowWindow(window_);
        } else {
            SDL_HideWindow(window_);
        }
        windowVisible_.store(windowVisibilityRequested_.load(std::memory_order_relaxed), std::memory_order_release);
    }

    if (!ensureRenderer(frame.width, frame.height)) {
        return false;
    }

    if (!ensureTextures(frame.width, frame.height)) {
        return false;
    }

    if (SDL_UpdateTexture(uploadTexture_, nullptr, frame.pixels.data(),
                          frame.width * static_cast<int>(sizeof(uint32_t))) != 0) {
        const auto updateError = std::string(SDL_GetError());
        if (!fallbackToSoftwareRenderer(frame.width, frame.height, VideoPresenterFallbackReason::RuntimePresentFailure) ||
            !ensureTextures(frame.width, frame.height) ||
            SDL_UpdateTexture(uploadTexture_, nullptr, frame.pixels.data(),
                              frame.width * static_cast<int>(sizeof(uint32_t))) != 0) {
            lastError_ = updateError;
            return false;
        }
    }
    ++diagnostics_.textureUploadCount;

    SDL_Texture* presentTexture = uploadTexture_;
    if (renderTargetAvailable_ && renderTarget_ != nullptr) {
        if (SDL_SetRenderTarget(renderer_, renderTarget_) != 0 ||
            SDL_RenderClear(renderer_) != 0 ||
            SDL_RenderCopy(renderer_, uploadTexture_, nullptr, nullptr) != 0 ||
            SDL_SetRenderTarget(renderer_, nullptr) != 0) {
            const auto targetError = std::string(SDL_GetError());
            if (!fallbackToSoftwareRenderer(frame.width, frame.height, VideoPresenterFallbackReason::RuntimePresentFailure) ||
                !ensureTextures(frame.width, frame.height) ||
                SDL_UpdateTexture(uploadTexture_, nullptr, frame.pixels.data(),
                                  frame.width * static_cast<int>(sizeof(uint32_t))) != 0) {
                lastError_ = targetError;
                return false;
            }
            presentTexture = uploadTexture_;
        } else {
            presentTexture = renderTarget_;
        }
    }

    if (SDL_RenderClear(renderer_) != 0) {
        const auto clearError = std::string(SDL_GetError());
        if (!fallbackToSoftwareRenderer(frame.width, frame.height, VideoPresenterFallbackReason::RuntimePresentFailure) ||
            !ensureTextures(frame.width, frame.height) ||
            SDL_UpdateTexture(uploadTexture_, nullptr, frame.pixels.data(),
                              frame.width * static_cast<int>(sizeof(uint32_t))) != 0 ||
            SDL_RenderClear(renderer_) != 0) {
            lastError_ = clearError;
            return false;
        }
        presentTexture = uploadTexture_;
    }
    if (SDL_RenderCopy(renderer_, presentTexture, nullptr, nullptr) != 0) {
        const auto copyError = std::string(SDL_GetError());
        if (!fallbackToSoftwareRenderer(frame.width, frame.height, VideoPresenterFallbackReason::RuntimePresentFailure) ||
            !ensureTextures(frame.width, frame.height) ||
            SDL_UpdateTexture(uploadTexture_, nullptr, frame.pixels.data(),
                              frame.width * static_cast<int>(sizeof(uint32_t))) != 0 ||
            SDL_RenderCopy(renderer_, uploadTexture_, nullptr, nullptr) != 0) {
            lastError_ = copyError;
            return false;
        }
    }

    const auto presentStart = std::chrono::high_resolution_clock::now();
    SDL_RenderPresent(renderer_);
    const auto presentEnd = std::chrono::high_resolution_clock::now();
    const auto presentDurationNanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(presentEnd - presentStart).count();
    updatePresentDurationMetric(presentDurationNanos);
    ++diagnostics_.presentCount;
    lastError_.clear();
    return true;
#else
    (void)frame;
    lastError_ = "SDL2 video backend unavailable";
    return false;
#endif
}

std::string_view HardwareVideoPresenter::lastError() const noexcept
{
    return lastError_;
}

VideoPresenterDiagnostics HardwareVideoPresenter::diagnostics() const noexcept
{
    std::lock_guard<std::mutex> lk(diagMutex_);
    return diagnostics_;
}

bool HardwareVideoPresenter::windowVisible() const noexcept
{
    return windowVisible_.load(std::memory_order_acquire);
}

void HardwareVideoPresenter::requestWindowVisibility(bool visible) noexcept
{
    windowVisibilityRequested_.store(visible, std::memory_order_relaxed);
}

bool HardwareVideoPresenter::windowVisibilityRequested() const noexcept
{
    return windowVisibilityRequested_.load(std::memory_order_relaxed);
}

bool HardwareVideoPresenter::ensureRenderer(int frameWidth, int frameHeight) noexcept
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

    auto createHardwareRenderer = [&](uint32_t flags) -> bool {
        renderer_ = SDL_CreateRenderer(window_, -1, static_cast<int>(flags));
        if (renderer_ == nullptr) {
            return false;
        }
        diagnostics_.activeMode = VideoPresenterMode::Hardware;
        SDL_RenderSetLogicalSize(renderer_, frameWidth, frameHeight);
        assignRendererMetadata();
        return true;
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

    // Always prefer hardware-accelerated renderer
    if (createHardwareRenderer(SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)) {
        return true;
    }
    if (createHardwareRenderer(SDL_RENDERER_ACCELERATED)) {
        return true;
    }

    // Fall back to software
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

bool HardwareVideoPresenter::ensureTextures(int frameWidth, int frameHeight) noexcept
{
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    if (uploadTexture_ != nullptr && textureWidth_ == frameWidth && textureHeight_ == frameHeight) {
        return true;
    }
    if (uploadTexture_ != nullptr) {
        SDL_DestroyTexture(uploadTexture_);
        uploadTexture_ = nullptr;
    }
    if (renderTarget_ != nullptr) {
        SDL_DestroyTexture(renderTarget_);
        renderTarget_ = nullptr;
    }
    renderTargetAvailable_ = false;
    SDL_RenderSetLogicalSize(renderer_, frameWidth, frameHeight);

    uploadTexture_ = SDL_CreateTexture(renderer_,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       frameWidth,
                                       frameHeight);
    if (uploadTexture_ == nullptr) {
        lastError_ = SDL_GetError();
        return false;
    }

    if (diagnostics_.activeMode == VideoPresenterMode::Hardware) {
        renderTarget_ = SDL_CreateTexture(renderer_,
                                          SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_TARGET,
                                          frameWidth,
                                          frameHeight);
        renderTargetAvailable_ = renderTarget_ != nullptr;
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

bool HardwareVideoPresenter::fallbackToSoftwareRenderer(int frameWidth,
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
    if (uploadTexture_ != nullptr) {
        SDL_DestroyTexture(uploadTexture_);
        uploadTexture_ = nullptr;
    }
    if (renderTarget_ != nullptr) {
        SDL_DestroyTexture(renderTarget_);
        renderTarget_ = nullptr;
    }
    textureWidth_ = 0;
    textureHeight_ = 0;
    renderTargetAvailable_ = false;
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
    return true;
#else
    (void)frameWidth;
    (void)frameHeight;
    (void)reason;
    return false;
#endif
}

void HardwareVideoPresenter::updatePresentDurationMetric(std::int64_t durationNanos) noexcept
{
    diagnostics_.presenterPresentDurationLastNanos = durationNanos;
    diagnostics_.presenterPresentDurationHighWaterNanos =
        std::max(diagnostics_.presenterPresentDurationHighWaterNanos, durationNanos);

    constexpr std::int64_t us50 = 50'000;
    constexpr std::int64_t us100 = 100'000;
    constexpr std::int64_t us250 = 250'000;
    constexpr std::int64_t us500 = 500'000;
    constexpr std::int64_t ms1 = 1'000'000;
    constexpr std::int64_t ms2 = 2'000'000;
    constexpr std::int64_t ms5 = 5'000'000;
    constexpr std::int64_t ms10 = 10'000'000;

    if (durationNanos < us50) {
        ++diagnostics_.presenterPresentDurationUnder50usCount;
    } else if (durationNanos < us100) {
        ++diagnostics_.presenterPresentDuration50To100usCount;
    } else if (durationNanos < us250) {
        ++diagnostics_.presenterPresentDuration100To250usCount;
    } else if (durationNanos < us500) {
        ++diagnostics_.presenterPresentDuration250To500usCount;
    } else if (durationNanos < ms1) {
        ++diagnostics_.presenterPresentDuration500usTo1msCount;
    } else if (durationNanos < ms2) {
        ++diagnostics_.presenterPresentDuration1To2msCount;
    } else if (durationNanos < ms5) {
        ++diagnostics_.presenterPresentDuration2To5msCount;
    } else if (durationNanos < ms10) {
        ++diagnostics_.presenterPresentDuration5To10msCount;
    } else {
        ++diagnostics_.presenterPresentDurationOver10msCount;
    }

    const auto sampleCount = ++diagnostics_.presenterPresentDurationSampleCount;
    const auto targetP50 = (sampleCount + 1u) / 2u;
    const auto targetP95 = (sampleCount * 95u + 99u) / 100u;
    const auto targetP99 = (sampleCount * 99u + 99u) / 100u;
    const auto targetP999 = (sampleCount * 999u + 999u) / 1000u;

    const auto estimateAtRank = [&](std::size_t rank) -> std::int64_t {
        std::size_t cumulative = 0u;
        cumulative += diagnostics_.presenterPresentDurationUnder50usCount;
        if (rank <= cumulative) {
            return us50;
        }
        cumulative += diagnostics_.presenterPresentDuration50To100usCount;
        if (rank <= cumulative) {
            return us100;
        }
        cumulative += diagnostics_.presenterPresentDuration100To250usCount;
        if (rank <= cumulative) {
            return us250;
        }
        cumulative += diagnostics_.presenterPresentDuration250To500usCount;
        if (rank <= cumulative) {
            return us500;
        }
        cumulative += diagnostics_.presenterPresentDuration500usTo1msCount;
        if (rank <= cumulative) {
            return ms1;
        }
        cumulative += diagnostics_.presenterPresentDuration1To2msCount;
        if (rank <= cumulative) {
            return ms2;
        }
        cumulative += diagnostics_.presenterPresentDuration2To5msCount;
        if (rank <= cumulative) {
            return ms5;
        }
        cumulative += diagnostics_.presenterPresentDuration5To10msCount;
        if (rank <= cumulative) {
            return ms10;
        }
        return std::max(ms10, diagnostics_.presenterPresentDurationHighWaterNanos);
    };

    diagnostics_.presenterPresentDurationP50Nanos = estimateAtRank(targetP50);
    diagnostics_.presenterPresentDurationP95Nanos = estimateAtRank(targetP95);
    diagnostics_.presenterPresentDurationP99Nanos = estimateAtRank(targetP99);
    diagnostics_.presenterPresentDurationP999Nanos = estimateAtRank(targetP999);
}

} // namespace BMMQ
