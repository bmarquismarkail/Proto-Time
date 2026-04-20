#ifndef BMMQ_VISUAL_TYPES_HPP
#define BMMQ_VISUAL_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace BMMQ {

enum class VisualResourceKind : uint8_t {
    Unknown = 0,
    Tile,
    Sprite,
    BackgroundTile,
};

enum class VisualPixelFormat : uint8_t {
    Unknown = 0,
    Indexed2,
    Rgba8888,
};

using VisualResourceHash = uint64_t;

namespace detail {

inline constexpr VisualResourceHash kFnvOffset = 14695981039346656037ull;
inline constexpr VisualResourceHash kFnvPrime = 1099511628211ull;

inline void mixVisualHashByte(VisualResourceHash& hash, uint8_t value) noexcept
{
    hash ^= static_cast<VisualResourceHash>(value);
    hash *= kFnvPrime;
}

} // namespace detail

struct VisualSourceMetadata {
    uint32_t bank = 0;
    uint32_t index = 0;
    uint32_t address = 0;
    uint32_t paletteValue = 0;
    std::string paletteRegister;
    std::string label;
};

struct VisualResourceDescriptor {
    std::string machineId;
    VisualResourceKind kind = VisualResourceKind::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    VisualPixelFormat decodedFormat = VisualPixelFormat::Unknown;
    VisualResourceHash sourceHash = 0;
    VisualResourceHash contentHash = 0;
    VisualResourceHash paletteHash = 0;
    VisualResourceHash paletteAwareHash = 0;
    VisualSourceMetadata source;
};

struct DecodedVisualResource {
    VisualResourceDescriptor descriptor;
    std::vector<uint8_t> pixels;
    uint32_t stride = 0;
};

struct VisualReplacementImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> argbPixels;

    [[nodiscard]] bool empty() const noexcept
    {
        return argbPixels.empty() || width == 0u || height == 0u;
    }
};

struct ResolvedVisualOverride {
    std::string packId;
    std::string assetPath;
    std::string scalePolicy;
    std::string filterPolicy;
    std::string anchor;
    VisualReplacementImage image;
};

[[nodiscard]] inline constexpr const char* visualResourceKindName(VisualResourceKind kind) noexcept
{
    switch (kind) {
    case VisualResourceKind::Tile:
        return "Tile";
    case VisualResourceKind::Sprite:
        return "Sprite";
    case VisualResourceKind::BackgroundTile:
        return "BackgroundTile";
    case VisualResourceKind::Unknown:
        break;
    }
    return "Unknown";
}

[[nodiscard]] inline constexpr VisualResourceKind visualResourceKindFromString(std::string_view value) noexcept
{
    if (value == "Tile" || value == "tile") {
        return VisualResourceKind::Tile;
    }
    if (value == "Sprite" || value == "sprite") {
        return VisualResourceKind::Sprite;
    }
    if (value == "BackgroundTile" || value == "background_tile") {
        return VisualResourceKind::BackgroundTile;
    }
    return VisualResourceKind::Unknown;
}

[[nodiscard]] inline constexpr const char* visualPixelFormatName(VisualPixelFormat format) noexcept
{
    switch (format) {
    case VisualPixelFormat::Indexed2:
        return "Indexed2";
    case VisualPixelFormat::Rgba8888:
        return "Rgba8888";
    case VisualPixelFormat::Unknown:
        break;
    }
    return "Unknown";
}

[[nodiscard]] inline constexpr VisualPixelFormat visualPixelFormatFromString(std::string_view value) noexcept
{
    if (value == "Indexed2" || value == "indexed2") {
        return VisualPixelFormat::Indexed2;
    }
    if (value == "Rgba8888" || value == "RGBA8888" || value == "rgba8888") {
        return VisualPixelFormat::Rgba8888;
    }
    return VisualPixelFormat::Unknown;
}

[[nodiscard]] inline std::string toHexVisualHash(VisualResourceHash hash)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

[[nodiscard]] inline VisualResourceHash hashVisualSourceBytes(std::span<const uint8_t> bytes,
                                                             VisualResourceHash seed) noexcept
{
    auto hash = seed;
    for (const auto byte : bytes) {
        detail::mixVisualHashByte(hash, byte);
    }
    return hash;
}

[[nodiscard]] inline VisualResourceHash hashVisualSourceBytes(std::span<const uint8_t> bytes) noexcept
{
    return hashVisualSourceBytes(bytes, detail::kFnvOffset);
}

[[nodiscard]] inline VisualResourceHash hashDecodedVisualContent(const DecodedVisualResource& resource) noexcept
{
    auto hash = detail::kFnvOffset;

    detail::mixVisualHashByte(hash, static_cast<uint8_t>(resource.descriptor.decodedFormat));
    for (int shift = 0; shift < 32; shift += 8) {
        detail::mixVisualHashByte(hash, static_cast<uint8_t>((resource.descriptor.width >> shift) & 0xFFu));
        detail::mixVisualHashByte(hash, static_cast<uint8_t>((resource.descriptor.height >> shift) & 0xFFu));
    }
    return hashVisualSourceBytes(resource.pixels, hash);
}

[[nodiscard]] inline VisualResourceHash hashVisualPalette(uint32_t paletteValue) noexcept
{
    auto hash = detail::kFnvOffset;
    for (int shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<VisualResourceHash>((paletteValue >> shift) & 0xFFu);
        hash *= detail::kFnvPrime;
    }
    return hash;
}

[[nodiscard]] inline VisualResourceHash combineVisualHashes(VisualResourceHash first, VisualResourceHash second) noexcept
{
    auto hash = first;
    for (int shift = 0; shift < 64; shift += 8) {
        hash ^= static_cast<VisualResourceHash>((second >> shift) & 0xFFu);
        hash *= detail::kFnvPrime;
    }
    return hash;
}

} // namespace BMMQ

#endif // BMMQ_VISUAL_TYPES_HPP
