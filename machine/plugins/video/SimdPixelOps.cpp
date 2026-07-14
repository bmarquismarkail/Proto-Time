#include "SimdPixelOps.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#  define BMMQ_SIMD_BUILD_X86 1
#  include <immintrin.h>
#  if defined(_MSC_VER)
#    include <intrin.h>
#  endif
#else
#  define BMMQ_SIMD_BUILD_X86 0
#endif

#if BMMQ_SIMD_BUILD_X86 && ((defined(__GNUC__) || defined(__clang__)) || defined(__AVX2__))
#  define BMMQ_SIMD_BUILD_AVX2 1
#else
#  define BMMQ_SIMD_BUILD_AVX2 0
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#  define BMMQ_SIMD_BUILD_NEON 1
#  include <arm_neon.h>
#else
#  define BMMQ_SIMD_BUILD_NEON 0
#endif

#if (defined(__GNUC__) || defined(__clang__)) && BMMQ_SIMD_BUILD_X86
#  define BMMQ_TARGET_SSE2 __attribute__((target("sse2")))
#  define BMMQ_TARGET_SSSE3 __attribute__((target("ssse3")))
#  define BMMQ_TARGET_SSE41 __attribute__((target("sse4.1")))
#  define BMMQ_TARGET_AVX2 __attribute__((target("avx2")))
#else
#  define BMMQ_TARGET_SSE2
#  define BMMQ_TARGET_SSSE3
#  define BMMQ_TARGET_SSE41
#  define BMMQ_TARGET_AVX2
#endif

namespace BMMQ {
namespace SimdPixelOps {
namespace {

// ---------------------------------------------------------------------------
// Scalar helpers (used for tail processing and as standalone fallback)
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::uint16_t argb8888_to_rgb565_single(std::uint32_t argb) noexcept
{
    // ARGB8888: 0xAARRGGBB
    const auto r = (argb >> 16u) & 0xFFu;
    const auto g = (argb >> 8u) & 0xFFu;
    const auto b = argb & 0xFFu;
    return static_cast<std::uint16_t>(
        ((r >> 3u) << 11u) |
        ((g >> 2u) << 5u) |
        (b >> 3u));
}

void convert_argb8888_to_rgb565_scalar(const std::uint32_t* src,
                                       std::uint16_t* dst,
                                       std::size_t pixel_count) noexcept
{
    for (std::size_t i = 0; i < pixel_count; ++i) {
        dst[i] = argb8888_to_rgb565_single(src[i]);
    }
}

void replace_pixels_scalar(const std::uint32_t* src,
                           const std::uint8_t* mask,
                           std::uint32_t* dst,
                           std::uint32_t replacement_color,
                           std::size_t pixel_count) noexcept
{
    for (std::size_t i = 0; i < pixel_count; ++i) {
        if (mask[i] != 0u) {
            dst[i] = replacement_color;
        } else {
            dst[i] = src[i];
        }
    }
}

void replace_pixels_mask_scalar(const std::uint32_t* src,
                                const std::uint8_t* mask,
                                const std::uint32_t* replacements,
                                std::uint32_t* dst,
                                std::size_t pixel_count) noexcept
{
    for (std::size_t i = 0; i < pixel_count; ++i) {
        dst[i] = (mask[i] != 0u) ? replacements[i] : src[i];
    }
}

void fill_pixels_scalar(std::uint32_t* dst,
                        std::uint32_t color,
                        std::size_t pixel_count) noexcept
{
    for (std::size_t i = 0; i < pixel_count; ++i) {
        dst[i] = color;
    }
}

// ---------------------------------------------------------------------------
// SSE2 implementations
// ---------------------------------------------------------------------------

#if BMMQ_SIMD_BUILD_X86

BMMQ_TARGET_SSE2 void convert_argb8888_to_rgb565_sse2(const std::uint32_t* src,
                                     std::uint16_t* dst,
                                     std::size_t pixel_count) noexcept
{
    // Process 4 pixels at a time (128 bits = 4 x uint32_t)
    const auto vec_count = pixel_count / 4u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto pix = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 4));

