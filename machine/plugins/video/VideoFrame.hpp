#ifndef BMMQ_VIDEO_FRAME_HPP
#define BMMQ_VIDEO_FRAME_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BMMQ {

enum class VideoFrameFormat : uint8_t {
    Argb8888 = 0,
};

enum class VideoFrameSource : uint8_t {
    MachineSnapshot = 0,
    RealtimeSnapshot = 1,
    BlankFallback = 2,
    LastValidFallback = 3,
};

enum class VideoPresenterMode : uint8_t {
    Auto = 0,
    Hardware = 1,
    Software = 2,
};

struct VideoFramePacket {
    int width = 160;
    int height = 144;
    VideoFrameFormat format = VideoFrameFormat::Argb8888;
    VideoFrameSource source = VideoFrameSource::MachineSnapshot;
    uint64_t generation = 0;
    uint64_t lifecycleEpoch = 1;
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

struct VideoPresentPacket {
    int width = 160;
    int height = 144;
    VideoFrameFormat format = VideoFrameFormat::Argb8888;
    VideoFrameSource source = VideoFrameSource::MachineSnapshot;
    uint64_t generation = 0;
    uint64_t lifecycleEpoch = 1;
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

struct VideoPresenterConfig {
    std::string windowTitle = "T.I.M.E. SDL Frontend";
    int scale = 2;
    int frameWidth = 160;
    int frameHeight = 144;
    VideoPresenterMode mode = VideoPresenterMode::Auto;
    bool createHiddenWindowOnOpen = true;
    bool showWindowOnPresent = false;
};

[[nodiscard]] inline VideoFramePacket makeBlankVideoFrame(int width, int height, uint64_t generation)
{
    VideoFramePacket frame;
    frame.width = std::max(width, 1);
    frame.height = std::max(height, 1);
    frame.generation = generation;
    frame.source = VideoFrameSource::BlankFallback;
    frame.pixels.assign(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height), 0xFF000000u);
    return frame;
}

[[nodiscard]] inline VideoPresentPacket makePresentPacket(VideoFramePacket frame)
{
    VideoPresentPacket present;
    present.width = frame.width;
    present.height = frame.height;
    present.format = frame.format;
    present.source = frame.source;
    present.generation = frame.generation;
    present.lifecycleEpoch = frame.lifecycleEpoch;
    present.pixels = std::move(frame.pixels);
    return present;
}

[[nodiscard]] inline VideoFramePacket makeFramePacket(VideoPresentPacket packet)
{
    VideoFramePacket frame;
    frame.width = packet.width;
    frame.height = packet.height;
    frame.format = packet.format;
    frame.source = packet.source;
    frame.generation = packet.generation;
    frame.lifecycleEpoch = packet.lifecycleEpoch;
    frame.pixels = std::move(packet.pixels);
    return frame;
}

} // namespace BMMQ

#endif // BMMQ_VIDEO_FRAME_HPP
