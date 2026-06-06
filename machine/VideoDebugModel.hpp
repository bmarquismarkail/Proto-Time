#ifndef BMMQ_VIDEO_DEBUG_MODEL_HPP
#define BMMQ_VIDEO_DEBUG_MODEL_HPP

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "VisualTypes.hpp"

namespace BMMQ {

struct VideoDebugRenderRequest {
    int frameWidth = 160;
    int frameHeight = 144;
};

struct VideoDebugPixelSemantic {
    static constexpr std::uint32_t kNoResourceIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t resourceIndex = kNoResourceIndex;
    std::uint8_t sampleX = 0;
    std::uint8_t sampleY = 0;

    [[nodiscard]] bool hasResource() const noexcept
    {
        return resourceIndex != kNoResourceIndex;
    }
};

struct VideoDebugFrameModel {
    int width = 0;
    int height = 0;
    bool displayEnabled = false;
    bool inVBlank = false;
    std::optional<std::uint16_t> scanlineIndex;
    std::vector<std::uint32_t> argbPixels;
    std::vector<VideoDebugPixelSemantic> semantics;
    std::vector<DecodedVisualResource> resources;

    [[nodiscard]] bool empty() const noexcept
    {
        return width <= 0 ||
               height <= 0 ||
               argbPixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    [[nodiscard]] std::size_t pixelCount() const noexcept
    {
        return argbPixels.size();
    }
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_DEBUG_MODEL_HPP
