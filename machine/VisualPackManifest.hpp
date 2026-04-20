#ifndef BMMQ_VISUAL_PACK_MANIFEST_HPP
#define BMMQ_VISUAL_PACK_MANIFEST_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "VisualTypes.hpp"

namespace BMMQ {

struct VisualOverrideRule {
    VisualResourceKind kind = VisualResourceKind::Unknown;
    VisualResourceHash decodedHash = 0;
    VisualResourceHash paletteHash = 0;
    VisualResourceHash paletteAwareHash = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::filesystem::path image;
    std::size_t order = 0;
    uint32_t specificity = 0;
};

struct VisualPackManifest {
    std::string id;
    std::string name;
    std::string target;
    std::filesystem::path root;
    std::vector<VisualOverrideRule> rules;
};

struct VisualPackManifestLoadResult {
    std::optional<VisualPackManifest> manifest;
    std::size_t invalidRulesSkipped = 0;
    std::size_t missingReplacementImages = 0;
    std::string error;
};

[[nodiscard]] VisualPackManifestLoadResult loadVisualPackManifest(const std::filesystem::path& manifestPath);

} // namespace BMMQ

#endif // BMMQ_VISUAL_PACK_MANIFEST_HPP
