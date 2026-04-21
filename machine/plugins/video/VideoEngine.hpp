#ifndef BMMQ_VIDEO_ENGINE_HPP
#define BMMQ_VIDEO_ENGINE_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../../../cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "../../VisualOverrideService.hpp"
#include "../IoPlugin.hpp"
#include "VideoFrame.hpp"

namespace BMMQ {

struct VideoEngineConfig {
    int frameWidth = 160;
    int frameHeight = 144;
    std::size_t queueCapacityFrames = 3;
};

struct VideoEngineStats {
    std::size_t droppedFrameCount = 0;
    std::size_t frameQueueHighWaterMark = 0;
    std::size_t framesSubmitted = 0;
    std::size_t framesConsumed = 0;
};

struct VideoSubmitResult {
    bool accepted = false;
    bool droppedOldest = false;
};

class VideoEngine {
public:
    explicit VideoEngine(const VideoEngineConfig& config = {})
        : config_(normalizedConfig(config))
    {
        initializeQueue();
    }

    void configure(const VideoEngineConfig& config)
    {
        config_ = normalizedConfig(config);
        initializeQueue();
        stats_ = {};
        lastValidFrame_.reset();
        currentGeneration_ = 0;
    }

    [[nodiscard]] const VideoEngineConfig& config() const noexcept
    {
        return config_;
    }

    [[nodiscard]] uint64_t currentGeneration() const noexcept
    {
        return currentGeneration_;
    }

    void setVisualOverrideService(VisualOverrideService* service) noexcept
    {
        visualOverrideService_ = service;
    }

    uint64_t advanceGeneration() noexcept
    {
        clearQueue();
        return ++currentGeneration_;
    }

    [[nodiscard]] VideoFramePacket buildDebugFrame(const VideoStateView& state, uint64_t generation) const
    {
        VideoFramePacket frame;
        frame.width = config_.frameWidth;
        frame.height = config_.frameHeight;
        frame.generation = generation;
        frame.source = VideoFrameSource::MachineSnapshot;
        const bool notifyVisualComposition =
            visualOverrideService_ != nullptr && visualOverrideService_->hasActiveWork();
        if (notifyVisualComposition) {
            visualOverrideService_->notifyFrameCompositionStarted(generation);
        }

        const auto pixelCount = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
        frame.pixels.assign(pixelCount, paletteColor(0));
        if (state.vram.empty()) {
            if (notifyVisualComposition) {
                visualOverrideService_->notifyFrameCompositionCompleted(generation);
            }
            return frame;
        }

        resetVisualTileCache();
        std::vector<uint8_t> backgroundColorIndices(static_cast<std::size_t>(frame.width), 0u);
        for (int y = 0; y < frame.height; ++y) {
            renderScanline(frame, state, y, backgroundColorIndices);
        }
        if (notifyVisualComposition) {
            visualOverrideService_->notifyFrameCompositionCompleted(generation);
        }
        return frame;
    }

    void renderScanline(VideoFramePacket& frame, const VideoStateView& state, int screenY) const
    {
        std::vector<uint8_t> backgroundColorIndices(static_cast<std::size_t>(std::max(frame.width, 0)), 0u);
        renderScanline(frame, state, screenY, backgroundColorIndices);
    }

