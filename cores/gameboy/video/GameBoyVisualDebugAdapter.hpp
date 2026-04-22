#ifndef GB_GAMEBOY_VISUAL_DEBUG_ADAPTER_HPP
#define GB_GAMEBOY_VISUAL_DEBUG_ADAPTER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../../../machine/Machine.hpp"
#include "../../../machine/VisualDebugAdapter.hpp"
#include "GameBoyVisualExtractor.hpp"

namespace GB {

class GameBoyVisualDebugAdapter final : public BMMQ::IVisualDebugAdapter {
public:
    [[nodiscard]] std::optional<BMMQ::VideoDebugFrameModel> buildFrameModel(
        const BMMQ::Machine& machine,
        const BMMQ::VideoDebugRenderRequest& request) const override
    {
        auto state = snapshotState(machine);

        BMMQ::VideoDebugFrameModel model;
        model.width = std::max(request.frameWidth, 1);
        model.height = std::max(request.frameHeight, 1);
        model.displayEnabled = state.lcdEnabled();
        model.inVBlank = state.inVBlank();
        model.scanlineIndex = state.ly;
        model.argbPixels.assign(static_cast<std::size_t>(model.width) * static_cast<std::size_t>(model.height),
                                paletteColor(0));
        model.semantics.resize(model.argbPixels.size());

        if (state.vram.empty()) {
            return model;
        }

        RenderContext context{
            .state = state,
            .model = model,
            .resourceCache = {},
        };
        std::vector<std::uint8_t> backgroundColorIndices(static_cast<std::size_t>(model.width), 0u);
        for (int y = 0; y < model.height; ++y) {
            renderScanline(context, y, backgroundColorIndices);
        }
        return model;
    }

    [[nodiscard]] std::optional<BMMQ::DecodedVisualResource> decodeTile(
        const std::vector<std::uint8_t>& vram,
        std::uint8_t bgp,
        std::uint8_t obp0,
        std::uint8_t obp1,
        const BMMQ::VisualTileDecodeRequest& request) const override
    {
        if (request.tileAddress < 0x8000u) {
            return std::nullopt;
        }

        BMMQ::VideoStateView state;
        state.vram = vram;
        state.bgp = bgp;
        state.obp0 = obp0;
        state.obp1 = obp1;

        GameBoyVisualSemanticContext semanticContext{
            .semanticLabel = request.semanticContext.semanticLabel,
            .fromWindow = request.semanticContext.fromWindow,
            .hasTileDataMode = request.semanticContext.unsignedTileData.has_value(),
            .unsignedTileData = request.semanticContext.unsignedTileData.value_or(true),
        };

        return decodeGameBoyTileResourceAtVramOffset(state,
                                                     static_cast<std::size_t>(request.tileAddress - 0x8000u),
                                                     request.tileIndex,
                                                     request.kind,
                                                     request.paletteValue,
                                                     request.paletteRegister,
                                                     semanticContext);
    }

private:
    struct RenderContext {
        const BMMQ::VideoStateView& state;
        BMMQ::VideoDebugFrameModel& model;
        std::unordered_map<std::string, std::uint32_t> resourceCache;
    };

    [[nodiscard]] static BMMQ::VideoStateView snapshotState(const BMMQ::Machine& machine)
    {
        BMMQ::VideoStateView state;
        state.vram.resize(0x2000u, 0xFFu);
        state.oam.resize(0x00A0u, 0xFFu);

        auto& runtime = machine.runtimeContext();
        for (std::uint16_t offset = 0; offset < 0x2000u; ++offset) {
            state.vram[static_cast<std::size_t>(offset)] = runtime.peek8(static_cast<std::uint16_t>(0x8000u + offset));
        }
        for (std::uint16_t offset = 0; offset < 0x00A0u; ++offset) {
            state.oam[static_cast<std::size_t>(offset)] = runtime.peek8(static_cast<std::uint16_t>(0xFE00u + offset));
        }

        state.lcdc = runtime.peek8(0xFF40u);
        state.stat = runtime.peek8(0xFF41u);
        state.scy = runtime.peek8(0xFF42u);
        state.scx = runtime.peek8(0xFF43u);
        state.ly = runtime.peek8(0xFF44u);
        state.lyc = runtime.peek8(0xFF45u);
        state.bgp = runtime.peek8(0xFF47u);
        state.obp0 = runtime.peek8(0xFF48u);
        state.obp1 = runtime.peek8(0xFF49u);
        state.wy = runtime.peek8(0xFF4Au);
        state.wx = runtime.peek8(0xFF4Bu);
        return state;
    }