        auto r = _mm_and_si128(_mm_srli_epi32(pix, 19), _mm_set1_epi32(0x1F));
        auto g = _mm_and_si128(_mm_srli_epi32(pix, 10), _mm_set1_epi32(0x3F));
        auto b = _mm_and_si128(_mm_srli_epi32(pix, 3), _mm_set1_epi32(0x1F));
        auto rgb565 = _mm_or_si128(
            _mm_or_si128(_mm_slli_epi32(r, 11), _mm_slli_epi32(g, 5)),
            b);

        alignas(16) std::uint32_t tmp[4];
        _mm_store_si128(reinterpret_cast<__m128i*>(tmp), rgb565);
        dst[i * 4 + 0] = static_cast<std::uint16_t>(tmp[0]);
        dst[i * 4 + 1] = static_cast<std::uint16_t>(tmp[1]);
        dst[i * 4 + 2] = static_cast<std::uint16_t>(tmp[2]);
        dst[i * 4 + 3] = static_cast<std::uint16_t>(tmp[3]);
    }

    // Tail
    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        dst[i] = argb8888_to_rgb565_single(src[i]);
    }
}

BMMQ_TARGET_SSE41 void convert_argb8888_to_rgb565_sse41(const std::uint32_t* src,
                                                        std::uint16_t* dst,
                                                        std::size_t pixel_count) noexcept
{
    const auto vec_count = pixel_count / 4u;
    for (std::size_t i = 0; i < vec_count; ++i) {
        const auto pix = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 4u));
        const auto r = _mm_and_si128(_mm_srli_epi32(pix, 19), _mm_set1_epi32(0x1F));
        const auto g = _mm_and_si128(_mm_srli_epi32(pix, 10), _mm_set1_epi32(0x3F));
        const auto b = _mm_and_si128(_mm_srli_epi32(pix, 3), _mm_set1_epi32(0x1F));
        const auto packed32 = _mm_or_si128(
            _mm_or_si128(_mm_slli_epi32(r, 11), _mm_slli_epi32(g, 5)), b);
        const auto packed16 = _mm_packus_epi32(packed32, _mm_setzero_si128());
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + i * 4u), packed16);
    }
    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        dst[i] = argb8888_to_rgb565_single(src[i]);
    }
}

BMMQ_TARGET_SSSE3 void replace_pixels_ssse3(const std::uint32_t* src,
                          const std::uint8_t* mask,
                          std::uint32_t* dst,
                          std::uint32_t replacement_color,
                          std::size_t pixel_count) noexcept
{
    // Process 4 pixels at a time (128 bits = 4 x uint32_t or 16 x uint8_t)
    const auto vec_count = pixel_count / 4u;
    const auto repl_vec = _mm_set1_epi32(replacement_color);

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto pix = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 4));
        auto mask_vec = _mm_loadu_si32(mask + i * 4);

        // Shuffle mask so each uint8_t is replicated 4 times -> creates 4 x int32 selectors
        // Control vector for pshufb: duplicate each byte 4 times
        const auto shuffle_mask = _mm_setr_epi8(
            0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3);
        auto expanded = _mm_shuffle_epi8(mask_vec, shuffle_mask);

        auto keep_src = _mm_cmpeq_epi32(expanded, _mm_setzero_si128());
        auto masked_src = _mm_and_si128(keep_src, pix);
        auto masked_repl = _mm_andnot_si128(keep_src, repl_vec);
        auto result = _mm_or_si128(masked_src, masked_repl);

        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4), result);
    }

    // Tail
    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        dst[i] = (mask[i] != 0u) ? replacement_color : src[i];
    }
}

