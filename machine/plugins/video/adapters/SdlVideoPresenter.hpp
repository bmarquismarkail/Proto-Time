#ifndef BMMQ_SDL_VIDEO_PRESENTER_HPP
#define BMMQ_SDL_VIDEO_PRESENTER_HPP

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "../VideoPlugin.hpp"

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
#endif

namespace BMMQ {

class SdlVideoPresenter final : public IVideoPresenterPlugin {
public:
    SdlVideoPresenter() = default;
    ~SdlVideoPresenter() override;
    SdlVideoPresenter(const SdlVideoPresenter&) = delete;
    SdlVideoPresenter& operator=(const SdlVideoPresenter&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] VideoPluginCapabilities capabilities() const noexcept override;
    bool open(const VideoPresenterConfig& config) override;
    void close() noexcept override;
    [[nodiscard]] bool ready() const noexcept override;
    bool present(const VideoFramePacket& frame) noexcept override;
    [[nodiscard]] std::string_view lastError() const noexcept override;
    [[nodiscard]] VideoPresenterDiagnostics diagnostics() const noexcept override;
    [[nodiscard]] bool windowVisible() const noexcept;
    void requestWindowVisibility(bool visible) noexcept;
    [[nodiscard]] bool windowVisibilityRequested() const noexcept;

private:
    bool ensureRenderer(int frameWidth, int frameHeight) noexcept;
    bool ensureTexture(int frameWidth, int frameHeight) noexcept;
    bool fallbackToSoftwareRenderer(int frameWidth, int frameHeight,
                                    VideoPresenterFallbackReason reason) noexcept;

    VideoPresenterConfig config_{};
    std::string lastError_{};
    // diagMutex_ serialises writes from the render thread (present/ensureRenderer/
    // fallbackToSoftwareRenderer) against reads from the emulation thread
    // (syncEngineDiagnostics -> diagnostics()). All diagnostics_ mutations that
    // occur inside present() are covered by the lock held for present()'s
    // entire SDL block; diagnostics() takes the same lock before copying.
    mutable std::mutex diagMutex_;
    VideoPresenterDiagnostics diagnostics_{};
    std::string rendererNameStorage_{};
    bool ready_ = false;
    // windowVisible_ and windowVisibilityRequested_ are read by SdlFrontendPlugin::windowVisible()
    // (render thread) without holding sharedStateMutex_, and written by close() (main thread).
    // Use atomic to prevent TSAN data races.
    std::atomic<bool> windowVisible_{false};
    std::atomic<bool> windowVisibilityRequested_{false};
    uint32_t initializedBackendFlags_ = 0;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    ::SDL_Window* window_ = nullptr;
    ::SDL_Renderer* renderer_ = nullptr;
    ::SDL_Texture* texture_ = nullptr;
#endif
};

} // namespace BMMQ

#endif // BMMQ_SDL_VIDEO_PRESENTER_HPP
