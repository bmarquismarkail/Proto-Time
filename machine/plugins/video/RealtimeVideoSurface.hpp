#ifndef BMMQ_REALTIME_VIDEO_SURFACE_HPP
#define BMMQ_REALTIME_VIDEO_SURFACE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace BMMQ {

enum class RealtimeVideoEncoding : std::uint8_t {
    Indexed2 = 2u,
    Indexed4 = 4u,
    Indexed5 = 5u,
    Indexed8 = 8u,
    Argb8888 = 32u,
};

// An owning, display-ready surface. Indexed encodings carry a complete palette
// and complete frame, so a latest-only mailbox may discard any older packet.
// Argb8888 is retained as a compatibility fallback for generic machines.
struct RealtimeVideoSurface {
    RealtimeVideoEncoding encoding = RealtimeVideoEncoding::Indexed8;
    std::uint16_t strideBytes = 0u;
    std::vector<std::uint32_t> paletteArgb;
    std::vector<std::uint8_t> indexedBytes;
    std::vector<std::uint32_t> argbPixels;

    [[nodiscard]] std::size_t payloadBytes() const noexcept
    {
        return paletteArgb.size() * sizeof(std::uint32_t) +
               indexedBytes.size() + argbPixels.size() * sizeof(std::uint32_t);
    }

    [[nodiscard]] bool validForDimensions(int width, int height) const noexcept
    {
        if (width <= 0 || height <= 0) {
            return false;
        }
        const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (encoding == RealtimeVideoEncoding::Argb8888) {
            return argbPixels.size() == pixelCount && indexedBytes.empty() && paletteArgb.empty();
        }

        const auto bitsPerPixel = static_cast<std::uint8_t>(encoding);
        if ((bitsPerPixel != 2u && bitsPerPixel != 4u && bitsPerPixel != 5u && bitsPerPixel != 8u) ||
            paletteArgb.empty() ||
            paletteArgb.size() > (std::size_t{1u} << bitsPerPixel)) {
            return false;
        }
        const auto minimumStride = (static_cast<std::size_t>(width) * bitsPerPixel + 7u) / 8u;
        return strideBytes >= minimumStride &&
               indexedBytes.size() == static_cast<std::size_t>(strideBytes) * static_cast<std::size_t>(height) &&
               argbPixels.empty();
    }
};

[[nodiscard]] inline RealtimeVideoSurface makeIndexedVideoSurface(
    std::span<const std::uint8_t> indices,
    int width,
    int height,
    RealtimeVideoEncoding encoding,
    std::span<const std::uint32_t> paletteArgb)
{
    RealtimeVideoSurface surface;
    surface.encoding = encoding;
    if (width <= 0 || height <= 0 ||
        indices.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return surface;
    }

    const auto bitsPerPixel = static_cast<std::uint8_t>(encoding);
    if ((bitsPerPixel != 2u && bitsPerPixel != 4u && bitsPerPixel != 5u && bitsPerPixel != 8u) ||
        paletteArgb.empty() || paletteArgb.size() > (std::size_t{1u} << bitsPerPixel)) {
        return surface;
    }

    surface.paletteArgb.assign(paletteArgb.begin(), paletteArgb.end());
    surface.strideBytes = static_cast<std::uint16_t>(
        (static_cast<std::size_t>(width) * bitsPerPixel + 7u) / 8u);
    surface.indexedBytes.assign(static_cast<std::size_t>(surface.strideBytes) *
                                    static_cast<std::size_t>(height),
                                0u);

    const auto mask = static_cast<std::uint8_t>((std::uint16_t{1u} << bitsPerPixel) - 1u);
    for (int y = 0; y < height; ++y) {
        const auto sourceRow = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        const auto destinationRow = static_cast<std::size_t>(y) * surface.strideBytes;
        std::size_t bitOffset = 0u;
        for (int x = 0; x < width; ++x) {
            const auto index = indices[sourceRow + static_cast<std::size_t>(x)];
            if (index >= surface.paletteArgb.size()) {
                return {};
            }
            const auto byteOffset = destinationRow + bitOffset / 8u;
            const auto shift = static_cast<unsigned>(bitOffset % 8u);
            const auto encoded = static_cast<std::uint16_t>((index & mask) << shift);
            surface.indexedBytes[byteOffset] |= static_cast<std::uint8_t>(encoded & 0xFFu);
            if (shift + bitsPerPixel > 8u) {
                surface.indexedBytes[byteOffset + 1u] |= static_cast<std::uint8_t>(encoded >> 8u);
            }
            bitOffset += bitsPerPixel;
        }
    }
    return surface;
}

