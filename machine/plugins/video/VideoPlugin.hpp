#ifndef BMMQ_VIDEO_PLUGIN_HPP
#define BMMQ_VIDEO_PLUGIN_HPP

#include <cstddef>
#include <string_view>

#include "VideoCapabilities.hpp"
#include "VideoFrame.hpp"

namespace BMMQ {

enum class VideoPresenterFallbackReason : uint8_t {
    None = 0,
    HardwareRendererUnavailable,
    RuntimePresentFailure,
};

struct VideoPresenterDiagnostics {
    VideoPresenterMode configuredMode = VideoPresenterMode::Auto;
    VideoPresenterMode activeMode = VideoPresenterMode::Auto;
    bool usedSoftwareFallback = false;
    std::size_t softwareFallbackCount = 0;
    VideoPresenterFallbackReason lastFallbackReason = VideoPresenterFallbackReason::None;
    std::size_t textureRecreateCount = 0;
    std::size_t textureUploadCount = 0;
    std::size_t presentCount = 0;
    std::string_view rendererName{};
};

class IVideoPresenterPlugin {
public:
    virtual ~IVideoPresenterPlugin() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual VideoPluginCapabilities capabilities() const noexcept
    {
        return {};
    }

    virtual bool open(const VideoPresenterConfig& config) = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool ready() const noexcept = 0;
    virtual bool present(const VideoFramePacket& frame) noexcept = 0;
    [[nodiscard]] virtual std::string_view lastError() const noexcept = 0;
    [[nodiscard]] virtual VideoPresenterDiagnostics diagnostics() const noexcept
    {
        return {};
    }
};

class IVideoFrameProcessorPlugin {
public:
    virtual ~IVideoFrameProcessorPlugin() = default;

    [[nodiscard]] virtual VideoPluginCapabilities capabilities() const noexcept
    {
        return {};
    }

    virtual bool process(const VideoFramePacket& input,
                         VideoFramePacket& output) noexcept = 0;
};

class IVideoCapturePlugin {
public:
    virtual ~IVideoCapturePlugin() = default;

    [[nodiscard]] virtual VideoPluginCapabilities capabilities() const noexcept
    {
        return {};
    }

    virtual bool capture(const VideoFramePacket& frame) noexcept = 0;
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_PLUGIN_HPP
