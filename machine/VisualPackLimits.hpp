#ifndef BMMQ_VISUAL_PACK_LIMITS_HPP
#define BMMQ_VISUAL_PACK_LIMITS_HPP

#include <cstddef>

namespace BMMQ {

struct VisualPackLimits {
    static constexpr std::size_t kMaxManifestBytes = 1024u * 1024u;
    static constexpr std::size_t kMaxManifestRules = 1024u;
    static constexpr std::size_t kMaxPngBytes = 8u * 1024u * 1024u;
    static constexpr std::size_t kMaxReplacementImageDimension = 2048;
    static constexpr std::size_t kMaxReplacementBytesPerPixel = 4u;
    static constexpr std::size_t kMaxReplacementInflatedBytes = 16u * 1024u * 1024u;
    static constexpr std::size_t kMaxReplacementCacheBytes = 64u * 1024u * 1024u;
};

static_assert(
    VisualPackLimits::kMaxReplacementImageDimension *
        VisualPackLimits::kMaxReplacementImageDimension *
        VisualPackLimits::kMaxReplacementBytesPerPixel <= VisualPackLimits::kMaxReplacementInflatedBytes,
    "replacement image dimension limit must fit within the inflated RGBA byte budget");

} // namespace BMMQ

#endif // BMMQ_VISUAL_PACK_LIMITS_HPP
