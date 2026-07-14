#ifndef BMMQ_PACKED_VIDEO_PIXELS_HPP
#define BMMQ_PACKED_VIDEO_PIXELS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

namespace BMMQ {

// Self-contained and lossless: every packet can be decoded independently, so
// latest-only mailbox overwrites never invalidate a later frame.
struct PackedVideoPixels {
    std::uint8_t bitsPerPixel = 0;
    std::vector<std::uint32_t> paletteArgb;
    std::vector<std::uint8_t> indices;

    [[nodiscard]] std::size_t payloadBytes() const noexcept
    {
        return paletteArgb.size() * sizeof(std::uint32_t) + indices.size();
    }

    [[nodiscard]] bool validForPixelCount(std::size_t pixelCount) const noexcept
    {
        if (pixelCount == 0u || bitsPerPixel == 0u || bitsPerPixel > 8u || paletteArgb.empty()) {
            return false;
        }
        if (paletteArgb.size() > (std::size_t{1u} << bitsPerPixel) ||
            pixelCount > (std::numeric_limits<std::size_t>::max() - 7u) / bitsPerPixel) {
            return false;
        }
        return indices.size() == (pixelCount * bitsPerPixel + 7u) / 8u;
    }
};

[[nodiscard]] inline PackedVideoPixels packVideoPixels(const std::vector<std::uint32_t>& argbPixels)
{
    PackedVideoPixels packed;
    if (argbPixels.empty()) {
        return packed;
    }

    packed.paletteArgb.reserve(32u);
    for (const auto pixel : argbPixels) {
        if (std::find(packed.paletteArgb.begin(), packed.paletteArgb.end(), pixel) != packed.paletteArgb.end()) {
            continue;
        }
        if (packed.paletteArgb.size() == 256u) {
            return {};
        }
        packed.paletteArgb.push_back(pixel);
    }

    std::size_t capacity = 2u;
    packed.bitsPerPixel = 1u;
    while (capacity < packed.paletteArgb.size()) {
        capacity <<= 1u;
        ++packed.bitsPerPixel;
    }

    packed.indices.assign((argbPixels.size() * packed.bitsPerPixel + 7u) / 8u, 0u);
    std::size_t bitOffset = 0u;
    for (const auto pixel : argbPixels) {
        const auto it = std::find(packed.paletteArgb.begin(), packed.paletteArgb.end(), pixel);
        const auto paletteIndex = static_cast<std::uint16_t>(std::distance(packed.paletteArgb.begin(), it));
        const auto byteOffset = bitOffset / 8u;
        const auto shift = static_cast<unsigned>(bitOffset % 8u);
        const auto encoded = static_cast<std::uint16_t>(paletteIndex << shift);
        packed.indices[byteOffset] |= static_cast<std::uint8_t>(encoded & 0xFFu);
        if (shift + packed.bitsPerPixel > 8u) {
            packed.indices[byteOffset + 1u] |= static_cast<std::uint8_t>(encoded >> 8u);
        }
        bitOffset += packed.bitsPerPixel;
    }
    return packed;
}

[[nodiscard]] inline bool unpackVideoPixels(const PackedVideoPixels& packed,
                                            std::size_t pixelCount,
                                            std::vector<std::uint32_t>& destination)
{
    if (!packed.validForPixelCount(pixelCount)) {
        destination.clear();
        return false;
    }

    destination.resize(pixelCount);
    const auto mask = static_cast<std::uint16_t>((std::uint16_t{1u} << packed.bitsPerPixel) - 1u);
    std::size_t bitOffset = 0u;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const auto byteOffset = bitOffset / 8u;
        const auto shift = static_cast<unsigned>(bitOffset % 8u);
        std::uint16_t encoded = packed.indices[byteOffset];
        if (shift + packed.bitsPerPixel > 8u) {
            encoded |= static_cast<std::uint16_t>(packed.indices[byteOffset + 1u]) << 8u;
        }
        const auto paletteIndex = static_cast<std::size_t>((encoded >> shift) & mask);
        if (paletteIndex >= packed.paletteArgb.size()) {
            destination.clear();
            return false;
        }
        destination[i] = packed.paletteArgb[paletteIndex];
        bitOffset += packed.bitsPerPixel;
    }
    return true;
}

} // namespace BMMQ

#endif // BMMQ_PACKED_VIDEO_PIXELS_HPP
