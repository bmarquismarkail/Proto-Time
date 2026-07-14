#ifndef BMMQ_SIMD_PIXEL_OPS_HPP
#define BMMQ_SIMD_PIXEL_OPS_HPP

// Portable SIMD intrinsics for pixel format conversion and scanline compositing.
//
// Targets: x86 SSE2 / SSE4.1 / AVX2 and ARM NEON, selected at runtime.
// Fallback: scalar C++20. BMMQ_SIMD_FORCE_SCALAR provides a deterministic
// scalar-only build for correctness and benchmark comparisons.
//
// All functions operate on contiguous uint32_t pixel arrays in ARGB8888 layout
// (0xAARRGGBB, big-endian byte order within each 32-bit word).

#include <cstddef>
#include <cstdint>

#include "RealtimeVideoSurface.hpp"

namespace BMMQ {
namespace SimdPixelOps {

enum class Backend : std::uint8_t {
    Scalar = 0,
    Sse2,
    Sse41,
    Avx2,
    Neon,
};

[[nodiscard]] Backend active_backend() noexcept;
[[nodiscard]] const char* active_backend_name() noexcept;

// ---------------------------------------------------------------------------
// ARGB8888 -> RGB565 conversion
// ---------------------------------------------------------------------------
// Input:  std::vector<uint32_t> in ARGB8888 (0xAARRGGBB)
// Output: std::vector<uint16_t> in RGB565 (0-15 R, 0-63 G, 0-15 B)
//
// Processes pixels in vector-width chunks. Tail is handled with scalar fallback.

[[nodiscard]] std::size_t argb8888_to_rgb565_vector_width() noexcept;

void convert_argb8888_to_rgb565(const std::uint32_t* src,
                                std::uint16_t* dst,
                                std::size_t pixel_count) noexcept;

// Convert a packed indexed surface directly to RGB565. The palette is tiny
// (4 entries for Indexed2, 32 for Indexed5), so it is converted once and the
// packed indices expand directly into caller-owned texture storage.
[[nodiscard]] bool convert_indexed_to_rgb565(const RealtimeVideoSurface& surface,
                                             int width,
                                             int height,
                                             std::uint16_t* dst,
                                             std::size_t destination_stride_pixels) noexcept;

// ---------------------------------------------------------------------------
// ARGB8888 -> ARGB8888 visual override pixel replacement
// ---------------------------------------------------------------------------
// Replace pixels in `dst` where `override_mask[i] != 0`, pulling replacement
// color from `replacement` (a single ARGB8888 pixel applied to all masked positions).
//
// This is used for visual override pixel replacement operations.

void replace_pixels_with_color(const std::uint32_t* src,
                               const std::uint8_t* mask,
                               std::uint32_t* dst,
                               std::uint32_t replacement_color,
                               std::size_t pixel_count) noexcept;

void replace_pixels_with_mask(const std::uint32_t* src,
                              const std::uint8_t* mask,
                              const std::uint32_t* replacements,
                              std::uint32_t* dst,
                              std::size_t pixel_count) noexcept;

// ---------------------------------------------------------------------------
// ARGB8888 fill (set a block of pixels to the same color)
// ---------------------------------------------------------------------------

void fill_pixels(std::uint32_t* dst,
                 std::uint32_t color,
                 std::size_t pixel_count) noexcept;

// ---------------------------------------------------------------------------
// Pixel comparison: count differing pixels between two ARGB8888 frames
// ---------------------------------------------------------------------------

[[nodiscard]] std::size_t count_different_pixels(const std::uint32_t* a,
                                                  const std::uint32_t* b,
                                                  std::size_t pixel_count) noexcept;

} // namespace SimdPixelOps
} // namespace BMMQ

#endif // BMMQ_SIMD_PIXEL_OPS_HPP
