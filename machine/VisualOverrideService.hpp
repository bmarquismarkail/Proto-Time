#ifndef BMMQ_VISUAL_OVERRIDE_SERVICE_HPP
#define BMMQ_VISUAL_OVERRIDE_SERVICE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "VisualCaptureWriter.hpp"
#include "VisualPackManifest.hpp"
#include "VisualTypes.hpp"
#include "plugins/IoPlugin.hpp"

namespace BMMQ {

// Forward declarations
class BackgroundTaskService;
class ImageDecoder;

struct VisualCaptureStats {
    std::size_t uniqueResourcesDumped = 0;
    std::size_t duplicateResourcesSkipped = 0;
};

struct VisualOverrideDiagnostics {
    std::size_t rulesLoaded = 0;
    std::size_t invalidRulesSkipped = 0;
    std::size_t missingReplacementImages = 0;
    std::size_t resolveHits = 0;
    std::size_t resolveMisses = 0;
    std::size_t replacementLoadFailures = 0;
    std::size_t ambiguousMatches = 0;
    std::size_t packReloadChecks = 0;
    std::size_t packReloadsSkipped = 0;
    std::size_t packReloadsSucceeded = 0;
    std::size_t packReloadsFailed = 0;
    std::size_t suppressedReloadWarnings = 0;
    std::size_t replacementCacheEvictions = 0;
    // Async visual probe telemetry (Phase 31)
    std::size_t asyncProbeSubmissions = 0;
    std::size_t asyncProbeChangesDetected = 0;
    std::size_t asyncProbeReloadApplies = 0;
    // Async image decode telemetry (Phase 32)
    std::size_t asyncDecodeSubmissions = 0;
    std::size_t asyncDecodePollsReady = 0;
    std::size_t asyncDecodePollsNotReady = 0;
};

struct VisualObservedResourceStat {
    VisualResourceDescriptor descriptor;
    std::size_t observations = 0;
    std::string imagePath;
};

class VisualOverrideService {
public:
    using EventSink = std::function<void(const MachineEvent&)>;

    VisualOverrideService() = default;

    [[nodiscard]] bool enabled() const noexcept;
    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool hasActiveWork() const noexcept;
    [[nodiscard]] bool hasLoadedPacks() const noexcept;
    [[nodiscard]] bool capturing() const noexcept;

    [[nodiscard]] bool loadPackManifest(const std::filesystem::path& manifestPath);
    [[nodiscard]] bool reloadChangedPacks();
    [[nodiscard]] std::optional<ResolvedVisualOverride> resolve(const VisualResourceDescriptor& descriptor);
    [[nodiscard]] std::optional<std::string> takeReloadWarning();

    [[nodiscard]] bool beginCapture(const std::filesystem::path& directory, std::string machineId);
    void endCapture() noexcept;
    [[nodiscard]] bool observe(const DecodedVisualResource& resource);
    void notifyResourceDecoded(const VisualResourceDescriptor& descriptor) const;
    void notifyFrameCompositionStarted(uint64_t generation) const;
    void notifyFrameCompositionCompleted(uint64_t generation) const;

    void setEventSink(EventSink sink);
    void clearEventSink() noexcept;

    // Set optional image decoder service for async PNG decode (Phase 32)
    void setImageDecoder(ImageDecoder* decoder) noexcept;

    [[nodiscard]] const VisualCaptureStats& captureStats() const noexcept;
    [[nodiscard]] const VisualOverrideDiagnostics& diagnostics() const noexcept;
    [[nodiscard]] std::string authorDiagnosticsReport(std::size_t maxObservedResources = 5u) const;
    [[nodiscard]] std::vector<std::filesystem::path> watchedReloadPaths() const;
    [[nodiscard]] std::string lastError() const;
    [[nodiscard]] uint64_t generation() const noexcept;