    void renderScanline(VideoFramePacket& frame,
                        const VideoStateView& state,
                        int screenY,
                        std::vector<uint8_t>& backgroundColorIndices) const
    {
        if (screenY < 0 || screenY >= frame.height || frame.width <= 0 || frame.empty()) {
            return;
        }

        const bool lcdEnabled = state.lcdEnabled();
        const bool backgroundEnabled = (state.lcdc & 0x01u) != 0u;
        const auto lineWidth = static_cast<std::size_t>(frame.width);
        if (backgroundColorIndices.size() != lineWidth) {
            backgroundColorIndices.resize(lineWidth);
        }
        std::fill(backgroundColorIndices.begin(), backgroundColorIndices.end(), 0u);

        for (int x = 0; x < frame.width; ++x) {
            const auto pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(frame.width)
                                  + static_cast<std::size_t>(x);
            if (pixelIndex >= frame.pixels.size()) {
                return;
            }
            if (!lcdEnabled) {
                frame.pixels[pixelIndex] = paletteColor(0);
                continue;
            }

            uint8_t shade = 0;
            bool usedVisualOverride = false;
            if (backgroundEnabled) {
                const auto sample = backgroundSample(state, x, screenY);
                const auto colorIndex = sample.colorIndex;
                backgroundColorIndices[static_cast<std::size_t>(x)] = colorIndex;
                if (auto replacementPixel = replacementPixelForTile(state, sample.tileIndex, sample.tileAddress,
                                                                     sample.tileX, sample.tileY,
                                                                     VisualResourceKind::Tile,
                                                                     state.bgp,
                                                                     "BGP",
                                                                     GB::GameBoyVisualSemanticContext{
                                                                         .fromWindow = sample.useWindow,
                                                                         .hasTileDataMode = true,
                                                                         .unsignedTileData = sample.unsignedTileData,
                                                                     });
                    replacementPixel.has_value()) {
                    frame.pixels[pixelIndex] = *replacementPixel;
                    usedVisualOverride = true;
                } else {
                    shade = mapPaletteShade(state.bgp, colorIndex);
                }
            }
            if (usedVisualOverride) {
                continue;
            }
            frame.pixels[pixelIndex] = paletteColor(shade);
        }

        if (lcdEnabled) {
            compositeSpritesForScanline(frame,
                                        state,
                                        std::span<const uint8_t>(backgroundColorIndices.data(),
                                                                 backgroundColorIndices.size()),
                                        screenY);
        }
    }

    // Queue policy: queuedFrames_ always keeps the newest config_.queueCapacityFrames frames.
    // When the queue is full, the oldest frame is evicted, stats_.droppedFrameCount still
    // increments, the new frame is accepted, stats_.framesSubmitted still increments, and
    // stats_.frameQueueHighWaterMark reflects the capped queue size. A successful submission
    // therefore means the newest frame was queued, not that no drop occurred.
    [[nodiscard]] VideoSubmitResult submitFrame(const VideoFramePacket& frame)
    {
        if (frame.empty()) {
            return {};
        }

        bool droppedOldest = false;
        lastValidFrame_ = frame;
        if (queuedFrames_.size() >= config_.queueCapacityFrames) {
            ++stats_.droppedFrameCount;
            queuedFrames_.pop_front();
            droppedOldest = true;
        }
        queuedFrames_.push_back(frame);
        ++stats_.framesSubmitted;
        stats_.frameQueueHighWaterMark = std::max(stats_.frameQueueHighWaterMark, queuedFrames_.size());
        return VideoSubmitResult{.accepted = true, .droppedOldest = droppedOldest};
    }

    [[nodiscard]] std::optional<VideoFramePacket> tryConsumeFrame()
    {
        if (queuedFrames_.empty()) {
            return std::nullopt;
        }
        auto frame = queuedFrames_.front();
        queuedFrames_.pop_front();
        ++stats_.framesConsumed;
        return frame;
    }

    [[nodiscard]] VideoFramePacket fallbackFrame() const
    {
        if (lastValidFrame_.has_value()) {
            auto frame = *lastValidFrame_;
            frame.source = VideoFrameSource::LastValidFallback;
            return frame;
        }
        return makeBlankVideoFrame(config_.frameWidth, config_.frameHeight, currentGeneration_);
    }

    [[nodiscard]] const std::optional<VideoFramePacket>& lastValidFrame() const noexcept
    {
        return lastValidFrame_;
    }

    [[nodiscard]] const VideoEngineStats& stats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] std::size_t queuedFrameCount() const noexcept
    {
        return queuedFrames_.size();
    }

private:
    [[nodiscard]] static VideoEngineConfig normalizedConfig(VideoEngineConfig config) noexcept
    {
        config.frameWidth = std::max(config.frameWidth, 1);
        config.frameHeight = std::max(config.frameHeight, 1);
        config.queueCapacityFrames = std::max<std::size_t>(config.queueCapacityFrames, 1u);
        return config;
    }

