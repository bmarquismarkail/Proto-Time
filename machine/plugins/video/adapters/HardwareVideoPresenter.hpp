#ifndef BMMQ_HARDWARE_VIDEO_PRESENTER_HPP
#define BMMQ_HARDWARE_VIDEO_PRESENTER_HPP

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

class HardwareVideoPresenter final : public IVideoPresenterPlugin {
public:
    HardwareVideoPresenter() = default;
    ~HardwareVideoPresenter() override;
    HardwareVideoPresenter(const HardwareVideoPresenter&) = delete;
    HardwareVideoPresenter& operator=(const HardwareVideoPresenter&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] VideoPluginCapabilities capabilities() const noexcept override;
    bool open(const VideoPresenterConfig& config) override;
    void close() noexcept override;
    [[nodiscard]] bool ready() const noexcept override;
    bool present(const VideoFramePacket& frame) noexcept override;
    [[nodiscard]] std::string_view lastError() const noexcept override;
    [[nodiscard]] VideoPresenterDiagnostics diagnostics() const noexcept override;
    [[nodiscard]] bool windowVisible() const noexcept override;
    void requestWindowVisibility(bool visible) noexcept override;
    [[nodiscard]] bool windowVisibilityRequested() const noexcept override;

private:
    bool ensureRenderer(int frameWidth, int frameHeight) noexcept;
    bool ensureTextures(int frameWidth, int frameHeight) noexcept;
    bool uploadLockedTexture(const VideoFramePacket& frame,
                             std::int64_t& expansionDurationNanos) noexcept;
    bool fallbackToSoftwareRenderer(int frameWidth, int frameHeight,
                                    VideoPresenterFallbackReason reason) noexcept;
    void updatePresentDurationMetric(std::int64_t durationNanos) noexcept;
    void updateStageDurationMetrics(std::int64_t expansionDurationNanos,
                                    std::int64_t uploadDurationNanos,
                                    std::int64_t renderSubmitDurationNanos,
                                    std::int64_t totalDurationNanos) noexcept;

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
    std::atomic<bool> windowVisible_{false};
    std::atomic<bool> windowVisibilityRequested_{false};
    uint32_t initializedBackendFlags_ = 0;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
    std::uint32_t rendererFlags_ = 0;
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    ::SDL_Window* window_ = nullptr;
    ::SDL_Renderer* renderer_ = nullptr;
    // uploadTexture_ receives CPU-produced frame pixels.
    ::SDL_Texture* uploadTexture_ = nullptr;
#endif
};

} // namespace BMMQ

#endif // BMMQ_HARDWARE_VIDEO_PRESENTER_HPP