    // Async probe telemetry recording (Phase 31)
    void recordAsyncProbeSubmission() noexcept;
    void recordAsyncProbeChangeDetected() noexcept;
    void recordAsyncProbeReloadApplied() noexcept;

private:
    struct ResolvedPath {
        std::string packId;
        std::filesystem::path path;
        std::vector<std::filesystem::path> layerPaths;
        std::vector<std::filesystem::path> animationFramePaths;
        std::optional<uint32_t> animationFrameDuration;
        std::vector<VisualPostEffect> effects;
        std::optional<VisualReplacementPalette> palette;
        std::optional<VisualSliceRect> slicing;
        std::optional<VisualTransform> transform;
        std::string scalePolicy;
        std::string filterPolicy;
        std::string anchor;
    };

    struct CachedReplacementImage {
        VisualReplacementImage image;
        std::size_t bytes = 0;
        uint64_t lastUseSerial = 0;
    };

    struct WatchedPathStamp {
        std::filesystem::path path;
        std::filesystem::file_time_type writeTime{};
    };

    struct LoadedPack {
        VisualPackManifest manifest;
        std::filesystem::path manifestPath;
        std::filesystem::file_time_type manifestWriteTime{};
        std::vector<WatchedPathStamp> assetStamps;
    };

    [[nodiscard]] static std::filesystem::file_time_type fileWriteTime(const std::filesystem::path& path) noexcept;
    [[nodiscard]] static std::vector<WatchedPathStamp> collectAssetStamps(const VisualPackManifest& manifest);
    [[nodiscard]] static bool watchedAssetChanged(const LoadedPack& pack) noexcept;
    [[nodiscard]] static std::size_t countLoadedRules(const std::vector<LoadedPack>& packs) noexcept;
    [[nodiscard]] LoadedPack makeLoadedPack(std::filesystem::path manifestPath, VisualPackManifest manifest) const;
    void clearResolutionCaches();
    [[nodiscard]] static std::string makeDescriptorKey(const VisualResourceDescriptor& descriptor);
    [[nodiscard]] static bool matches(const VisualOverrideRule& rule, const VisualResourceDescriptor& descriptor) noexcept;
    [[nodiscard]] std::optional<ResolvedVisualOverride> loadResolved(const ResolvedPath& resolvedPath);
    [[nodiscard]] std::optional<VisualReplacementImage> loadPng(const std::filesystem::path& path);
    bool evictImageCacheFor(std::size_t bytesNeeded);
    [[nodiscard]] bool writeCaptureManifest() const;
    [[nodiscard]] bool writeAuthorReport() const;
    void emitVisualEvent(MachineEventType type, uint64_t tick, std::string_view detail) const;
    void recordObservation(const VisualResourceDescriptor& descriptor);

    bool enabled_ = true;
    bool captureEnabled_ = false;
    uint64_t generation_ = 0;
    std::string captureMachineId_;
    std::filesystem::path captureDirectory_;
    VisualCaptureStats captureStats_{};
    mutable VisualOverrideDiagnostics diagnostics_{};
    std::set<std::string> captureSeen_;
    std::vector<VisualCaptureEntry> captureEntries_;
    std::vector<VisualObservedResourceStat> observedResources_;
    std::unordered_map<std::string, std::size_t> observedResourceIndices_;
    mutable bool captureManifestDirty_ = false;
    std::vector<LoadedPack> packs_;
    std::map<std::string, ResolvedPath> resolvedCache_;
    std::unordered_map<std::string, CachedReplacementImage> imageCache_;
    std::size_t imageCacheBytes_ = 0;
    uint64_t imageCacheUseSerial_ = 0;
    std::optional<std::string> pendingReloadWarning_;
    std::string lastReloadWarning_;
    mutable std::string lastError_;
    EventSink eventSink_;
    ImageDecoder* imageDecoder_ = nullptr;  // Phase 32 async decode
};

} // namespace BMMQ

#endif // BMMQ_VISUAL_OVERRIDE_SERVICE_HPP