    void initializeQueue()
    {
        queuedFrames_.clear();
    }

    void clearQueue() noexcept
    {
        queuedFrames_.clear();
    }

    [[nodiscard]] static uint32_t paletteColor(uint8_t shade) noexcept
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

    [[nodiscard]] static uint8_t mapPaletteShade(uint8_t palette, uint8_t colorIndex) noexcept
    {
        return static_cast<uint8_t>((palette >> (colorIndex * 2u)) & 0x03u);
    }

    [[nodiscard]] static uint8_t readVramByte(const VideoStateView& state, uint16_t address) noexcept
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

    [[nodiscard]] static uint8_t readOamByte(const VideoStateView& state, std::size_t index) noexcept
    {
        if (index >= state.oam.size()) {
            return 0xFFu;
        }
        return state.oam[index];
    }

    [[nodiscard]] static uint8_t sampleTileColorIndex(const VideoStateView& state,
                                                      uint8_t tileIndex,
                                                      bool unsignedTileData,
                                                      uint8_t tileX,
                                                      uint8_t tileY) noexcept
    {
        const auto tileAddress = tileDataAddress(tileIndex, unsignedTileData);
        const auto rowAddress = static_cast<uint16_t>(tileAddress + static_cast<uint16_t>(tileY) * 2u);
        const auto low = readVramByte(state, rowAddress);
        const auto high = readVramByte(state, static_cast<uint16_t>(rowAddress + 1u));
        const auto bit = static_cast<uint8_t>(7u - (tileX & 0x07u));
        return static_cast<uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
    }

    [[nodiscard]] static uint16_t tileDataAddress(uint8_t tileIndex, bool unsignedTileData) noexcept
    {
        if (unsignedTileData) {
            return static_cast<uint16_t>(0x8000u + static_cast<uint16_t>(tileIndex) * 16u);
        }
        return static_cast<uint16_t>(0x9000 + static_cast<int16_t>(static_cast<int8_t>(tileIndex)) * 16);
    }

    [[nodiscard]] static uint8_t backgroundColorIndex(const VideoStateView& state, int screenX, int screenY) noexcept
    {
        return backgroundSample(state, screenX, screenY).colorIndex;
    }

    struct BackgroundSample {
        uint8_t tileIndex = 0;
        uint16_t tileAddress = 0x8000u;
        uint8_t tileX = 0;
        uint8_t tileY = 0;
        uint8_t colorIndex = 0;
        bool useWindow = false;
        bool unsignedTileData = true;
    };