BMMQ_TARGET_SSSE3 void replace_pixels_mask_ssse3(const std::uint32_t* src,
                               const std::uint8_t* mask,
                               const std::uint32_t* replacements,
                               std::uint32_t* dst,
                               std::size_t pixel_count) noexcept
{
    const auto vec_count = pixel_count / 4u;
    const auto shuffle_mask = _mm_setr_epi8(
        0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3);

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto pix = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 4));
        auto repl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(replacements + i * 4));
        auto mask_vec = _mm_loadu_si32(mask + i * 4);
        auto expanded = _mm_shuffle_epi8(mask_vec, shuffle_mask);

        auto keep_src = _mm_cmpeq_epi32(expanded, _mm_setzero_si128());
        auto masked_src = _mm_and_si128(keep_src, pix);
        auto masked_repl = _mm_andnot_si128(keep_src, repl);
        auto result = _mm_or_si128(masked_src, masked_repl);

        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4), result);
    }

    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        dst[i] = (mask[i] != 0u) ? replacements[i] : src[i];
    }
}

BMMQ_TARGET_SSE2 void fill_pixels_sse2(std::uint32_t* dst,
                      std::uint32_t color,
                      std::size_t pixel_count) noexcept
{
    const auto vec = _mm_set1_epi32(color);
    const auto vec_count = pixel_count / 4u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4), vec);
    }

    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        dst[i] = color;
    }
}

#endif // SSE2+

// ---------------------------------------------------------------------------
// AVX2 implementations (wider vectors)
// ---------------------------------------------------------------------------

#if BMMQ_SIMD_BUILD_AVX2
BMMQ_TARGET_AVX2 void convert_argb8888_to_rgb565_avx2(const std::uint32_t* src,
                                     std::uint16_t* dst,
                                     std::size_t pixel_count) noexcept
{
    // Process 8 pixels at a time (256 bits = 8 x uint32_t)
    const auto vec_count = pixel_count / 8u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto pix = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i * 8));

        auto r = _mm256_and_si256(_mm256_srli_epi32(pix, 19), _mm256_set1_epi32(0x0000001F));
        auto g = _mm256_and_si256(_mm256_srli_epi32(pix, 10), _mm256_set1_epi32(0x0000003F));
        auto b = _mm256_and_si256(_mm256_srli_epi32(pix, 3), _mm256_set1_epi32(0x0000001F));

        auto rg = _mm256_or_si256(_mm256_slli_epi32(r, 11), _mm256_slli_epi32(g, 5));
        auto rgb565 = _mm256_or_si256(rg, b);

        alignas(32) std::uint32_t tmp[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), rgb565);
        for (std::size_t lane = 0; lane < 8u; ++lane) {
            dst[i * 8 + lane] = static_cast<std::uint16_t>(tmp[lane]);
        }
    }

    // Tail: fall back to scalar
    for (std::size_t i = vec_count * 8u; i < pixel_count; ++i) {
        dst[i] = argb8888_to_rgb565_single(src[i]);
    }
}

BMMQ_TARGET_AVX2 void replace_pixels_avx2(const std::uint32_t* src,
                         const std::uint8_t* mask,
                         std::uint32_t* dst,
                         std::uint32_t replacement_color,
                         std::size_t pixel_count) noexcept
{
    const auto vec_count = pixel_count / 8u;
    const auto repl_vec = _mm256_set1_epi32(replacement_color);
    for (std::size_t i = 0; i < vec_count; ++i) {
        auto pix = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i * 8));

        __m128i mask_vec = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(mask + i * 8));

        // Expand to 256-bit: shuffle each byte 4 times, interleave for both lanes
        __m128i shuffle_128 = _mm_setr_epi8(0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3);
        __m128i exp_lo = _mm_shuffle_epi8(mask_vec, shuffle_128);
        __m128i exp_hi = _mm_shuffle_epi8(_mm_srli_si128(mask_vec, 4), shuffle_128);
        __m256i expanded = _mm256_inserti128_si256(_mm256_castsi128_si256(exp_lo), exp_hi, 1);

        auto keep_src = _mm256_cmpeq_epi32(expanded, _mm256_setzero_si256());
        auto masked_src = _mm256_and_si256(keep_src, pix);
        auto masked_repl = _mm256_andnot_si256(keep_src, repl_vec);
        auto result = _mm256_or_si256(masked_src, masked_repl);

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i * 8), result);
    }

    for (std::size_t i = vec_count * 8u; i < pixel_count; ++i) {
        dst[i] = (mask[i] != 0u) ? replacement_color : src[i];
    }
}

