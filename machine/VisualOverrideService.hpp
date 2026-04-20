#ifndef BMMQ_VISUAL_OVERRIDE_SERVICE_HPP
#define BMMQ_VISUAL_OVERRIDE_SERVICE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "VisualCaptureWriter.hpp"
#include "VisualPackManifest.hpp"
#include "VisualTypes.hpp"

namespace BMMQ {

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
};

class VisualOverrideService {
public:
    VisualOverrideService() = default;

    [[nodiscard]] bool enabled() const noexcept;
    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool hasActiveWork() const noexcept;
    [[nodiscard]] bool hasLoadedPacks() const noexcept;
    [[nodiscard]] bool capturing() const noexcept;

    [[nodiscard]] bool loadPackManifest(const std::filesystem::path& manifestPath);
    [[nodiscard]] std::optional<ResolvedVisualOverride> resolve(const VisualResourceDescriptor& descriptor);

    [[nodiscard]] bool beginCapture(const std::filesystem::path& directory, std::string machineId);
    void endCapture() noexcept;
    [[nodiscard]] bool observe(const DecodedVisualResource& resource);

    [[nodiscard]] const VisualCaptureStats& captureStats() const noexcept;
    [[nodiscard]] const VisualOverrideDiagnostics& diagnostics() const noexcept;
    [[nodiscard]] std::string lastError() const;
    [[nodiscard]] uint64_t generation() const noexcept;

private:
    struct ResolvedPath {
        std::string packId;
        std::filesystem::path path;
    };

    [[nodiscard]] static std::string makeDescriptorKey(const VisualResourceDescriptor& descriptor);
    [[nodiscard]] static bool matches(const VisualOverrideRule& rule, const VisualResourceDescriptor& descriptor) noexcept;
    [[nodiscard]] std::optional<ResolvedVisualOverride> loadResolved(const ResolvedPath& resolvedPath);
    [[nodiscard]] std::optional<VisualReplacementImage> loadPng(const std::filesystem::path& path);
    [[nodiscard]] bool writeCaptureManifest() const;

    bool enabled_ = true;
    bool captureEnabled_ = false;
    uint64_t generation_ = 0;
    std::string captureMachineId_;
    std::filesystem::path captureDirectory_;
    VisualCaptureStats captureStats_{};
    mutable VisualOverrideDiagnostics diagnostics_{};
    std::set<std::string> captureSeen_;
    std::vector<VisualCaptureEntry> captureEntries_;
    mutable bool captureManifestDirty_ = false;
    std::vector<VisualPackManifest> packs_;
    std::map<std::string, ResolvedPath> resolvedCache_;
    std::unordered_map<std::string, VisualReplacementImage> imageCache_;
    std::size_t imageCacheBytes_ = 0;
    mutable std::string lastError_;
};

} // namespace BMMQ

#endif // BMMQ_VISUAL_OVERRIDE_SERVICE_HPP