    [[nodiscard]] static std::uint32_t paletteColor(std::uint8_t shade) noexcept
    {
        switch (shade & 0x03u) {
        case 0:
            return 0xFFE0F8D0u;
        case 1:
            return 0xFF88C070u;
        case 2:
            return 0xFF346856u;
        default:
            return 0xFF081820u;
        }
    }

    [[nodiscard]] static std::uint8_t mapPaletteShade(std::uint8_t palette, std::uint8_t colorIndex) noexcept
    {
        return static_cast<std::uint8_t>((palette >> (colorIndex * 2u)) & 0x03u);
    }

    [[nodiscard]] static std::uint8_t readVramByte(const BMMQ::VideoStateView& state, std::uint16_t address) noexcept
    {
        if (address < 0x8000u || address >= 0xA000u) {
            return 0xFFu;
        }
        const auto index = static_cast<std::size_t>(address - 0x8000u);
        if (index >= state.vram.size()) {
            return 0xFFu;
        }
        return state.vram[index];
    }

    [[nodiscard]] static std::uint8_t readOamByte(const BMMQ::VideoStateView& state, std::size_t index) noexcept
    {
        if (index >= state.oam.size()) {
            return 0xFFu;
        }
        return state.oam[index];
    }

    [[nodiscard]] static std::uint16_t tileDataAddress(std::uint8_t tileIndex, bool unsignedTileData) noexcept
    {
        if (unsignedTileData) {
            return static_cast<std::uint16_t>(0x8000u + static_cast<std::uint16_t>(tileIndex) * 16u);
        }
        return static_cast<std::uint16_t>(0x9000u + static_cast<std::int16_t>(static_cast<std::int8_t>(tileIndex)) * 16);
    }

    [[nodiscard]] static std::uint8_t sampleTileColorIndex(const BMMQ::VideoStateView& state,
                                                           std::uint8_t tileIndex,
                                                           bool unsignedTileData,
                                                           std::uint8_t tileX,
                                                           std::uint8_t tileY) noexcept
    {
        const auto tileAddress = tileDataAddress(tileIndex, unsignedTileData);
        const auto rowAddress = static_cast<std::uint16_t>(tileAddress + static_cast<std::uint16_t>(tileY) * 2u);
        const auto low = readVramByte(state, rowAddress);
        const auto high = readVramByte(state, static_cast<std::uint16_t>(rowAddress + 1u));
        const auto bit = static_cast<std::uint8_t>(7u - (tileX & 0x07u));
        return static_cast<std::uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
    }

    struct BackgroundSample {
        std::uint8_t tileIndex = 0;
        std::uint16_t tileAddress = 0x8000u;
        std::uint8_t tileX = 0;
        std::uint8_t tileY = 0;
        std::uint8_t colorIndex = 0;
        bool useWindow = false;
        bool unsignedTileData = true;
    };

    [[nodiscard]] static BackgroundSample backgroundSample(const BMMQ::VideoStateView& state,
                                                           int screenX,
                                                           int screenY) noexcept
    {
        const bool windowEnabled = (state.lcdc & 0x20u) != 0u;
        const int windowLeft = static_cast<int>(state.wx) - 7;
        const bool useWindow = windowEnabled &&
                               screenY >= static_cast<int>(state.wy) &&
                               screenX >= windowLeft;

        int mapX = (screenX + static_cast<int>(state.scx)) & 0xFF;
        int mapY = (screenY + static_cast<int>(state.scy)) & 0xFF;
        std::uint16_t mapBase = (state.lcdc & 0x08u) != 0u ? 0x9C00u : 0x9800u;

        if (useWindow) {
            mapBase = (state.lcdc & 0x40u) != 0u ? 0x9C00u : 0x9800u;
            mapX = std::max(0, screenX - windowLeft);
            mapY = std::max(0, screenY - static_cast<int>(state.wy));
        }

        const auto tileMapAddress = static_cast<std::uint16_t>(
            mapBase + static_cast<std::uint16_t>(((mapY >> 3) & 0x1Fu) * 32 + ((mapX >> 3) & 0x1Fu)));
        const auto tileIndex = readVramByte(state, tileMapAddress);
        const bool unsignedTileData = (state.lcdc & 0x10u) != 0u;
        const auto tileX = static_cast<std::uint8_t>(mapX & 0x07);
        const auto tileY = static_cast<std::uint8_t>(mapY & 0x07);

        return BackgroundSample{
            .tileIndex = tileIndex,
            .tileAddress = tileDataAddress(tileIndex, unsignedTileData),
            .tileX = tileX,
            .tileY = tileY,
            .colorIndex = sampleTileColorIndex(state, tileIndex, unsignedTileData, tileX, tileY),
            .useWindow = useWindow,
            .unsignedTileData = unsignedTileData,
        };
    }