    [[nodiscard]] static BackgroundSample backgroundSample(const VideoStateView& state, int screenX, int screenY) noexcept
    {
        const bool windowEnabled = (state.lcdc & 0x20u) != 0u;
        const int windowLeft = static_cast<int>(state.wx) - 7;
        const bool useWindow = windowEnabled && screenY >= static_cast<int>(state.wy) && screenX >= windowLeft;

        int mapX = (screenX + static_cast<int>(state.scx)) & 0xFF;
        int mapY = (screenY + static_cast<int>(state.scy)) & 0xFF;
        uint16_t mapBase = (state.lcdc & 0x08u) != 0u ? 0x9C00u : 0x9800u;

        if (useWindow) {
            mapBase = (state.lcdc & 0x40u) != 0u ? 0x9C00u : 0x9800u;
            mapX = std::max(0, screenX - windowLeft);
            mapY = std::max(0, screenY - static_cast<int>(state.wy));
        }

        const auto tileMapAddress = static_cast<uint16_t>(
            mapBase + static_cast<uint16_t>(((mapY >> 3) & 0x1Fu) * 32 + ((mapX >> 3) & 0x1Fu)));
        const auto tileIndex = readVramByte(state, tileMapAddress);
        const bool unsignedTileData = (state.lcdc & 0x10u) != 0u;
        const auto tileX = static_cast<uint8_t>(mapX & 0x07);
        const auto tileY = static_cast<uint8_t>(mapY & 0x07);
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

    struct VisualTileCacheEntry {
        std::optional<DecodedVisualResource> resource;
        std::optional<ResolvedVisualOverride> resolved;
        bool decodeAttempted = false;
        bool resolveAttempted = false;
    };

    void resetVisualTileCache() const
    {
        visualTileCache_.clear();
    }

    [[nodiscard]] static std::string visualTileCacheKey(uint16_t tileAddress,
                                                        VisualResourceKind kind,
                                                        uint8_t paletteValue,
                                                        std::string_view semanticLabel)
    {
        return std::to_string(tileAddress) + "|" +
               std::to_string(static_cast<uint32_t>(kind)) + "|" +
               std::to_string(paletteValue) + "|" +
               std::string(semanticLabel);
    }

    [[nodiscard]] std::optional<uint32_t> replacementPixelForTile(const VideoStateView& state,
                                                                  uint8_t tileIndex,
                                                                  uint16_t tileAddress,
                                                                  uint8_t tileX,
                                                                  uint8_t tileY,
                                                                  VisualResourceKind kind,
                                                                  uint8_t paletteValue,
                                                                  std::string_view paletteRegister,
                                                                  const GB::GameBoyVisualSemanticContext& semanticContext = {}) const
    {
        if (visualOverrideService_ == nullptr || !visualOverrideService_->hasActiveWork()) {
            return std::nullopt;
        }
        if (tileAddress < 0x8000u) {
            return std::nullopt;
        }
        const auto semanticLabel = GB::gameBoySemanticLabel(kind, semanticContext);
        const auto cacheKey = visualTileCacheKey(tileAddress, kind, paletteValue, semanticLabel);
        auto& entry = visualTileCache_[cacheKey];

        if (!entry.decodeAttempted) {
            entry.decodeAttempted = true;
            auto resource = GB::decodeGameBoyTileResourceAtVramOffset(state,
                                                                      static_cast<std::size_t>(tileAddress - 0x8000u),
                                                                      tileIndex,
                                                                      kind,
                                                                      paletteValue,
                                                                      paletteRegister,
                                                                      semanticContext);
            if (!resource.has_value()) {
                return std::nullopt;
            }
            visualOverrideService_->notifyResourceDecoded(resource->descriptor);
            (void)visualOverrideService_->observe(*resource);
            entry.resource = std::move(*resource);
        }

        if (!entry.resource.has_value()) {
            return std::nullopt;
        }

        if (!visualOverrideService_->hasLoadedPacks()) {
            return std::nullopt;
        }

        if (!entry.resolveAttempted) {
            entry.resolveAttempted = true;
            auto resolved = visualOverrideService_->resolve(entry.resource->descriptor);
            if ((!resolved.has_value() || resolved->image.empty()) && kind != VisualResourceKind::Tile) {
                auto fallbackDescriptor = entry.resource->descriptor;
                fallbackDescriptor.kind = VisualResourceKind::Tile;
                resolved = visualOverrideService_->resolve(fallbackDescriptor);
            }
            if (!resolved.has_value() || resolved->image.empty()) {
                return std::nullopt;
            }
            entry.resolved = std::move(*resolved);
        }

        if (!entry.resolved.has_value()) {
            return std::nullopt;
        }
        return sampleReplacementPixel(*entry.resolved, entry.resource->descriptor, tileX, tileY);
    }

    [[nodiscard]] static std::optional<uint32_t> sampleReplacementPixel(const ResolvedVisualOverride& resolved,
                                                                        const VisualResourceDescriptor& descriptor,
                                                                        uint8_t tileX,
                                                                        uint8_t tileY) noexcept
    {
        if (resolved.image.empty()) {
            return std::nullopt;
        }
        if (resolved.scalePolicy == "exact" &&
            (resolved.image.width != descriptor.width || resolved.image.height != descriptor.height)) {
            return std::nullopt;
        }

        if (resolved.scalePolicy == "crop") {
            return sampleNearest(resolved.image,
                                 cropCoordinate(tileX, resolved.image.width, descriptor.width, resolved.anchor),
                                 cropCoordinate(tileY, resolved.image.height, descriptor.height, resolved.anchor));
        }

        const auto x = scaledCoordinate(tileX, resolved.image.width, descriptor.width);
        const auto y = scaledCoordinate(tileY, resolved.image.height, descriptor.height);
        if (resolved.filterPolicy == "linear") {
            return sampleLinear(resolved.image, x, y);
        }
        return sampleNearest(resolved.image,
                             static_cast<std::size_t>(x),
                             static_cast<std::size_t>(y));
    }

    [[nodiscard]] static double scaledCoordinate(uint8_t sourceCoordinate,
                                                 uint32_t replacementSize,
                                                 uint32_t sourceSize) noexcept
    {
        if (sourceSize <= 1u || replacementSize <= 1u) {
            return 0.0;
        }
        return static_cast<double>(sourceCoordinate) * static_cast<double>(replacementSize - 1u) /
               static_cast<double>(sourceSize - 1u);
    }

    [[nodiscard]] static std::size_t cropCoordinate(uint8_t sourceCoordinate,
                                                    uint32_t replacementSize,
                                                    uint32_t sourceSize,
                                                    const std::string& anchor) noexcept
    {
        std::size_t offset = 0;
        if (replacementSize > sourceSize) {
            if (anchor == "bottom-right" || anchor == "right" || anchor == "bottom") {
                offset = static_cast<std::size_t>(replacementSize - sourceSize);
            } else if (anchor == "center" || anchor == "middle") {
                offset = static_cast<std::size_t>((replacementSize - sourceSize) / 2u);
            }
        }
        return std::min(offset + static_cast<std::size_t>(sourceCoordinate),
                        static_cast<std::size_t>(replacementSize - 1u));
    }

    [[nodiscard]] static std::optional<uint32_t> sampleNearest(const VisualReplacementImage& image,
                                                               std::size_t x,
                                                               std::size_t y) noexcept
    {
        const auto clampedX = std::min(x, static_cast<std::size_t>(image.width - 1u));
        const auto clampedY = std::min(y, static_cast<std::size_t>(image.height - 1u));
        const auto index = clampedY * static_cast<std::size_t>(image.width) + clampedX;
        if (index >= image.argbPixels.size()) {
            return std::nullopt;
        }
        return image.argbPixels[index];
    }

    [[nodiscard]] static std::optional<uint32_t> sampleLinear(const VisualReplacementImage& image,
                                                              double x,
                                                              double y) noexcept
    {
        const auto x0 = static_cast<std::size_t>(std::floor(x));
        const auto y0 = static_cast<std::size_t>(std::floor(y));
        const auto x1 = std::min(x0 + 1u, static_cast<std::size_t>(image.width - 1u));
        const auto y1 = std::min(y0 + 1u, static_cast<std::size_t>(image.height - 1u));
        const auto tx = x - static_cast<double>(x0);
        const auto ty = y - static_cast<double>(y0);
        const auto c00 = sampleNearest(image, x0, y0);
        const auto c10 = sampleNearest(image, x1, y0);
        const auto c01 = sampleNearest(image, x0, y1);
        const auto c11 = sampleNearest(image, x1, y1);
        if (!c00.has_value() || !c10.has_value() || !c01.has_value() || !c11.has_value()) {
            return std::nullopt;
        }
        const auto blend = [tx, ty](uint32_t c00Value, uint32_t c10Value, uint32_t c01Value, uint32_t c11Value,
                                    int shift) {
            const auto p00 = static_cast<double>((c00Value >> shift) & 0xFFu);
            const auto p10 = static_cast<double>((c10Value >> shift) & 0xFFu);
            const auto p01 = static_cast<double>((c01Value >> shift) & 0xFFu);
            const auto p11 = static_cast<double>((c11Value >> shift) & 0xFFu);
            const auto top = p00 + (p10 - p00) * tx;
            const auto bottom = p01 + (p11 - p01) * tx;
            return static_cast<uint32_t>(std::lround(top + (bottom - top) * ty));
        };
        const auto a = blend(*c00, *c10, *c01, *c11, 24);
        const auto r = blend(*c00, *c10, *c01, *c11, 16);
        const auto g = blend(*c00, *c10, *c01, *c11, 8);
        const auto b = blend(*c00, *c10, *c01, *c11, 0);
        return (a << 24u) | (r << 16u) | (g << 8u) | b;
    }

    void compositeSpritesForScanline(VideoFramePacket& frame,
                                     const VideoStateView& state,
                                     std::span<const uint8_t> backgroundColorIndices,
                                     int screenY) const
    {
        if ((state.lcdc & 0x02u) == 0u || state.oam.empty()) {
            return;
        }

        const bool tallSprites = (state.lcdc & 0x04u) != 0u;
        const int spriteHeight = tallSprites ? 16 : 8;
        for (int spriteIndex = 39; spriteIndex >= 0; --spriteIndex) {
            const auto base = static_cast<std::size_t>(spriteIndex) * 4u;
            if (base + 3u >= state.oam.size()) {
                continue;
            }

            const int spriteY = static_cast<int>(readOamByte(state, base)) - 16;
            const int spriteX = static_cast<int>(readOamByte(state, base + 1u)) - 8;
            uint8_t tileIndex = readOamByte(state, base + 2u);
            const uint8_t attributes = readOamByte(state, base + 3u);
            if (spriteX <= -8 || spriteX >= frame.width || spriteY <= -spriteHeight || spriteY >= frame.height) {
                continue;
            }
            if (screenY < spriteY || screenY >= spriteY + spriteHeight) {
                continue;
            }

            if (tallSprites) {
                tileIndex = static_cast<uint8_t>(tileIndex & 0xFEu);
            }

            const bool xFlip = (attributes & 0x20u) != 0u;
            const bool yFlip = (attributes & 0x40u) != 0u;
            const bool behindBackground = (attributes & 0x80u) != 0u;
            const uint8_t palette = (attributes & 0x10u) != 0u ? state.obp1 : state.obp0;
            const int localY = screenY - spriteY;
            int spriteRow = yFlip ? (spriteHeight - 1 - localY) : localY;
            uint8_t effectiveTile = tileIndex;
            if (tallSprites && spriteRow >= 8) {
                effectiveTile = static_cast<uint8_t>(tileIndex + 1u);
                spriteRow -= 8;
            }

            for (int localX = 0; localX < 8; ++localX) {
                const int screenX = spriteX + localX;
                if (screenX < 0 || screenX >= frame.width) {
                    continue;
                }

                const auto pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(frame.width)
                                      + static_cast<std::size_t>(screenX);
                const uint8_t spriteColumn = static_cast<uint8_t>(xFlip ? (7 - localX) : localX);
                const uint8_t colorIndex = sampleTileColorIndex(state,
                                                                effectiveTile,
                                                                true,
                                                                spriteColumn,
                                                                static_cast<uint8_t>(spriteRow));
                if (colorIndex == 0u) {
                    continue;
                }
                if (behindBackground && backgroundColorIndices[static_cast<std::size_t>(screenX)] != 0u) {
                    continue;
                }

                const uint8_t shade = mapPaletteShade(palette, colorIndex);
                const auto spriteTileAddress = tileDataAddress(effectiveTile, true);
                if (const auto replacementPixel = replacementPixelForTile(state,
                                                                           effectiveTile,
                                                                           spriteTileAddress,
                                                                           spriteColumn,
                                                                           static_cast<uint8_t>(spriteRow),
                                                                           VisualResourceKind::Sprite,
                                                                           palette,
                                                                           (attributes & 0x10u) != 0u ? "OBP1" : "OBP0");
                    replacementPixel.has_value()) {
                    frame.pixels[pixelIndex] = *replacementPixel;
                    continue;
                }
                frame.pixels[pixelIndex] = paletteColor(shade);
            }
        }
    }

    VideoEngineConfig config_{};
    VisualOverrideService* visualOverrideService_ = nullptr;
    mutable std::unordered_map<std::string, VisualTileCacheEntry> visualTileCache_;
    std::deque<VideoFramePacket> queuedFrames_{};
    std::optional<VideoFramePacket> lastValidFrame_{};
    VideoEngineStats stats_{};
    uint64_t currentGeneration_ = 0;
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_ENGINE_HPP
