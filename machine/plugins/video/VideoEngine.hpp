#ifndef BMMQ_VIDEO_ENGINE_HPP
#define BMMQ_VIDEO_ENGINE_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../VideoDebugModel.hpp"
#include "../../VisualOverrideService.hpp"
#include "VideoFrame.hpp"

namespace BMMQ {

struct VideoEngineConfig {
    int frameWidth = 160;
    int frameHeight = 144;
    std::size_t mailboxDepthFrames = 2;
};

struct VideoEngineStats {
    std::size_t publishedFrameCount = 0;
    std::size_t publishedDebugFrameCount = 0;
    std::size_t publishedRealtimeFrameCount = 0;
    std::size_t publishedDebugPixelBytes = 0;
    std::size_t publishedRealtimePixelBytes = 0;
    std::size_t consumedFrameCount = 0;
    std::size_t staleFrameDropCount = 0;
    std::size_t staleDebugFrameDropCount = 0;
    std::size_t staleRealtimeFrameDropCount = 0;
    std::size_t overwriteCount = 0;
    std::size_t overwriteDebugFrameCount = 0;
    std::size_t overwriteRealtimeFrameCount = 0;
    std::size_t mailboxHighWaterMark = 0;
    std::size_t mailboxDepth = 0;
    std::size_t publishedPixelBytes = 0;
    std::uint64_t lastPublishedGeneration = 0;
    std::uint64_t lastConsumedGeneration = 0;
    std::uint64_t frameAgeLastNs = 0;
    std::uint64_t frameAgeHighWaterNs = 0;
    std::size_t frameAgeUnder50usCount = 0;
    std::size_t frameAge50To100usCount = 0;
    std::size_t frameAge100To250usCount = 0;
    std::size_t frameAge250To500usCount = 0;
    std::size_t frameAge500usTo1msCount = 0;
    std::size_t frameAge1To2msCount = 0;
    std::size_t frameAge2To5msCount = 0;
    std::size_t frameAge5To10msCount = 0;
    std::size_t frameAgeOver10msCount = 0;
};

struct VideoSubmitResult {
    bool accepted = false;
    bool overwroteOldFrame = false;
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

    [[nodiscard]] std::uint64_t currentGeneration() const noexcept
    {
        return currentGeneration_;
    }

    void setVisualOverrideService(VisualOverrideService* service) noexcept
    {
        visualOverrideService_ = service;
    }

    [[nodiscard]] VideoFramePacket buildDebugFrame(const VideoDebugFrameModel& model,
                                                   std::uint64_t generation) const
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
        frame.pixels.assign(pixelCount, 0xFF000000u);
        if (model.empty()) {
            if (notifyVisualComposition) {
                visualOverrideService_->notifyFrameCompositionCompleted(generation);
            }
            return frame;
        }

        resetVisualResourceCache();
        const auto copyCount = std::min(frame.pixels.size(), model.argbPixels.size());
        for (std::size_t i = 0; i < copyCount; ++i) {
            auto pixel = model.argbPixels[i];
            if (i < model.semantics.size()) {
                const auto& semantic = model.semantics[i];
                if (semantic.hasResource() && semantic.resourceIndex < model.resources.size()) {
                    if (const auto replacement = replacementPixelForResource(model.resources[semantic.resourceIndex],
                                                                            semantic.sampleX,
                                                                            semantic.sampleY,
                                                                            generation);
                        replacement.has_value()) {
                        pixel = *replacement;
                    }
                }
            }
            frame.pixels[i] = pixel;
        }

        if (notifyVisualComposition) {
            visualOverrideService_->notifyFrameCompositionCompleted(generation);
        }
        return frame;
    }

    [[nodiscard]] VideoSubmitResult submitFrame(const VideoFramePacket& frame)
    {
        return submitPresentPacket(makePresentPacket(VideoFramePacket(frame)));
    }