[[nodiscard]] inline RealtimeVideoSurface makeArgbVideoSurface(std::vector<std::uint32_t> pixels,
                                                                int width,
                                                                int height)
{
    RealtimeVideoSurface surface;
    surface.encoding = RealtimeVideoEncoding::Argb8888;
    surface.strideBytes = width > 0 ? static_cast<std::uint16_t>(width * sizeof(std::uint32_t)) : 0u;
    if (width > 0 && height > 0 &&
        pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        surface.argbPixels = std::move(pixels);
    }
    return surface;
}

[[nodiscard]] inline bool decodeVideoSurface(const RealtimeVideoSurface& surface,
                                             int width,
                                             int height,
                                             std::vector<std::uint32_t>& destination)
{
    if (!surface.validForDimensions(width, height)) {
        destination.clear();
        return false;
    }
    if (surface.encoding == RealtimeVideoEncoding::Argb8888) {
        destination = surface.argbPixels;
        return true;
    }

    const auto bitsPerPixel = static_cast<std::uint8_t>(surface.encoding);
    const auto mask = static_cast<std::uint16_t>((std::uint16_t{1u} << bitsPerPixel) - 1u);
    destination.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const auto sourceRow = static_cast<std::size_t>(y) * surface.strideBytes;
        const auto destinationRow = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        std::size_t bitOffset = 0u;
        for (int x = 0; x < width; ++x) {
            const auto byteOffset = sourceRow + bitOffset / 8u;
            const auto shift = static_cast<unsigned>(bitOffset % 8u);
            std::uint16_t encoded = surface.indexedBytes[byteOffset];
            if (shift + bitsPerPixel > 8u) {
                encoded |= static_cast<std::uint16_t>(surface.indexedBytes[byteOffset + 1u]) << 8u;
            }
            const auto paletteIndex = static_cast<std::size_t>((encoded >> shift) & mask);
            if (paletteIndex >= surface.paletteArgb.size()) {
                destination.clear();
                return false;
            }
            destination[destinationRow + static_cast<std::size_t>(x)] = surface.paletteArgb[paletteIndex];
            bitOffset += bitsPerPixel;
        }
    }
    return true;
}

// Decode into caller-owned storage. Presenters use this overload to expand an
// indexed surface directly into a locked host texture without allocating an
// intermediate ARGB framebuffer.
[[nodiscard]] inline bool decodeVideoSurfaceToArgb(const RealtimeVideoSurface& surface,
                                                   int width,
                                                   int height,
                                                   std::uint32_t* destination,
                                                   std::size_t destinationStridePixels) noexcept
{
    if (destination == nullptr ||
        destinationStridePixels < static_cast<std::size_t>(std::max(width, 0)) ||
        !surface.validForDimensions(width, height)) {
        return false;
    }

    if (surface.encoding == RealtimeVideoEncoding::Argb8888) {
        for (int y = 0; y < height; ++y) {
            const auto rowOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
            std::copy_n(surface.argbPixels.data() + rowOffset,
                        static_cast<std::size_t>(width),
                        destination + static_cast<std::size_t>(y) * destinationStridePixels);
        }
        return true;
    }

    const auto bitsPerPixel = static_cast<std::uint8_t>(surface.encoding);
    const auto mask = static_cast<std::uint16_t>((std::uint16_t{1u} << bitsPerPixel) - 1u);
    for (int y = 0; y < height; ++y) {
        const auto sourceRow = static_cast<std::size_t>(y) * surface.strideBytes;
        auto* destinationRow = destination + static_cast<std::size_t>(y) * destinationStridePixels;
        std::size_t bitOffset = 0u;
        for (int x = 0; x < width; ++x) {
            const auto byteOffset = sourceRow + bitOffset / 8u;
            const auto shift = static_cast<unsigned>(bitOffset % 8u);
            std::uint16_t encoded = surface.indexedBytes[byteOffset];
            if (shift + bitsPerPixel > 8u) {
                encoded |= static_cast<std::uint16_t>(surface.indexedBytes[byteOffset + 1u]) << 8u;
            }
            const auto paletteIndex = static_cast<std::size_t>((encoded >> shift) & mask);
            if (paletteIndex >= surface.paletteArgb.size()) {
                return false;
            }
            destinationRow[static_cast<std::size_t>(x)] = surface.paletteArgb[paletteIndex];
            bitOffset += bitsPerPixel;
        }
    }
    return true;
}

} // namespace BMMQ

#endif // BMMQ_REALTIME_VIDEO_SURFACE_HPP
