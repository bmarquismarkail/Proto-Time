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
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../VideoDebugModel.hpp"
#include "../../VisualOverrideService.hpp"
#include "SimdPixelOps.hpp"
#include "VideoFrame.hpp"

namespace BMMQ {

struct VideoEngineConfig {
    int frameWidth = 160;
    int frameHeight = 144;
    std::size_t mailboxDepthFrames = 1;
    // HD texture replacement scale factor. When > 1, output frames are scaled
    // by this factor and replaced tiles sample their replacement images at the
    // higher resolution. Default 1 preserves existing behavior.
    int hdScale = 1;
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
    std::size_t buildDebugFrameCallCount = 0;
    std::uint64_t buildDebugFrameTotalNs = 0;
    std::size_t buildDebugFrameRealtimeReasonCount = 0;
    std::size_t buildDebugFrameDebugReasonCount = 0;
    std::size_t buildDebugFrameFallbackReasonCount = 0;
    std::size_t buildDebugFrameUnknownReasonCount = 0;
    std::size_t buildDebugFrameDebugConsumerActiveCount = 0;
    std::size_t buildDebugFrameDebugConsumerInactiveCount = 0;
    std::size_t visualOverrideLookupSampleCount = 0;
    std::uint64_t visualOverrideLookupTotalNs = 0;
    std::uint64_t visualOverrideLookupHighWaterNs = 0;
    std::size_t visualOverrideApplySampleCount = 0;
    std::uint64_t visualOverrideApplyTotalNs = 0;
    std::uint64_t visualOverrideApplyHighWaterNs = 0;
};

struct VideoSubmitResult {
    bool accepted = false;
    bool overwroteOldFrame = false;
};

class VideoEngine {
public:
    enum class BuildDebugFrameReason : std::uint8_t {
        Unknown = 0,
        Realtime = 1,
        Debug = 2,
        Fallback = 3,
    };

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
                                                   std::uint64_t generation,
                                                   BuildDebugFrameReason reason = BuildDebugFrameReason::Unknown,
                                                   bool debugConsumerActive = false) const
    {
        using Clock = std::chrono::steady_clock;
        const auto startedAt = Clock::now();
        ++stats_.buildDebugFrameCallCount;
        switch (reason) {
        case BuildDebugFrameReason::Realtime:
            ++stats_.buildDebugFrameRealtimeReasonCount;
            break;
        case BuildDebugFrameReason::Debug:
            ++stats_.buildDebugFrameDebugReasonCount;
            break;
        case BuildDebugFrameReason::Fallback:
            ++stats_.buildDebugFrameFallbackReasonCount;
            break;
        case BuildDebugFrameReason::Unknown:
        default:
            ++stats_.buildDebugFrameUnknownReasonCount;
            break;
        }
        if (debugConsumerActive) {
            ++stats_.buildDebugFrameDebugConsumerActiveCount;
        } else {
            ++stats_.buildDebugFrameDebugConsumerInactiveCount;
        }

        const int hdScale = std::max(config_.hdScale, 1);
        const int outWidth = config_.frameWidth * hdScale;
        const int outHeight = config_.frameHeight * hdScale;

        VideoFramePacket frame;
        frame.width = outWidth;
        frame.height = outHeight;
        frame.generation = generation;
        frame.source = VideoFrameSource::MachineSnapshot;

        const bool notifyVisualComposition =
            visualOverrideService_ != nullptr && visualOverrideService_->hasActiveWork();
        if (notifyVisualComposition) {
            visualOverrideService_->notifyFrameCompositionStarted(generation);
        }

        const auto pixelCount = static_cast<std::size_t>(config_.frameWidth) * static_cast<std::size_t>(config_.frameHeight);
        std::vector<std::uint32_t> canonicalPixels(pixelCount);
        SimdPixelOps::fill_pixels(canonicalPixels.data(), 0xFF000000u, pixelCount);

        if (model.empty()) {
            if (notifyVisualComposition) {
                visualOverrideService_->notifyFrameCompositionCompleted(generation);
            }
            // Upscale blank frame to HD if needed
            if (hdScale > 1) {
                const auto hdPixelCount = static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight);
                frame.pixels.assign(hdPixelCount, canonicalPixels.front());
            } else {
                frame.pixels = std::move(canonicalPixels);
            }
            stats_.buildDebugFrameTotalNs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt).count());
            return frame;
        }

        resetVisualResourceCache(model.resources.size());

        // HD replacement tracking: which canonical pixels have replacements
        // Use reusable scratch buffers to avoid per-frame allocation on the composition lane
        if (hdResolvedOverrides_.size() != pixelCount) {
            hdResolvedOverrides_.resize(pixelCount);
        }
        std::fill(hdResolvedOverrides_.begin(), hdResolvedOverrides_.end(), nullptr);

        const auto copyCount = std::min(canonicalPixels.size(), model.argbPixels.size());
        std::memcpy(canonicalPixels.data(), model.argbPixels.data(),
                    copyCount * sizeof(std::uint32_t));

        if (hdScale > 1) {
            // HD path: sample replacements at HD resolution with proper sub-pixel coordinates
            const auto lookupStartedAt = Clock::now();

            // First pass: identify which canonical pixels have replacements and cache the resolved overrides
            bool hasReplacement = false;
            if (visualOverrideService_ != nullptr) {
                for (std::size_t i = 0; i < copyCount && i < model.semantics.size(); ++i) {
                    const auto& semantic = model.semantics[i];
                    if (!semantic.hasResource() || semantic.resourceIndex >= model.resources.size()) {
                        continue;
                    }
                    // Use shared observe+resolve helper to preserve capture path
                    if (const auto* resolved =
                            resolveReplacementForResource(model.resources[semantic.resourceIndex])) {
                        hdResolvedOverrides_[i] = resolved;
                        hasReplacement = true;
                    }
                }
            }

            const auto lookupDuration = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - lookupStartedAt).count());
            ++stats_.visualOverrideLookupSampleCount;
            stats_.visualOverrideLookupTotalNs += lookupDuration;
            stats_.visualOverrideLookupHighWaterNs = std::max(
                stats_.visualOverrideLookupHighWaterNs, lookupDuration);

            // Build HD output frame
            const auto hdPixelCount = static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight);
            frame.pixels.resize(hdPixelCount);

            if (hasReplacement) {
                const auto applyStartedAt = Clock::now();
                // For each HD output pixel, compute proper sub-pixel coordinates
                for (std::size_t oy = 0; oy < static_cast<std::size_t>(outHeight); ++oy) {
                    const std::size_t srcY = oy / hdScale;
                    const std::size_t dstRowStart = oy * static_cast<std::size_t>(outWidth);
                    for (std::size_t ox = 0; ox < static_cast<std::size_t>(outWidth); ++ox) {
                        const std::size_t srcIdx = srcY * static_cast<std::size_t>(config_.frameWidth) + (ox / hdScale);

                        if (const auto* resolved = hdResolvedOverrides_[srcIdx]) {
                            // HD sampling: compute integer output coordinates directly
                            // qx = semantic.sampleX * hdScale + subX gives exact 1:1 mapping
                            // when replacement dimensions equal source dimensions * hdScale
                            const auto& semantic = model.semantics[srcIdx];
                            const auto& resource = model.resources[semantic.resourceIndex];
                            // Compute integer output coordinates with sub-pixel index
                            const std::size_t qx = semantic.sampleX * static_cast<std::size_t>(hdScale) + (ox % hdScale);
                            const std::size_t qy = semantic.sampleY * static_cast<std::size_t>(hdScale) + (oy % hdScale);

                                // For HD sampling, pass q-space coordinates to the shared sampler.
                                // The sampler normalizes against resource.descriptor.width*hdScale to
                                // preserve exact 1:1 mapping when replacement dimensions match.
                            const double sampleX = static_cast<double>(qx);
                            const double sampleY = static_cast<double>(qy);

                            if (const auto replacement = sampleReplacementPixelAtCoordinate(
                                    *resolved, resource, sampleX, sampleY, generation,
                                    static_cast<std::uint32_t>(resource.descriptor.width) * static_cast<std::uint32_t>(hdScale),
                                    static_cast<std::uint32_t>(resource.descriptor.height) * static_cast<std::uint32_t>(hdScale))) {
                                frame.pixels[dstRowStart + ox] = *replacement;
                                continue;
                            }
                            // Fallback to canonical pixel if sampling fails
                        }
                        frame.pixels[dstRowStart + ox] = canonicalPixels[srcIdx];
                    }
                }
                const auto applyDuration = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - applyStartedAt).count());
                ++stats_.visualOverrideApplySampleCount;
                stats_.visualOverrideApplyTotalNs += applyDuration;
                stats_.visualOverrideApplyHighWaterNs = std::max(
                    stats_.visualOverrideApplyHighWaterNs, applyDuration);
            } else {
                // No replacements: simple nearest-neighbor upscale
                upscaleNearest(canonicalPixels, config_.frameWidth, config_.frameHeight,
                               hdScale, frame.pixels);
            }
        } else if (notifyVisualComposition) {
            // Non-HD path: existing behavior
            if (visualReplacementMask_.size() != copyCount) {
                visualReplacementMask_.resize(copyCount);
                visualReplacementPixels_.resize(copyCount);
            }
            std::fill(visualReplacementMask_.begin(), visualReplacementMask_.end(), 0u);
            const auto lookupStartedAt = Clock::now();
            bool hasReplacement = false;
            for (std::size_t i = 0; i < copyCount && i < model.semantics.size(); ++i) {
                const auto& semantic = model.semantics[i];
                if (!semantic.hasResource() || semantic.resourceIndex >= model.resources.size()) {
                    continue;
                }
                if (const auto replacement = replacementPixelForResource(model.resources[semantic.resourceIndex],
                                                                          semantic.sampleX,
                                                                          semantic.sampleY,
                                                                          generation);
                    replacement.has_value()) {
                    visualReplacementMask_[i] = 1u;
                    visualReplacementPixels_[i] = *replacement;
                    hasReplacement = true;
                }
            }
            const auto lookupDuration = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - lookupStartedAt).count());
            ++stats_.visualOverrideLookupSampleCount;
            stats_.visualOverrideLookupTotalNs += lookupDuration;
            stats_.visualOverrideLookupHighWaterNs = std::max(
                stats_.visualOverrideLookupHighWaterNs, lookupDuration);

            if (hasReplacement) {
                const auto applyStartedAt = Clock::now();
                SimdPixelOps::replace_pixels_with_mask(canonicalPixels.data(),
                                                       visualReplacementMask_.data(),
                                                       visualReplacementPixels_.data(),
                                                       canonicalPixels.data(),
                                                       copyCount);
                const auto applyDuration = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - applyStartedAt).count());
                ++stats_.visualOverrideApplySampleCount;
                stats_.visualOverrideApplyTotalNs += applyDuration;
                stats_.visualOverrideApplyHighWaterNs = std::max(
                    stats_.visualOverrideApplyHighWaterNs, applyDuration);
            }
            frame.pixels = std::move(canonicalPixels);
        } else {
            frame.pixels = std::move(canonicalPixels);
        }

        if (notifyVisualComposition) {
            visualOverrideService_->notifyFrameCompositionCompleted(generation);
        }
        stats_.buildDebugFrameTotalNs += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt).count());
        return frame;
    }

    [[nodiscard]] VideoSubmitResult submitFrame(const VideoFramePacket& frame)
    {
        return submitPresentPacket(makePresentPacket(VideoFramePacket(frame)));
    }

    [[nodiscard]] VideoSubmitResult submitPresentPacket(VideoPresentPacket frame)
    {
        if (frame.empty()) {
            return {};
        }

        auto stamped = std::move(frame);
        stamped.publishedAtNs = steadyClockNs();
        const auto publishedSource = stamped.source;
        const auto publishedPayloadBytes = stamped.payloadBytes();
        const auto publishedGeneration = stamped.generation;
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
        if (publishedSource == VideoFrameSource::RealtimeSnapshot) {
            ++stats_.publishedRealtimeFrameCount;
            stats_.publishedRealtimePixelBytes += publishedPayloadBytes;
        } else {
            ++stats_.publishedDebugFrameCount;
            stats_.publishedDebugPixelBytes += publishedPayloadBytes;
        }
        stats_.publishedPixelBytes += publishedPayloadBytes;

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
        stats_.lastPublishedGeneration = publishedGeneration;
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
        auto frame = std::move(mailboxSlots_[mailboxConsumerSlot_]);

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
        const int hdScale = std::max(config_.hdScale, 1);
        return makePresentPacket(makeBlankVideoFrame(
            config_.frameWidth * hdScale, config_.frameHeight * hdScale, currentGeneration_));
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
        config.mailboxDepthFrames = 1u;
        // Clamp hdScale to [1, 8] for direct API callers.
        config.hdScale = std::clamp(config.hdScale, 1, 8);
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

    void resetVisualResourceCache(std::size_t expectedResources) const
    {
        visualResourceCache_.clear();
        visualResourceCache_.reserve(expectedResources);
    }

    static void upscaleNearest(std::span<const std::uint32_t> source,
                               int width, int height, int scale,
                               std::vector<std::uint32_t>& output)
    {
        const auto outWidth = static_cast<std::size_t>(width * scale);
        const auto outHeight = static_cast<std::size_t>(height * scale);
        output.resize(outWidth * outHeight);
        for (std::size_t y = 0; y < outHeight; ++y) {
            const auto sourceRow = (y / static_cast<std::size_t>(scale)) *
                                   static_cast<std::size_t>(width);
            const auto outputRow = y * outWidth;
            for (std::size_t x = 0; x < outWidth; ++x) {
                output[outputRow + x] = source[sourceRow + x / static_cast<std::size_t>(scale)];
            }
        }
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

    // Shared observe+resolve helper. Returns true if a replacement was resolved
    // and cached; false otherwise. Owned by both HD mask and canonical sampling paths.
    [[nodiscard]] const ResolvedVisualOverride* resolveReplacementForResource(
        const DecodedVisualResource& resource) const
    {
        if (visualOverrideService_ == nullptr || !visualOverrideService_->hasActiveWork()) {
            return nullptr;
        }

        auto& entry = visualResourceCache_[resourceCacheKey(resource.descriptor)];
        if (!entry.observed) {
            entry.observed = true;
            entry.resource = &resource;
            visualOverrideService_->notifyResourceDecoded(resource.descriptor);
            (void)visualOverrideService_->observe(resource);
        }

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

        return entry.resolved.has_value() ? &*entry.resolved : nullptr;
    }

    [[nodiscard]] std::optional<std::uint32_t> replacementPixelForResource(const DecodedVisualResource& resource,
                                                                           std::uint8_t sampleX,
                                                                           std::uint8_t sampleY,
                                                                           std::uint64_t generation) const
    {
        const auto* resolved = resolveReplacementForResource(resource);
        if (resolved == nullptr) {
            return std::nullopt;
        }
        return sampleReplacementPixel(*resolved, resource, sampleX, sampleY, generation);
    }

    [[nodiscard]] static bool hasResolvedPayload(const ResolvedVisualOverride& resolved) noexcept
    {
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
    }

    [[nodiscard]] static std::optional<std::uint32_t> sampleReplacementPixel(const ResolvedVisualOverride& resolved,
                                                                              const DecodedVisualResource& resource,
                                                                              std::uint8_t sampleX,
                                                                              std::uint8_t sampleY,
                                                                              std::uint64_t generation) noexcept
    {
        return sampleReplacementPixelAtCoordinate(resolved, resource,
                                                  static_cast<double>(sampleX),
                                                  static_cast<double>(sampleY),
                                                  generation,
                                                  0u, 0u);
    }

    // HD path: sample replacement at continuous coordinates with sub-pixel precision
    [[nodiscard]] static std::optional<std::uint32_t> sampleReplacementPixelAtCoordinate(
        const ResolvedVisualOverride& resolved,
        const DecodedVisualResource& resource,
        double sampleX,
        double sampleY,
        std::uint64_t generation,
        std::uint32_t effectiveSourceWidth = 0u,
        std::uint32_t effectiveSourceHeight = 0u) noexcept
    {
        const std::uint32_t srcW = effectiveSourceWidth != 0u ? effectiveSourceWidth : resource.descriptor.width;
        const std::uint32_t srcH = effectiveSourceHeight != 0u ? effectiveSourceHeight : resource.descriptor.height;

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
            // Map q-space coordinates back to canonical tile coordinates for palette lookup
            const std::size_t canonX = srcW > 1u ? static_cast<std::size_t>(std::floor(sampleX * static_cast<double>(resource.descriptor.width) / srcW)) : 0u;
            const std::size_t canonY = srcH > 1u ? static_cast<std::size_t>(std::floor(sampleY * static_cast<double>(resource.descriptor.height) / srcH)) : 0u;
            if (canonX >= resource.descriptor.width || canonY >= resource.descriptor.height || resource.stride == 0u) {
                return std::nullopt;
            }
            const auto index = canonY * resource.stride + canonX;
            if (index >= resource.pixels.size()) {
                return std::nullopt;
            }
            return applyPostEffects((*palette)[resource.pixels[index] & 0x03u]);
        }

        const auto sampleImagePixel = [&resolved, &resource, sampleX, sampleY, srcW, srcH](const VisualReplacementImage& image)
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
                (sampleWidth != srcW || sampleHeight != srcH)) {
                return std::nullopt;
            }

            const auto applyTransform =
                [&resolved](double x, double y, std::size_t width, std::size_t height) noexcept {
                    double tx = x;
                    double ty = y;
                    if (resolved.transform.flipX && width > 0u) {
                        tx = static_cast<double>(width - 1u) - x;
                    }
                    if (resolved.transform.flipY && height > 0u) {
                        ty = static_cast<double>(height - 1u) - y;
                    }
                    switch (resolved.transform.rotateDegrees) {
                    case 90u:
                        return std::pair<double, double>{ty, static_cast<double>(width - 1u) - tx};
                    case 180u:
                        return std::pair<double, double>{static_cast<double>(width - 1u) - tx, static_cast<double>(height - 1u) - ty};
                    case 270u:
                        return std::pair<double, double>{static_cast<double>(height - 1u) - ty, tx};
                    default:
                        return std::pair<double, double>{tx, ty};
                    }
                };

            if (resolved.scalePolicy == "crop") {
                const auto [x, y] = applyTransform(
                    cropCoordinateDouble(sampleX, static_cast<std::uint32_t>(sampleWidth), srcW, resolved.anchor),
                    cropCoordinateDouble(sampleY, static_cast<std::uint32_t>(sampleHeight), srcH, resolved.anchor),
                    sampleWidth,
                    sampleHeight);
                return sampleNearest(image,
                    sliceX + static_cast<std::size_t>(std::llround(std::max(x, 0.0))),
                    sliceY + static_cast<std::size_t>(std::llround(std::max(y, 0.0))));
            }

            const auto x = scaledCoordinateDouble(sampleX, static_cast<std::uint32_t>(sampleWidth), srcW);
            const auto y = scaledCoordinateDouble(sampleY, static_cast<std::uint32_t>(sampleHeight), srcH);
            const auto [tx, ty] = applyTransform(x, y, sampleWidth, sampleHeight);
            if (resolved.filterPolicy == "linear" && resolved.transform.rotateDegrees == 0u &&
                !resolved.transform.flipX && !resolved.transform.flipY) {
                return sampleLinear(image,
                                    static_cast<double>(sliceX) + x,
                                    static_cast<double>(sliceY) + y);
            }
            return sampleNearest(image,
                sliceX + static_cast<std::size_t>(std::llround(std::max(tx, 0.0))),
                sliceY + static_cast<std::size_t>(std::llround(std::max(ty, 0.0))));
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

    // HD path: double-precision version for sub-pixel sampling
    [[nodiscard]] static double scaledCoordinateDouble(double sourceCoordinate,
                                                       std::uint32_t replacementSize,
                                                       std::uint32_t sourceSize) noexcept
    {
        if (sourceSize <= 1u || replacementSize <= 1u) {
            return 0.0;
        }
        return sourceCoordinate * static_cast<double>(replacementSize - 1u) /
               static_cast<double>(sourceSize - 1u);
    }

    // HD path: double-precision version for sub-pixel sampling
    [[nodiscard]] static std::size_t cropCoordinateDouble(double sourceCoordinate,
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
    mutable std::vector<std::uint8_t> visualReplacementMask_{};
    mutable std::vector<std::uint32_t> visualReplacementPixels_{};
    // HD scratch buffers for replacement-aware upscaling
    mutable std::vector<const ResolvedVisualOverride*> hdResolvedOverrides_{};
    std::optional<VideoPresentPacket> lastValidFrame_{};
    mutable VideoEngineStats stats_{};
    std::uint64_t currentGeneration_ = 0;
};

} // namespace BMMQ

#endif // BMMQ_VIDEO_ENGINE_HPP