    [[nodiscard]] VideoSubmitResult submitPresentPacket(const VideoPresentPacket& frame)
    {
        if (frame.empty()) {
            return {};
        }

        auto stamped = frame;
        stamped.publishedAtNs = steadyClockNs();
        lastValidFrame_ = stamped;

        // Write frame to the producer's private slot (never touched by consumer).
        mailboxSlots_[mailboxProducerSlot_] = std::move(stamped);

        // Atomically publish: set dirty flag and our slot index.
        // Swap with the shared slot; the old shared slot becomes the new producer slot.
        const auto toShare = static_cast<std::uint8_t>(kMailboxDirty | static_cast<std::uint8_t>(mailboxProducerSlot_));
        const auto old = mailboxShared_.exchange(toShare, std::memory_order_acq_rel);
        const bool overwroteOldFrame = (old & kMailboxDirty) != 0u;
        const int oldSlot = static_cast<int>(old & kMailboxSlotMask);

        // The old shared slot is now exclusively ours; recycle it for the next write.
        mailboxProducerSlot_ = oldSlot;

        // Stats
        ++stats_.publishedFrameCount;
        if (frame.source == VideoFrameSource::RealtimeSnapshot) {
            ++stats_.publishedRealtimeFrameCount;
            stats_.publishedRealtimePixelBytes += frame.pixelCount() * sizeof(std::uint32_t);
        } else {
            ++stats_.publishedDebugFrameCount;
            stats_.publishedDebugPixelBytes += frame.pixelCount() * sizeof(std::uint32_t);
        }
        stats_.publishedPixelBytes += frame.pixelCount() * sizeof(std::uint32_t);

        if (overwroteOldFrame) {
            ++stats_.overwriteCount;
            // Read the overwritten frame's source from the recycled slot.
            if (mailboxSlots_[oldSlot].source == VideoFrameSource::RealtimeSnapshot) {
                ++stats_.overwriteRealtimeFrameCount;
            } else {
                ++stats_.overwriteDebugFrameCount;
            }
        }

        stats_.mailboxDepth = overwroteOldFrame ? 1u : 1u;
        stats_.mailboxHighWaterMark = std::max(stats_.mailboxHighWaterMark, static_cast<std::size_t>(1u));
        stats_.lastPublishedGeneration = frame.generation;
        return VideoSubmitResult{.accepted = true, .overwroteOldFrame = overwroteOldFrame};
    }

    [[nodiscard]] std::optional<VideoPresentPacket> tryConsumeLatestFrame()
    {
        // Check if a new frame is available.  The dirty flag is set by the producer.
        const auto current = mailboxShared_.load(std::memory_order_acquire);
        if ((current & kMailboxDirty) == 0u) {
            return std::nullopt;
        }

        // Atomically take the latest slot; give the consumer's current slot back as
        // the new shared slot (clean, no dirty flag).  Any concurrent producer write
        // will simply replace our clean slot with a new dirty one.
        const auto toGiveBack = static_cast<std::uint8_t>(mailboxConsumerSlot_);
        const auto taken = mailboxShared_.exchange(toGiveBack, std::memory_order_acq_rel);

        if ((taken & kMailboxDirty) == 0u) {
            // No dirty frame (cannot happen in SPSC, but be defensive).
            return std::nullopt;
        }

        mailboxConsumerSlot_ = static_cast<int>(taken & kMailboxSlotMask);
        auto frame = mailboxSlots_[mailboxConsumerSlot_];

        ++stats_.consumedFrameCount;
        stats_.mailboxDepth = 0u;
        stats_.lastConsumedGeneration = frame.generation;
        if (frame.publishedAtNs != 0u) {
            const auto nowNs = steadyClockNs();
            const auto ageNs = nowNs >= frame.publishedAtNs ? nowNs - frame.publishedAtNs : 0u;
            stats_.frameAgeLastNs = ageNs;
            stats_.frameAgeHighWaterNs = std::max(stats_.frameAgeHighWaterNs, ageNs);
            recordFrameAgeBucket(ageNs);
        }
        return frame;
    }

    [[nodiscard]] VideoPresentPacket fallbackFrame() const
    {
        if (lastValidFrame_.has_value()) {
            auto frame = *lastValidFrame_;
            frame.source = VideoFrameSource::LastValidFallback;
            return frame;
        }
        return makePresentPacket(makeBlankVideoFrame(config_.frameWidth, config_.frameHeight, currentGeneration_));
    }