    [[nodiscard]] static std::string resourceCacheKey(std::uint16_t tileAddress,
                                                      BMMQ::VisualResourceKind kind,
                                                      std::uint8_t paletteValue,
                                                      std::string_view paletteRegister,
                                                      std::string_view semanticLabel) {
        return std::to_string(tileAddress) + "|" +
               std::to_string(static_cast<std::uint32_t>(kind)) + "|" +
               std::to_string(paletteValue) + "|" +
               std::string(paletteRegister) + "|" +
               std::string(semanticLabel);
    }

    [[nodiscard]] static std::optional<std::uint32_t> ensureResource(
        RenderContext& context,
        std::uint8_t tileIndex,
        std::uint16_t tileAddress,
        BMMQ::VisualResourceKind kind,
        std::uint8_t paletteValue,
        std::string_view paletteRegister,
        const GameBoyVisualSemanticContext& semanticContext)
    {
        const auto semanticLabel = gameBoySemanticLabel(kind, semanticContext);
        const auto key = resourceCacheKey(tileAddress, kind, paletteValue, paletteRegister, semanticLabel);
        if (const auto it = context.resourceCache.find(key); it != context.resourceCache.end()) {
            return it->second;
        }

        auto resource = decodeGameBoyTileResourceAtVramOffset(context.state,
                                                              static_cast<std::size_t>(tileAddress - 0x8000u),
                                                              tileIndex,
                                                              kind,
                                                              paletteValue,
                                                              paletteRegister,
                                                              semanticContext);
        if (!resource.has_value()) {
            return std::nullopt;
        }

        const auto resourceIndex = static_cast<std::uint32_t>(context.model.resources.size());
        context.model.resources.push_back(std::move(*resource));
        context.resourceCache.emplace(key, resourceIndex);
        return resourceIndex;
    }

    static void setPixel(RenderContext& context,
                         std::size_t pixelIndex,
                         std::uint32_t argb,
                         std::optional<std::uint32_t> resourceIndex,
                         std::uint8_t sampleX,
                         std::uint8_t sampleY)
    {
        if (pixelIndex >= context.model.argbPixels.size()) {
            return;
        }
        context.model.argbPixels[pixelIndex] = argb;
        if (!resourceIndex.has_value()) {
            context.model.semantics[pixelIndex] = {};
            return;
        }
        context.model.semantics[pixelIndex] = BMMQ::VideoDebugPixelSemantic{
            .resourceIndex = *resourceIndex,
            .sampleX = sampleX,
            .sampleY = sampleY,
        };
    }

    static void renderScanline(RenderContext& context,
                               int screenY,
                               std::vector<std::uint8_t>& backgroundColorIndices)
    {
        auto& model = context.model;
        if (screenY < 0 || screenY >= model.height || model.width <= 0 || model.empty()) {
            return;
        }

        const bool lcdEnabled = context.state.lcdEnabled();
        const bool backgroundEnabled = (context.state.lcdc & 0x01u) != 0u;
        const auto lineWidth = static_cast<std::size_t>(model.width);
        if (backgroundColorIndices.size() != lineWidth) {
            backgroundColorIndices.resize(lineWidth);
        }
        std::fill(backgroundColorIndices.begin(), backgroundColorIndices.end(), 0u);

        for (int x = 0; x < model.width; ++x) {
            const auto pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(model.width)
                                  + static_cast<std::size_t>(x);
            if (!lcdEnabled) {
                setPixel(context, pixelIndex, paletteColor(0), std::nullopt, 0u, 0u);
                continue;
            }

            if (!backgroundEnabled) {
                setPixel(context, pixelIndex, paletteColor(0), std::nullopt, 0u, 0u);
                continue;
            }

            const auto sample = backgroundSample(context.state, x, screenY);
            backgroundColorIndices[static_cast<std::size_t>(x)] = sample.colorIndex;
            const auto resourceIndex = ensureResource(
                context,
                sample.tileIndex,
                sample.tileAddress,
                BMMQ::VisualResourceKind::Tile,
                context.state.bgp,
                "BGP",
                GameBoyVisualSemanticContext{
                    .fromWindow = sample.useWindow,
                    .hasTileDataMode = true,
                    .unsignedTileData = sample.unsignedTileData,
                });
            const auto shade = mapPaletteShade(context.state.bgp, sample.colorIndex);
            setPixel(context,
                     pixelIndex,
                     paletteColor(shade),
                     resourceIndex,
                     sample.tileX,
                     sample.tileY);
        }

        compositeSpritesForScanline(context,
                                    std::span<const std::uint8_t>(backgroundColorIndices.data(),
                                                                  backgroundColorIndices.size()),
                                    screenY);
    }

