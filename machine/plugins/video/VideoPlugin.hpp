#ifndef BMMQ_VIDEO_PLUGIN_HPP
#define BMMQ_VIDEO_PLUGIN_HPP

#include <cstddef>
#include <cstdint>
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
    
    // Phase 39A: presenter present() latency percentiles (nanoseconds)
    std::int64_t presenterPresentDurationLastNanos = 0;
    std::int64_t presenterPresentDurationHighWaterNanos = 0;
    std::int64_t presenterPresentDurationP50Nanos = 0;
    std::int64_t presenterPresentDurationP95Nanos = 0;
    std::int64_t presenterPresentDurationP99Nanos = 0;
    std::int64_t presenterPresentDurationP999Nanos = 0;
    std::size_t presenterPresentDurationSampleCount = 0;
    
    // Duration histogram buckets
    std::size_t presenterPresentDurationUnder50usCount = 0;
    std::size_t presenterPresentDuration50To100usCount = 0;
    std::size_t presenterPresentDuration100To250usCount = 0;
    std::size_t presenterPresentDuration250To500usCount = 0;
    std::size_t presenterPresentDuration500usTo1msCount = 0;
    std::size_t presenterPresentDuration1To2msCount = 0;
    std::size_t presenterPresentDuration2To5msCount = 0;
    std::size_t presenterPresentDuration5To10msCount = 0;
    std::size_t presenterPresentDurationOver10msCount = 0;
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