BMMQ_TARGET_AVX2 void replace_pixels_mask_avx2(const std::uint32_t* src,
                              const std::uint8_t* mask,
                              const std::uint32_t* replacements,
                              std::uint32_t* dst,
                              std::size_t pixel_count) noexcept
{
    const auto vec_count = pixel_count / 8u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto pix = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i * 8));
        auto repl = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(replacements + i * 8));
        __m128i mask_vec = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(mask + i * 8));

        __m128i shuffle_128 = _mm_setr_epi8(0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3);
        __m128i exp_lo = _mm_shuffle_epi8(mask_vec, shuffle_128);
        __m128i exp_hi = _mm_shuffle_epi8(_mm_srli_si128(mask_vec, 4), shuffle_128);
        __m256i expanded = _mm256_inserti128_si256(_mm256_castsi128_si256(exp_lo), exp_hi, 1);

        auto keep_src = _mm256_cmpeq_epi32(expanded, _mm256_setzero_si256());
        auto masked_src = _mm256_and_si256(keep_src, pix);
        auto masked_repl = _mm256_andnot_si256(keep_src, repl);
        auto result = _mm256_or_si256(masked_src, masked_repl);

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i * 8), result);
    }

    for (std::size_t i = vec_count * 8u; i < pixel_count; ++i) {
        dst[i] = (mask[i] != 0u) ? replacements[i] : src[i];
    }
}

BMMQ_TARGET_AVX2 void fill_pixels_avx2(std::uint32_t* dst,
                      std::uint32_t color,
                      std::size_t pixel_count) noexcept
{
    const auto vec = _mm256_set1_epi32(color);
    const auto vec_count = pixel_count / 8u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i * 8), vec);
    }

    for (std::size_t i = vec_count * 8u; i < pixel_count; ++i) {
        dst[i] = color;
    }
}
#endif // BMMQ_SIMD_BUILD_AVX2

#if BMMQ_SIMD_BUILD_X86
BMMQ_TARGET_SSE2 void count_different_pixels_sse2(const std::uint32_t* a,
                                 const std::uint32_t* b,
                                 std::size_t pixel_count,
                                 std::size_t& diff) noexcept
{
    const auto vec_count = pixel_count / 4u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i * 4));
        auto vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i * 4));

        auto eq = _mm_cmpeq_epi32(va, vb);
        const auto equalMask = static_cast<unsigned>(_mm_movemask_ps(_mm_castsi128_ps(eq))) & 0x0Fu;
        diff += 4u - static_cast<std::size_t>((equalMask & 1u) + ((equalMask >> 1u) & 1u) +
                                              ((equalMask >> 2u) & 1u) + ((equalMask >> 3u) & 1u));
    }

    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        if (a[i] != b[i]) {
            ++diff;
        }
    }
}
#if BMMQ_SIMD_BUILD_AVX2
BMMQ_TARGET_AVX2 void count_different_pixels_avx2(const std::uint32_t* a,
                                 const std::uint32_t* b,
                                 std::size_t pixel_count,
                                 std::size_t& diff) noexcept
{
    const auto vec_count = pixel_count / 8u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i * 8));
        auto vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + i * 8));

        auto eq = _mm256_cmpeq_epi32(va, vb);
        auto equalMask = static_cast<unsigned>(_mm256_movemask_ps(_mm256_castsi256_ps(eq))) & 0xFFu;
        std::size_t equalCount = 0u;
        for (unsigned lane = 0u; lane < 8u; ++lane) {
            equalCount += (equalMask >> lane) & 1u;
        }
        diff += 8u - equalCount;
    }

    for (std::size_t i = vec_count * 8u; i < pixel_count; ++i) {
        if (a[i] != b[i]) {
            ++diff;
        }
    }
}
#endif
#endif // BMMQ_SIMD_BUILD_X86

void count_different_pixels_scalar(const std::uint32_t* a,
                                   const std::uint32_t* b,
                                   std::size_t pixel_count,
                                   std::size_t& diff) noexcept
{
    for (std::size_t i = 0; i < pixel_count; ++i) {
        if (a[i] != b[i]) {
            ++diff;
        }
    }
}