    static void compositeSpritesForScanline(RenderContext& context,
                                            std::span<const std::uint8_t> backgroundColorIndices,
                                            int screenY)
    {
        if ((context.state.lcdc & 0x02u) == 0u || context.state.oam.empty()) {
            return;
        }

        const bool tallSprites = (context.state.lcdc & 0x04u) != 0u;
        const int spriteHeight = tallSprites ? 16 : 8;
        for (int spriteIndex = 39; spriteIndex >= 0; --spriteIndex) {
            const auto base = static_cast<std::size_t>(spriteIndex) * 4u;
            if (base + 3u >= context.state.oam.size()) {
                continue;
            }

            const int spriteY = static_cast<int>(readOamByte(context.state, base)) - 16;
            const int spriteX = static_cast<int>(readOamByte(context.state, base + 1u)) - 8;
            std::uint8_t tileIndex = readOamByte(context.state, base + 2u);
            const std::uint8_t attributes = readOamByte(context.state, base + 3u);
            if (spriteX <= -8 || spriteX >= context.model.width ||
                spriteY <= -spriteHeight || spriteY >= context.model.height) {
                continue;
            }
            if (screenY < spriteY || screenY >= spriteY + spriteHeight) {
                continue;
            }

            if (tallSprites) {
                tileIndex = static_cast<std::uint8_t>(tileIndex & 0xFEu);
            }

            const bool xFlip = (attributes & 0x20u) != 0u;
            const bool yFlip = (attributes & 0x40u) != 0u;
            const bool behindBackground = (attributes & 0x80u) != 0u;
            const std::uint8_t palette = (attributes & 0x10u) != 0u ? context.state.obp1 : context.state.obp0;
            const int localY = screenY - spriteY;
            int spriteRow = yFlip ? (spriteHeight - 1 - localY) : localY;
            std::uint8_t effectiveTile = tileIndex;
            if (tallSprites && spriteRow >= 8) {
                effectiveTile = static_cast<std::uint8_t>(tileIndex + 1u);
                spriteRow -= 8;
            }

            for (int localX = 0; localX < 8; ++localX) {
                const int screenX = spriteX + localX;
                if (screenX < 0 || screenX >= context.model.width) {
                    continue;
                }

                const auto pixelIndex = static_cast<std::size_t>(screenY) *
                                            static_cast<std::size_t>(context.model.width) +
                                        static_cast<std::size_t>(screenX);
                const std::uint8_t spriteColumn = static_cast<std::uint8_t>(xFlip ? (7 - localX) : localX);
                const std::uint8_t colorIndex = sampleTileColorIndex(context.state,
                                                                     effectiveTile,
                                                                     true,
                                                                     spriteColumn,
                                                                     static_cast<std::uint8_t>(spriteRow));
                if (colorIndex == 0u) {
                    continue;
                }
                if (behindBackground && backgroundColorIndices[static_cast<std::size_t>(screenX)] != 0u) {
                    continue;
                }

                const auto resourceIndex = ensureResource(
                    context,
                    effectiveTile,
                    tileDataAddress(effectiveTile, true),
                    BMMQ::VisualResourceKind::Sprite,
                    palette,
                    (attributes & 0x10u) != 0u ? "OBP1" : "OBP0",
                    GameBoyVisualSemanticContext{});
                const auto shade = mapPaletteShade(palette, colorIndex);
                setPixel(context,
                         pixelIndex,
                         paletteColor(shade),
                         resourceIndex,
                         spriteColumn,
                         static_cast<std::uint8_t>(spriteRow));
            }
        }
    }
};

[[nodiscard]] inline const BMMQ::IVisualDebugAdapter& gameBoyVisualDebugAdapter()
{
    static const GameBoyVisualDebugAdapter adapter;
    return adapter;
}

} // namespace GB

#endif // GB_GAMEBOY_VISUAL_DEBUG_ADAPTER_HPP
