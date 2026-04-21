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
    std::string machineId;
    std::string semanticLabel;
    VisualResourceKind kind = VisualResourceKind::Unknown;
    VisualPixelFormat decodedFormat = VisualPixelFormat::Unknown;
    VisualResourceHash sourceHash = 0;
    VisualResourceHash decodedHash = 0;
    VisualResourceHash paletteHash = 0;
    VisualResourceHash paletteAwareHash = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::optional<uint32_t> sourceBank;
    std::optional<uint32_t> sourceAddress;
    std::optional<uint32_t> sourceIndex;
    std::optional<uint32_t> paletteValue;
    std::string paletteRegister;
    // Replacement mode fields are mutually exclusive: use `image` for one static PNG, `layers`
    // for an ordered list of PNGs composited front-to-back, or `animationFrames` for an ordered
    // frame sequence. `animationFrameDuration` is the per-frame duration in milliseconds and is
    // only set when `animationFrames` is present.
    std::filesystem::path image;
    std::vector<std::filesystem::path> layers;
    std::vector<std::filesystem::path> animationFrames;
    std::optional<uint32_t> animationFrameDuration;
    // Post-effects support Invert, Grayscale, Multiply, and AlphaScale. They can be applied to
    // any replacement payload (`image`, composited `layers`, or `animationFrames`) and run in
    // declaration order from first to last.
    std::vector<VisualPostEffect> effects;
    std::optional<VisualReplacementPalette> palette;
    std::optional<VisualSliceRect> slicing;
    std::optional<VisualTransform> transform;
    std::string scalePolicy;
    std::string filterPolicy;
    std::string anchor;
    std::size_t order = 0;
    uint32_t specificity = 0;
};

struct VisualPackManifest {
    std::string id;
    std::string name;
    std::string target;
    std::vector<std::string> targets;
    uint32_t priority = 0;
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