#if BMMQ_SIMD_BUILD_NEON
void convert_argb8888_to_rgb565_neon(const std::uint32_t* src,
                                     std::uint16_t* dst,
                                     std::size_t pixelCount) noexcept
{
    std::size_t i = 0u;
    const auto mask5 = vdupq_n_u32(0x1Fu);
    const auto mask6 = vdupq_n_u32(0x3Fu);
    for (; i + 4u <= pixelCount; i += 4u) {
        const auto pixels = vld1q_u32(src + i);
        const auto red = vandq_u32(vshrq_n_u32(pixels, 19), mask5);
        const auto green = vandq_u32(vshrq_n_u32(pixels, 10), mask6);
        const auto blue = vandq_u32(vshrq_n_u32(pixels, 3), mask5);
        const auto packed = vorrq_u32(vorrq_u32(vshlq_n_u32(red, 11), vshlq_n_u32(green, 5)), blue);
        vst1_u16(dst + i, vmovn_u32(packed));
    }
    convert_argb8888_to_rgb565_scalar(src + i, dst + i, pixelCount - i);
}

void replace_pixels_mask_neon(const std::uint32_t* src,
                              const std::uint8_t* mask,
                              const std::uint32_t* replacements,
                              std::uint32_t* dst,
                              std::size_t pixelCount) noexcept
{
    std::size_t i = 0u;
    for (; i + 4u <= pixelCount; i += 4u) {
        const std::uint32_t maskLanes[4] = {
            mask[i] != 0u ? ~0u : 0u, mask[i + 1u] != 0u ? ~0u : 0u,
            mask[i + 2u] != 0u ? ~0u : 0u, mask[i + 3u] != 0u ? ~0u : 0u};
        const auto select = vld1q_u32(maskLanes);
        vst1q_u32(dst + i, vbslq_u32(select, vld1q_u32(replacements + i), vld1q_u32(src + i)));
    }
    replace_pixels_mask_scalar(src + i, mask + i, replacements + i, dst + i, pixelCount - i);
}

void replace_pixels_neon(const std::uint32_t* src,
                         const std::uint8_t* mask,
                         std::uint32_t* dst,
                         std::uint32_t replacementColor,
                         std::size_t pixelCount) noexcept
{
    std::size_t i = 0u;
    const auto replacement = vdupq_n_u32(replacementColor);
    for (; i + 4u <= pixelCount; i += 4u) {
        const std::uint32_t maskLanes[4] = {
            mask[i] != 0u ? ~0u : 0u, mask[i + 1u] != 0u ? ~0u : 0u,
            mask[i + 2u] != 0u ? ~0u : 0u, mask[i + 3u] != 0u ? ~0u : 0u};
        vst1q_u32(dst + i, vbslq_u32(vld1q_u32(maskLanes), replacement, vld1q_u32(src + i)));
    }
    replace_pixels_scalar(src + i, mask + i, dst + i, replacementColor, pixelCount - i);
}

void fill_pixels_neon(std::uint32_t* dst, std::uint32_t color, std::size_t pixelCount) noexcept
{
    std::size_t i = 0u;
    const auto pixels = vdupq_n_u32(color);
    for (; i + 4u <= pixelCount; i += 4u) {
        vst1q_u32(dst + i, pixels);
    }
    fill_pixels_scalar(dst + i, color, pixelCount - i);
}
#endif

[[nodiscard]] Backend detectBackend() noexcept
{
#if defined(BMMQ_SIMD_FORCE_SCALAR) && BMMQ_SIMD_FORCE_SCALAR
    return Backend::Scalar;
#elif BMMQ_SIMD_BUILD_NEON
    return Backend::Neon;
#elif BMMQ_SIMD_BUILD_X86 && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    if (BMMQ_SIMD_BUILD_AVX2 && __builtin_cpu_supports("avx2")) {
        return Backend::Avx2;
    }
    if (__builtin_cpu_supports("sse4.1") && __builtin_cpu_supports("ssse3")) {
        return Backend::Sse41;
    }
    if (__builtin_cpu_supports("sse2")) {
        return Backend::Sse2;
    }
    return Backend::Scalar;