    [[nodiscard]] const std::optional<VideoPresentPacket>& lastValidFrame() const noexcept
    {
        return lastValidFrame_;
    }

    // Returns a live diagnostics structure. Callers should externally
    // serialize access if producer/consumer activity can run concurrently.
    [[nodiscard]] const VideoEngineStats& stats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] std::size_t mailboxFrameCount() const noexcept
    {
        const auto shared = mailboxShared_.load(std::memory_order_acquire);
        return (shared & kMailboxDirty) != 0u ? 1u : 0u;
    }

    std::uint64_t advanceGeneration() noexcept
    {
        clearQueue();
        return ++currentGeneration_;
    }

private:
    struct VisualResourceCacheEntry {
        const DecodedVisualResource* resource = nullptr;
        std::optional<ResolvedVisualOverride> resolved;
        bool observed = false;
        bool resolveAttempted = false;
    };

    [[nodiscard]] static std::uint64_t steadyClockNs() noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    void recordFrameAgeBucket(std::uint64_t ageNs) noexcept
    {
        constexpr std::uint64_t k50us  =    50'000u;
        constexpr std::uint64_t k100us =   100'000u;
        constexpr std::uint64_t k250us =   250'000u;
        constexpr std::uint64_t k500us =   500'000u;
        constexpr std::uint64_t k1ms   = 1'000'000u;
        constexpr std::uint64_t k2ms   = 2'000'000u;
        constexpr std::uint64_t k5ms   = 5'000'000u;
        constexpr std::uint64_t k10ms  = 10'000'000u;
        if (ageNs < k50us)       { ++stats_.frameAgeUnder50usCount; }
        else if (ageNs < k100us) { ++stats_.frameAge50To100usCount; }
        else if (ageNs < k250us) { ++stats_.frameAge100To250usCount; }
        else if (ageNs < k500us) { ++stats_.frameAge250To500usCount; }
        else if (ageNs < k1ms)   { ++stats_.frameAge500usTo1msCount; }
        else if (ageNs < k2ms)   { ++stats_.frameAge1To2msCount; }
        else if (ageNs < k5ms)   { ++stats_.frameAge2To5msCount; }
        else if (ageNs < k10ms)  { ++stats_.frameAge5To10msCount; }
        else                     { ++stats_.frameAgeOver10msCount; }
    }

    [[nodiscard]] static VideoEngineConfig normalizedConfig(VideoEngineConfig config) noexcept
    {
        config.frameWidth = std::max(config.frameWidth, 1);
        config.frameHeight = std::max(config.frameHeight, 1);
        config.mailboxDepthFrames = std::max<std::size_t>(config.mailboxDepthFrames, 1u);
        return config;
    }

    void initializeQueue() noexcept
    {
        mailboxShared_.store(0x00u, std::memory_order_release);
        mailboxProducerSlot_ = 1;
        mailboxConsumerSlot_ = 2;
        stats_.mailboxDepth = 0u;
    }

    void clearQueue() noexcept
    {
        // Drop any pending frame by clearing the dirty flag.
        const auto current = mailboxShared_.load(std::memory_order_acquire);
        const auto clean = static_cast<std::uint8_t>(current & kMailboxSlotMask);
        mailboxShared_.store(clean, std::memory_order_release);
        stats_.mailboxDepth = 0u;
    }

    void resetVisualResourceCache() const
    {
        visualResourceCache_.clear();
    }

    [[nodiscard]] static std::string resourceCacheKey(const VisualResourceDescriptor& descriptor)
    {
        std::string key;
        key.reserve(128);
        key.append(descriptor.machineId);
        key.push_back('|');
        key.append(std::to_string(static_cast<std::uint32_t>(descriptor.kind)));
        key.push_back('|');
        key.append(std::to_string(descriptor.source.address));
        key.push_back('|');
        key.append(std::to_string(descriptor.source.index));
        key.push_back('|');
        key.append(std::to_string(descriptor.source.paletteValue));
        key.push_back('|');
        key.append(descriptor.source.paletteRegister);
        key.push_back('|');
        key.append(descriptor.source.label);
        key.push_back('|');
        key.append(std::to_string(descriptor.contentHash));
        key.push_back('|');
        key.append(std::to_string(descriptor.paletteAwareHash));
        return key;
    }

    [[nodiscard]] std::optional<std::uint32_t> replacementPixelForResource(const DecodedVisualResource& resource,
                                                                           std::uint8_t sampleX,
                                                                           std::uint8_t sampleY,
                                                                           std::uint64_t generation) const
    {
        if (visualOverrideService_ == nullptr || !visualOverrideService_->hasActiveWork()) {
            return std::nullopt;
        }

        auto& entry = visualResourceCache_[resourceCacheKey(resource.descriptor)];
        if (!entry.observed) {
            entry.observed = true;
            entry.resource = &resource;
            visualOverrideService_->notifyResourceDecoded(resource.descriptor);
            (void)visualOverrideService_->observe(resource);
        }

        if (!visualOverrideService_->hasLoadedPacks()) {
            return std::nullopt;
        }

        const auto hasResolvedPayload = [](const ResolvedVisualOverride& resolved) noexcept {
            switch (resolved.mode) {
            case VisualOverrideMode::ReplaceImage: {
                const auto* image = std::get_if<VisualReplacementImage>(&resolved.payload);
                return image != nullptr && !image->empty();
            }
            case VisualOverrideMode::CompositeLayers: {
                const auto* layers = std::get_if<std::vector<VisualReplacementImage>>(&resolved.payload);
                return layers != nullptr && !layers->empty();
            }
            case VisualOverrideMode::AnimationGroup: {
                const auto* animation = std::get_if<VisualAnimationGroup>(&resolved.payload);
                return animation != nullptr && !animation->frames.empty();
            }
            case VisualOverrideMode::ReplacePalette:
                return std::holds_alternative<VisualReplacementPalette>(resolved.payload);
            case VisualOverrideMode::None:
                break;
            }
            return false;
        };

        if (!entry.resolveAttempted) {
            entry.resolveAttempted = true;
            auto resolved = visualOverrideService_->resolve(resource.descriptor);
            if ((!resolved.has_value() || !hasResolvedPayload(*resolved)) &&
                resource.descriptor.kind != VisualResourceKind::Tile) {
                auto fallbackDescriptor = resource.descriptor;
                fallbackDescriptor.kind = VisualResourceKind::Tile;
                resolved = visualOverrideService_->resolve(fallbackDescriptor);
            }
            if (resolved.has_value() && hasResolvedPayload(*resolved)) {
                entry.resolved = std::move(*resolved);
            }
        }

        if (!entry.resolved.has_value()) {
            return std::nullopt;
        }
        return sampleReplacementPixel(*entry.resolved, resource, sampleX, sampleY, generation);
    }

    [[nodiscard]] static std::optional<std::uint32_t> sampleReplacementPixel(const ResolvedVisualOverride& resolved,
                                                                              const DecodedVisualResource& resource,
                                                                              std::uint8_t sampleX,
                                                                              std::uint8_t sampleY,
                                                                              std::uint64_t generation) noexcept
    {
        const auto applyPostEffects = [&resolved](std::uint32_t pixel) noexcept {
            auto result = pixel;
            for (const auto& effect : resolved.effects) {
                switch (effect.kind) {
                case VisualPostEffectKind::Invert: {
                    const auto alpha = result & 0xFF000000u;
                    result = alpha | (~result & 0x00FFFFFFu);
                    break;
                }
                case VisualPostEffectKind::Grayscale: {
                    const auto alpha = (result >> 24u) & 0xFFu;
                    const auto red = (result >> 16u) & 0xFFu;
                    const auto green = (result >> 8u) & 0xFFu;
                    const auto blue = result & 0xFFu;
                    const auto gray = static_cast<std::uint32_t>(std::lround(
                        0.299 * static_cast<double>(red) +
                        0.587 * static_cast<double>(green) +
                        0.114 * static_cast<double>(blue)));
                    result = (alpha << 24u) | (gray << 16u) | (gray << 8u) | gray;
                    break;
                }
                case VisualPostEffectKind::Multiply: {
                    const auto multiplyChannel = [result, &effect](int shift) noexcept {
                        return static_cast<std::uint32_t>(
                            (((result >> shift) & 0xFFu) * ((effect.argb >> shift) & 0xFFu) + 127u) / 255u);
                    };
                    result = (multiplyChannel(24) << 24u) |
                             (multiplyChannel(16) << 16u) |
                             (multiplyChannel(8) << 8u) |
                             multiplyChannel(0);
                    break;
                }
                case VisualPostEffectKind::AlphaScale: {
                    const auto alpha = ((result >> 24u) & 0xFFu);
                    const auto scaledAlpha = static_cast<std::uint32_t>((alpha * effect.amount + 127u) / 255u);
                    result = (scaledAlpha << 24u) | (result & 0x00FFFFFFu);
                    break;
                }
                default:
                    assert(false && "Unhandled VisualPostEffectKind");
                    break;
                }
            }
            return result;
        };

        if (resolved.mode == VisualOverrideMode::ReplacePalette) {
            const auto* palette = std::get_if<VisualReplacementPalette>(&resolved.payload);
            if (palette == nullptr) {
                return std::nullopt;
            }
            if (sampleX >= resource.descriptor.width || sampleY >= resource.descriptor.height || resource.stride == 0u) {
                return std::nullopt;
            }
            const auto index = static_cast<std::size_t>(sampleY) * resource.stride + sampleX;
            if (index >= resource.pixels.size()) {
                return std::nullopt;
            }
            return applyPostEffects((*palette)[resource.pixels[index] & 0x03u]);
        }

        const auto sampleImagePixel = [&resolved, &resource, sampleX, sampleY](const VisualReplacementImage& image)
            -> std::optional<std::uint32_t> {
            if (image.empty()) {
                return std::nullopt;
            }
            const auto sliceX = std::min<std::size_t>(resolved.slice.x, image.width);
            const auto sliceY = std::min<std::size_t>(resolved.slice.y, image.height);
            const auto availableWidth = static_cast<std::size_t>(image.width) - sliceX;
            const auto availableHeight = static_cast<std::size_t>(image.height) - sliceY;
            const auto sampleWidth = resolved.slice.width != 0u
                ? std::min<std::size_t>(resolved.slice.width, availableWidth)
                : availableWidth;
            const auto sampleHeight = resolved.slice.height != 0u
                ? std::min<std::size_t>(resolved.slice.height, availableHeight)
                : availableHeight;
            if (sampleWidth == 0u || sampleHeight == 0u) {
                return std::nullopt;
            }
            if (resolved.scalePolicy == "exact" &&
                (sampleWidth != resource.descriptor.width || sampleHeight != resource.descriptor.height)) {
                return std::nullopt;
            }

            const auto applyTransform =
                [&resolved](std::size_t x, std::size_t y, std::size_t width, std::size_t height) noexcept {
                    std::size_t tx = x;
                    std::size_t ty = y;
                    if (resolved.transform.flipX && width > 0u) {
                        tx = width - 1u - tx;
                    }
                    if (resolved.transform.flipY && height > 0u) {
                        ty = height - 1u - ty;
                    }
                    switch (resolved.transform.rotateDegrees) {
                    case 90u:
                        return std::pair<std::size_t, std::size_t>{ty, width - 1u - tx};
                    case 180u:
                        return std::pair<std::size_t, std::size_t>{width - 1u - tx, height - 1u - ty};
                    case 270u:
                        return std::pair<std::size_t, std::size_t>{height - 1u - ty, tx};
                    default:
                        return std::pair<std::size_t, std::size_t>{tx, ty};
                    }
                };

            if (resolved.scalePolicy == "crop") {
                const auto [x, y] = applyTransform(
                    cropCoordinate(sampleX, static_cast<std::uint32_t>(sampleWidth), resource.descriptor.width, resolved.anchor),
                    cropCoordinate(sampleY, static_cast<std::uint32_t>(sampleHeight), resource.descriptor.height, resolved.anchor),
                    sampleWidth,
                    sampleHeight);
                return sampleNearest(image, sliceX + x, sliceY + y);
            }

            const auto x = scaledCoordinate(sampleX, static_cast<std::uint32_t>(sampleWidth), resource.descriptor.width);
            const auto y = scaledCoordinate(sampleY, static_cast<std::uint32_t>(sampleHeight), resource.descriptor.height);
            const auto [tx, ty] = applyTransform(static_cast<std::size_t>(x),
                                                 static_cast<std::size_t>(y),
                                                 sampleWidth,
                                                 sampleHeight);
            if (resolved.filterPolicy == "linear" && resolved.transform.rotateDegrees == 0u &&
                !resolved.transform.flipX && !resolved.transform.flipY) {
                return sampleLinear(image,
                                    static_cast<double>(sliceX) + x,
                                    static_cast<double>(sliceY) + y);
            }
            return sampleNearest(image, sliceX + tx, sliceY + ty);
        };

        const auto alphaOver = [](std::uint32_t dst, std::uint32_t src) noexcept {
            const double srcA = static_cast<double>((src >> 24u) & 0xFFu) / 255.0;
            const double dstA = static_cast<double>((dst >> 24u) & 0xFFu) / 255.0;
            const double outA = srcA + dstA * (1.0 - srcA);
            if (outA <= 0.0) {
                return 0u;
            }
            const auto blendChannel = [dst, src, srcA, dstA, outA](int shift) noexcept {
                const double srcC = static_cast<double>((src >> shift) & 0xFFu);
                const double dstC = static_cast<double>((dst >> shift) & 0xFFu);
                return static_cast<std::uint32_t>(std::lround((srcC * srcA + dstC * dstA * (1.0 - srcA)) / outA));
            };
            const auto outAlpha = static_cast<std::uint32_t>(std::lround(outA * 255.0));
            const auto outRed = blendChannel(16);
            const auto outGreen = blendChannel(8);
            const auto outBlue = blendChannel(0);
            return (outAlpha << 24u) | (outRed << 16u) | (outGreen << 8u) | outBlue;
        };

        if (resolved.mode == VisualOverrideMode::ReplaceImage) {
            const auto* image = std::get_if<VisualReplacementImage>(&resolved.payload);
            if (image == nullptr) {
                return std::nullopt;
            }
            const auto sampled = sampleImagePixel(*image);
            return sampled.has_value() ? std::optional<std::uint32_t>(applyPostEffects(*sampled)) : std::nullopt;
        }
        if (resolved.mode == VisualOverrideMode::AnimationGroup) {
            const auto* animation = std::get_if<VisualAnimationGroup>(&resolved.payload);
            if (animation == nullptr || animation->frames.empty() || animation->frameDuration == 0u) {
                return std::nullopt;
            }
            const auto frameIndex = static_cast<std::size_t>(
                (generation / static_cast<std::uint64_t>(animation->frameDuration)) % animation->frames.size());
            const auto sampled = sampleImagePixel(animation->frames[frameIndex]);
            return sampled.has_value() ? std::optional<std::uint32_t>(applyPostEffects(*sampled)) : std::nullopt;
        }
        const auto* layers = std::get_if<std::vector<VisualReplacementImage>>(&resolved.payload);
        if (resolved.mode != VisualOverrideMode::CompositeLayers || layers == nullptr || layers->empty()) {
            return std::nullopt;
        }

        std::uint32_t composed = 0u;
        bool sampledAnyLayer = false;
        for (const auto& layer : *layers) {
            const auto sampled = sampleImagePixel(layer);
            if (!sampled.has_value()) {
                continue;
            }
            composed = alphaOver(composed, *sampled);
            sampledAnyLayer = true;
        }
        if (!sampledAnyLayer) {
            return std::nullopt;
        }
        return applyPostEffects(composed);
    }

    [[nodiscard]] static double scaledCoordinate(std::uint8_t sourceCoordinate,
                                                 std::uint32_t replacementSize,
                                                 std::uint32_t sourceSize) noexcept
    {
        if (sourceSize <= 1u || replacementSize <= 1u) {
            return 0.0;
        }
        return static_cast<double>(sourceCoordinate) * static_cast<double>(replacementSize - 1u) /
               static_cast<double>(sourceSize - 1u);
    }

    [[nodiscard]] static std::size_t cropCoordinate(std::uint8_t sourceCoordinate,
                                                    std::uint32_t replacementSize,
                                                    std::uint32_t sourceSize,
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

    [[nodiscard]] static std::optional<std::uint32_t> sampleNearest(const VisualReplacementImage& image,
                                                                    std::size_t x,
                                                                    std::size_t y) noexcept
    {
        if (image.width == 0 || image.height == 0 || image.argbPixels.empty()) {
            return std::nullopt;
        }
        const auto clampedX = std::min(x, static_cast<std::size_t>(image.width - 1u));
        const auto clampedY = std::min(y, static_cast<std::size_t>(image.height - 1u));
        const auto index = clampedY * static_cast<std::size_t>(image.width) + clampedX;
        if (index >= image.argbPixels.size()) {
            return std::nullopt;
        }
        return image.argbPixels[index];
    }

    [[nodiscard]] static std::optional<std::uint32_t> sampleLinear(const VisualReplacementImage& image,
                                                                   double x,
                                                                   double y) noexcept
    {
        if (image.width == 0 || image.height == 0 || image.argbPixels.empty()) {
            return std::nullopt;
        }
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
        const auto blend = [tx, ty](std::uint32_t c00Value,
                                    std::uint32_t c10Value,
                                    std::uint32_t c01Value,
                                    std::uint32_t c11Value,
                                    int shift) {
            const auto p00 = static_cast<double>((c00Value >> shift) & 0xFFu);
            const auto p10 = static_cast<double>((c10Value >> shift) & 0xFFu);
            const auto p01 = static_cast<double>((c01Value >> shift) & 0xFFu);
            const auto p11 = static_cast<double>((c11Value >> shift) & 0xFFu);
            const auto top = p00 + (p10 - p00) * tx;
            const auto bottom = p01 + (p11 - p01) * tx;
            return static_cast<std::uint32_t>(std::lround(top + (bottom - top) * ty));
        };
        const auto a = blend(*c00, *c10, *c01, *c11, 24);
        const auto r = blend(*c00, *c10, *c01, *c11, 16);
        const auto g = blend(*c00, *c10, *c01, *c11, 8);
        const auto b = blend(*c00, *c10, *c01, *c11, 0);
        return (a << 24u) | (r << 16u) | (g << 8u) | b;
    }

    // Triple-buffer SPSC mailbox:
    // - 3 slots: one private to producer, one private to consumer, one shared (latest frame).
    // - mailboxShared_: bits [2]=dirty (new frame available), bits [1:0]=slot index.
    // - Producer writes to mailboxProducerSlot_, then atomically swaps with mailboxShared_.
    // - Consumer atomically swaps mailboxConsumerSlot_ into mailboxShared_ to take the latest.
    // - No two threads ever access the same slot concurrently.
    static constexpr std::uint8_t kMailboxDirty    = 0x04u;
    static constexpr std::uint8_t kMailboxSlotMask = 0x03u;
    static constexpr int kMailboxSlotCount         = 3;

    std::array<VideoPresentPacket, kMailboxSlotCount> mailboxSlots_{};
    std::atomic<std::uint8_t> mailboxShared_{0x00u}; // (dirty:1 | slotIdx:2)
    int mailboxProducerSlot_{1};  // emulation-thread-private
    int mailboxConsumerSlot_{2};  // render-thread-private

    VideoEngineConfig config_{};
    VisualOverrideService* visualOverrideService_ = nullptr;
    mutable std::unordered_map<std::string, VisualResourceCacheEntry> visualResourceCache_{};
    std::optional<VideoPresentPacket> lastValidFrame_{};
    VideoEngineStats stats_{};
    std::uint64_t currentGeneration_ = 0;
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_ENGINE_HPP
