#ifndef BMMQ_SIMD_PIXEL_OPS_HPP
#define BMMQ_SIMD_PIXEL_OPS_HPP

// Portable SIMD intrinsics for pixel format conversion and scanline compositing.
//
// Targets: x86 SSE2 / SSSE3 / SSE4.1 / AVX2 (auto-detected via compiler defines).
// Fallbacks: scalar C++20 when no SIMD detected.
//
// All functions operate on contiguous uint32_t pixel arrays in ARGB8888 layout
// (0xAARRGGBB, big-endian byte order within each 32-bit word).

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__has_include)
#  if __has_include(<immintrin.h>)
#    include <immintrin.h>
#  endif
#endif

// Feature detection — prefer the highest available ISA.
#if defined(BMMQ_SIMD_FORCE_SCALAR) && BMMQ_SIMD_FORCE_SCALAR
#  define BMMQ_SIMD_SCALAR 1
#elif defined(__AVX2__) && defined(__AVX__) && defined(__SSE4_2__)
#  define BMMQ_SIMD_AVX2 1
#elif defined(__SSE4_2__) && defined(__SSE4_1__) && defined(__SSSE3__)
#  define BMMQ_SIMD_SSE4_1 1
#elif defined(__SSSE3__)
#  define BMMQ_SIMD_SSSE3 1
#elif defined(__SSE2__)
#  define BMMQ_SIMD_SSE2 1
#else
#  define BMMQ_SIMD_SCALAR 1
#endif

namespace BMMQ {
namespace SimdPixelOps {

// ---------------------------------------------------------------------------
// ARGB8888 -> RGB565 conversion
// ---------------------------------------------------------------------------
// Input:  std::vector<uint32_t> in ARGB8888 (0xAARRGGBB)
// Output: std::vector<uint16_t> in RGB565 (0-15 R, 0-63 G, 0-15 B)
//
// Processes pixels in vector-width chunks. Tail is handled with scalar fallback.

[[nodiscard]] inline std::size_t argb8888_to_rgb565_vector_width() noexcept
{
#if BMMQ_SIMD_AVX2
    return 8u;          // 256 bits -> 8 x uint32_t
#elif BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE2
    return 4u;          // 128 bits -> 4 x uint32_t
#else
    return 1u;          // scalar
#endif
}

void convert_argb8888_to_rgb565(const std::uint32_t* src,
                                std::uint16_t* dst,
                                std::size_t pixel_count) noexcept;

// ---------------------------------------------------------------------------
// ARGB8888 -> ARGB8888 visual override (pixel replacement with alpha blend)
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
// ARGB8888 scanline compositing (overlay one row onto another with alpha)
// ---------------------------------------------------------------------------
// Composite `overlay` pixels over `base` pixels where overlay alpha > 0.
// Both arrays must have the same length. Writes result into `dst`.

void composite_scanline_alpha(const std::uint32_t* base,
                              const std::uint32_t* overlay,
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