#elif BMMQ_SIMD_BUILD_X86 && defined(_MSC_VER)
    int registers[4]{};
    __cpuid(registers, 1);
    const bool sse2 = (registers[3] & (1 << 26)) != 0;
    const bool ssse3 = (registers[2] & (1 << 9)) != 0;
    const bool sse41 = (registers[2] & (1 << 19)) != 0;
    const bool avx = (registers[2] & (1 << 28)) != 0;
    const bool osxsave = (registers[2] & (1 << 27)) != 0;
    bool avx2 = false;
#if BMMQ_SIMD_BUILD_AVX2
    if (avx && osxsave && (_xgetbv(0) & 0x6u) == 0x6u) {
        __cpuidex(registers, 7, 0);
        avx2 = (registers[1] & (1 << 5)) != 0;
    }
#endif
    return avx2 ? Backend::Avx2 : (sse41 && ssse3 ? Backend::Sse41 :
           (sse2 ? Backend::Sse2 : Backend::Scalar));
#else
    return Backend::Scalar;
#endif
}

[[nodiscard]] Backend selectedBackend() noexcept
{
    static const Backend backend = detectBackend();
    return backend;
}

} // namespace (anonymous)

Backend active_backend() noexcept
{
    return selectedBackend();
}

const char* active_backend_name() noexcept
{
    switch (selectedBackend()) {
    case Backend::Sse2: return "sse2";
    case Backend::Sse41: return "sse4.1";
    case Backend::Avx2: return "avx2";
    case Backend::Neon: return "neon";
    case Backend::Scalar:
    default: return "scalar";
    }
}

std::size_t argb8888_to_rgb565_vector_width() noexcept
{
    return selectedBackend() == Backend::Avx2 ? 8u :
        (selectedBackend() == Backend::Scalar ? 1u : 4u);
}

void convert_argb8888_to_rgb565(const std::uint32_t* src,
                                std::uint16_t* dst,
                                std::size_t pixelCount) noexcept
{
    if (pixelCount == 0u) return;
    switch (selectedBackend()) {
#if BMMQ_SIMD_BUILD_AVX2
    case Backend::Avx2: convert_argb8888_to_rgb565_avx2(src, dst, pixelCount); return;
#endif
#if BMMQ_SIMD_BUILD_X86
    case Backend::Sse41: convert_argb8888_to_rgb565_sse41(src, dst, pixelCount); return;
    case Backend::Sse2: convert_argb8888_to_rgb565_sse2(src, dst, pixelCount); return;
#endif
#if BMMQ_SIMD_BUILD_NEON
    case Backend::Neon: convert_argb8888_to_rgb565_neon(src, dst, pixelCount); return;
#endif
    default: convert_argb8888_to_rgb565_scalar(src, dst, pixelCount); return;
    }
}

bool convert_indexed_to_rgb565(const RealtimeVideoSurface& surface,
                               int width,
                               int height,
                               std::uint16_t* dst,
                               std::size_t destinationStridePixels) noexcept
{
    if (dst == nullptr || !surface.validForDimensions(width, height) ||
        destinationStridePixels < static_cast<std::size_t>(width) ||
        surface.encoding == RealtimeVideoEncoding::Argb8888) {
        return false;
    }
    std::array<std::uint16_t, 256> palette{};
    convert_argb8888_to_rgb565(surface.paletteArgb.data(), palette.data(), surface.paletteArgb.size());
    const auto bits = static_cast<std::uint8_t>(surface.encoding);
    const auto mask = static_cast<std::uint16_t>((std::uint16_t{1u} << bits) - 1u);
    for (int y = 0; y < height; ++y) {
        const auto sourceRow = static_cast<std::size_t>(y) * surface.strideBytes;
        auto* destinationRow = dst + static_cast<std::size_t>(y) * destinationStridePixels;
        std::size_t bitOffset = 0u;
        for (int x = 0; x < width; ++x) {
            const auto byteOffset = sourceRow + bitOffset / 8u;
            const auto shift = static_cast<unsigned>(bitOffset % 8u);
            std::uint16_t encoded = surface.indexedBytes[byteOffset];
            if (shift + bits > 8u) {
                encoded |= static_cast<std::uint16_t>(surface.indexedBytes[byteOffset + 1u]) << 8u;
            }
            const auto index = static_cast<std::size_t>((encoded >> shift) & mask);
            if (index >= surface.paletteArgb.size()) return false;
            destinationRow[static_cast<std::size_t>(x)] = palette[index];
            bitOffset += bits;
        }
    }
    return true;
}

