#ifndef BMMQ_VIDEO_ENGINE_HPP
#define BMMQ_VIDEO_ENGINE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

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

        const auto pixelCount = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
        frame.pixels.assign(pixelCount, paletteColor(0));
        if (state.vram.empty()) {
            return frame;
        }

        std::vector<uint8_t> backgroundColorIndices(static_cast<std::size_t>(frame.width), 0u);
        for (int y = 0; y < frame.height; ++y) {
            renderScanline(frame, state, y, backgroundColorIndices);
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
            if (backgroundEnabled) {
                const auto colorIndex = backgroundColorIndex(state, x, screenY);
                backgroundColorIndices[static_cast<std::size_t>(x)] = colorIndex;
                shade = mapPaletteShade(state.bgp, colorIndex);
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
        uint16_t tileAddress = 0x8000u;
        if (unsignedTileData) {
            tileAddress = static_cast<uint16_t>(0x8000u + static_cast<uint16_t>(tileIndex) * 16u);
        } else {
            tileAddress = static_cast<uint16_t>(0x9000 + static_cast<int16_t>(static_cast<int8_t>(tileIndex)) * 16);
        }

        const auto rowAddress = static_cast<uint16_t>(tileAddress + static_cast<uint16_t>(tileY) * 2u);
        const auto low = readVramByte(state, rowAddress);
        const auto high = readVramByte(state, static_cast<uint16_t>(rowAddress + 1u));
        const auto bit = static_cast<uint8_t>(7u - (tileX & 0x07u));
        return static_cast<uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
    }

    [[nodiscard]] static uint8_t backgroundColorIndex(const VideoStateView& state, int screenX, int screenY) noexcept
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
        return sampleTileColorIndex(state,
                                    tileIndex,
                                    unsignedTileData,
                                    static_cast<uint8_t>(mapX & 0x07),
                                    static_cast<uint8_t>(mapY & 0x07));
    }

    static void compositeSpritesForScanline(VideoFramePacket& frame,
                                            const VideoStateView& state,
                                            std::span<const uint8_t> backgroundColorIndices,
                                            int screenY) noexcept
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
                frame.pixels[pixelIndex] = paletteColor(shade);
            }
        }
    }

    VideoEngineConfig config_{};
    std::deque<VideoFramePacket> queuedFrames_{};
    std::optional<VideoFramePacket> lastValidFrame_{};
    VideoEngineStats stats_{};
    uint64_t currentGeneration_ = 0;
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_ENGINE_HPP
