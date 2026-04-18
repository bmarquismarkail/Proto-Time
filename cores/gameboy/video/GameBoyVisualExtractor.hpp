#ifndef GB_GAMEBOY_VISUAL_EXTRACTOR_HPP
#define GB_GAMEBOY_VISUAL_EXTRACTOR_HPP

#include <cstddef>
#include <cstdint>
#include <optional>

#include "../../../machine/VisualTypes.hpp"
#include "../../../machine/plugins/IoPlugin.hpp"

namespace GB {

[[nodiscard]] inline std::optional<BMMQ::DecodedVisualResource> decodeGameBoyTileResource(
    const BMMQ::VideoStateView& state,
    uint16_t tileIndex,
    BMMQ::VisualResourceKind kind = BMMQ::VisualResourceKind::Tile)
{
    constexpr uint32_t kTileWidth = 8;
    constexpr uint32_t kTileHeight = 8;
    constexpr std::size_t kBytesPerTile = 16;
    const auto base = static_cast<std::size_t>(tileIndex) * kBytesPerTile;
    if (base + kBytesPerTile > state.vram.size()) {
        return std::nullopt;
    }

    BMMQ::DecodedVisualResource resource;
    resource.descriptor.machineId = "gameboy";
    resource.descriptor.kind = kind;
    resource.descriptor.width = kTileWidth;
    resource.descriptor.height = kTileHeight;
    resource.descriptor.decodedFormat = BMMQ::VisualPixelFormat::Indexed2;
    resource.descriptor.source.index = tileIndex;
    resource.descriptor.source.address = static_cast<uint32_t>(0x8000u + base);
    resource.stride = kTileWidth;
    resource.pixels.resize(kTileWidth * kTileHeight);

    for (uint32_t y = 0; y < kTileHeight; ++y) {
        const auto rowBase = base + static_cast<std::size_t>(y) * 2u;
        const auto low = state.vram[rowBase];
        const auto high = state.vram[rowBase + 1u];
        for (uint32_t x = 0; x < kTileWidth; ++x) {
            const auto bit = static_cast<uint8_t>(7u - x);
            resource.pixels[static_cast<std::size_t>(y) * kTileWidth + x] =
                static_cast<uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
        }
    }

    resource.descriptor.contentHash = BMMQ::hashDecodedVisualContent(resource);
    return resource;
}

} // namespace GB

#endif // GB_GAMEBOY_VISUAL_EXTRACTOR_HPP