void replace_pixels_with_color(const std::uint32_t* src,
                               const std::uint8_t* mask,
                               std::uint32_t* dst,
                               std::uint32_t replacementColor,
                               std::size_t pixelCount) noexcept
{
    if (pixelCount == 0u) return;
    switch (selectedBackend()) {
#if BMMQ_SIMD_BUILD_AVX2
    case Backend::Avx2: replace_pixels_avx2(src, mask, dst, replacementColor, pixelCount); return;
#endif
#if BMMQ_SIMD_BUILD_X86
    case Backend::Sse41: replace_pixels_ssse3(src, mask, dst, replacementColor, pixelCount); return;
#endif
#if BMMQ_SIMD_BUILD_NEON
    case Backend::Neon: replace_pixels_neon(src, mask, dst, replacementColor, pixelCount); return;
#endif
    default: replace_pixels_scalar(src, mask, dst, replacementColor, pixelCount); return;
    }
}

void replace_pixels_with_mask(const std::uint32_t* src,
                              const std::uint8_t* mask,
                              const std::uint32_t* replacements,
                              std::uint32_t* dst,
                              std::size_t pixelCount) noexcept
{
    if (pixelCount == 0u) return;
    switch (selectedBackend()) {
#if BMMQ_SIMD_BUILD_AVX2
    case Backend::Avx2: replace_pixels_mask_avx2(src, mask, replacements, dst, pixelCount); return;
#endif
#if BMMQ_SIMD_BUILD_X86
    case Backend::Sse41: replace_pixels_mask_ssse3(src, mask, replacements, dst, pixelCount); return;
#endif
#if BMMQ_SIMD_BUILD_NEON
    case Backend::Neon: replace_pixels_mask_neon(src, mask, replacements, dst, pixelCount); return;
#endif
    default: replace_pixels_mask_scalar(src, mask, replacements, dst, pixelCount); return;
    }
}

void fill_pixels(std::uint32_t* dst, std::uint32_t color, std::size_t pixelCount) noexcept
{
    if (pixelCount == 0u) return;
    switch (selectedBackend()) {
#if BMMQ_SIMD_BUILD_AVX2
    case Backend::Avx2: fill_pixels_avx2(dst, color, pixelCount); return;
#endif
#if BMMQ_SIMD_BUILD_X86
    case Backend::Sse41:
    case Backend::Sse2: fill_pixels_sse2(dst, color, pixelCount); return;
#endif
#if BMMQ_SIMD_BUILD_NEON
    case Backend::Neon: fill_pixels_neon(dst, color, pixelCount); return;
#endif
    default: fill_pixels_scalar(dst, color, pixelCount); return;
    }
}

std::size_t count_different_pixels(const std::uint32_t* a,
                                   const std::uint32_t* b,
                                   std::size_t pixelCount) noexcept
{
    std::size_t diff = 0;
    if (pixelCount == 0u) return diff;
    switch (selectedBackend()) {
#if BMMQ_SIMD_BUILD_AVX2
    case Backend::Avx2: count_different_pixels_avx2(a, b, pixelCount, diff); break;
#endif
#if BMMQ_SIMD_BUILD_X86
    case Backend::Sse41:
    case Backend::Sse2: count_different_pixels_sse2(a, b, pixelCount, diff); break;
#endif
    default: count_different_pixels_scalar(a, b, pixelCount, diff); break;
    }
    return diff;
}

} // namespace SimdPixelOps
} // namespace BMMQ
