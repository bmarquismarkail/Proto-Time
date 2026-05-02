#include "machine/PngDecode.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

#include <zlib.h>

#include "machine/VisualPackLimits.hpp"

namespace BMMQ {
namespace {

[[nodiscard]] std::uint32_t readBigEndian32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u) |
           static_cast<std::uint32_t>(bytes[offset + 3u]);
}

[[nodiscard]] std::uint8_t paethPredictor(std::uint8_t left, std::uint8_t up, std::uint8_t upLeft) noexcept
{
    const int p = static_cast<int>(left) + static_cast<int>(up) - static_cast<int>(upLeft);
    const int pa = std::abs(p - static_cast<int>(left));
    const int pb = std::abs(p - static_cast<int>(up));
    const int pc = std::abs(p - static_cast<int>(upLeft));
    if (pa <= pb && pa <= pc) {
        return left;
    }
    if (pb <= pc) {
        return up;
    }
    return upLeft;
}

} // namespace

DecodeResult decodePngToRgba(std::span<const std::uint8_t> pngData) noexcept
{
    DecodeResult result;

    constexpr std::array<std::uint8_t, 8> kPngSignature{
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    };
    if (pngData.size() < kPngSignature.size() ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(), pngData.begin())) {
        result.error = "Invalid PNG signature";
        return result;
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bitDepth = 0;
    std::uint8_t colorType = 0;
    std::vector<std::uint8_t> idat;
    std::size_t offset = kPngSignature.size();

    while (offset + 12u <= pngData.size()) {
        const auto length = readBigEndian32(pngData, offset);
        offset += 4u;
        if (offset + 4u + length + 4u > pngData.size()) {
            result.error = "PNG chunk extends beyond file";
            return result;
        }

        const std::string type(reinterpret_cast<const char*>(pngData.data() + offset), 4u);
        offset += 4u;
        const auto dataOffset = offset;
        offset += length;
        offset += 4u; // Skip CRC.

        if (type == "IHDR") {
            if (length != 13u) {
                result.error = "Invalid IHDR length";
                return result;
            }
            width = readBigEndian32(pngData, dataOffset);
            height = readBigEndian32(pngData, dataOffset + 4u);
            bitDepth = pngData[dataOffset + 8u];
            colorType = pngData[dataOffset + 9u];
            const auto compression = pngData[dataOffset + 10u];
            const auto filter = pngData[dataOffset + 11u];
            const auto interlace = pngData[dataOffset + 12u];

            if (width == 0u || height == 0u) {
                result.error = "PNG has zero dimensions";
                return result;
            }
            if (width > VisualPackLimits::kMaxReplacementImageDimension ||
                height > VisualPackLimits::kMaxReplacementImageDimension) {
                result.error = "PNG dimensions exceed configured limits";
                return result;
            }
            if (bitDepth != 8u || (colorType != 2u && colorType != 6u) ||
                compression != 0u || filter != 0u || interlace != 0u) {
                result.error = "PNG format not supported (expected non-interlaced 8-bit RGB/RGBA)";
                return result;
            }
        } else if (type == "IDAT") {
            idat.insert(idat.end(),
                        pngData.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                        pngData.begin() + static_cast<std::ptrdiff_t>(dataOffset + length));
        } else if (type == "IEND") {
            break;
        }
    }

    if (width == 0u || height == 0u) {
        result.error = "PNG missing IHDR";
        return result;
    }
    if (idat.empty()) {
        result.error = "PNG missing IDAT data";
        return result;
    }

    const std::size_t channels = colorType == 6u ? 4u : 3u;
    const std::size_t stride = static_cast<std::size_t>(width) * channels;
    const std::size_t inflatedSize = (stride + 1u) * static_cast<std::size_t>(height);
    if (inflatedSize > VisualPackLimits::kMaxReplacementInflatedBytes) {
        result.error = "PNG inflated payload exceeds configured limits";
        return result;
    }

    std::vector<std::uint8_t> inflated(inflatedSize);
    auto destinationSize = static_cast<uLongf>(inflated.size());
    if (uncompress(inflated.data(), &destinationSize, idat.data(), static_cast<uLong>(idat.size())) != Z_OK ||
        destinationSize != inflated.size()) {
        result.error = "PNG inflate failed";
        return result;
    }

    std::vector<std::uint8_t> decoded(stride * static_cast<std::size_t>(height));
    for (std::size_t y = 0; y < height; ++y) {
        const auto filterType = inflated[y * (stride + 1u)];
        const auto* source = inflated.data() + y * (stride + 1u) + 1u;
        auto* output = decoded.data() + y * stride;
        const auto* previous = y == 0u ? nullptr : decoded.data() + (y - 1u) * stride;

        for (std::size_t x = 0; x < stride; ++x) {
            const auto left = x >= channels ? output[x - channels] : 0u;
            const auto up = previous != nullptr ? previous[x] : 0u;
            const auto upLeft = previous != nullptr && x >= channels ? previous[x - channels] : 0u;

            std::uint8_t predictor = 0;
            switch (filterType) {
            case 0:
                predictor = 0;
                break;
            case 1:
                predictor = left;
                break;
            case 2:
                predictor = up;
                break;
            case 3:
                predictor = static_cast<std::uint8_t>((static_cast<std::uint16_t>(left) + static_cast<std::uint16_t>(up)) / 2u);
                break;
            case 4:
                predictor = paethPredictor(left, up, upLeft);
                break;
            default:
                result.error = "PNG row uses unsupported filter type";
                return result;
            }

            output[x] = static_cast<std::uint8_t>(source[x] + predictor);
        }
    }

    DecodedImage image;
    image.width = width;
    image.height = height;
    image.hasPalette = false;
    image.hasAlpha = channels == 4u;

    const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.argbPixels.reserve(pixelCount);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const auto base = i * channels;
        const auto r = decoded[base];
        const auto g = decoded[base + 1u];
        const auto b = decoded[base + 2u];
        const auto a = channels == 4u ? decoded[base + 3u] : 0xFFu;
        image.argbPixels.push_back((static_cast<std::uint32_t>(a) << 24u) |
                                   (static_cast<std::uint32_t>(r) << 16u) |
                                   (static_cast<std::uint32_t>(g) << 8u) |
                                   static_cast<std::uint32_t>(b));
    }

    result.success = true;
    result.image = std::move(image);
    return result;
}

} // namespace BMMQ
