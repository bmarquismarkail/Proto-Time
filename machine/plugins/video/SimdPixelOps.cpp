#include "SimdPixelOps.hpp"

#include <algorithm>

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

void composite_scanline_scalar(const std::uint32_t* base,
                               const std::uint32_t* overlay,
                               std::uint32_t* dst,
                               std::size_t pixel_count) noexcept
{
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const auto o = overlay[i];
        const auto oa = (o >> 24u) & 0xFFu;
        if (oa == 0u) {
            dst[i] = base[i];
        } else {
            const auto b = base[i];
            // Per-channel alpha blend: result = overlay * alpha / 255 + base * (255 - alpha) / 255
            const auto or_c = (o >> 16u) & 0xFFu;
            const auto og_c = (o >> 8u) & 0xFFu;
            const auto ob_c = o & 0xFFu;
            const auto br_c = (b >> 16u) & 0xFFu;
            const auto bg_c = (b >> 8u) & 0xFFu;
            const auto bb_c = b & 0xFFu;

            const auto ra = static_cast<std::uint32_t>(
                (static_cast<std::uint32_t>(or_c) * oa +
                 static_cast<std::uint32_t>(br_c) * (255u - oa) + 128u) / 255u);
            const auto ga = static_cast<std::uint32_t>(
                (static_cast<std::uint32_t>(og_c) * oa +
                 static_cast<std::uint32_t>(bg_c) * (255u - oa) + 128u) / 255u);
            const auto ba = static_cast<std::uint32_t>(
                (static_cast<std::uint32_t>(ob_c) * oa +
                 static_cast<std::uint32_t>(bb_c) * (255u - oa) + 128u) / 255u);

            dst[i] = (oa << 24u) | (ra << 16u) | (ga << 8u) | ba;
        }
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

#if BMMQ_SIMD_SSE2 || BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
#include <immintrin.h>

void convert_argb8888_to_rgb565_sse2(const std::uint32_t* src,
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

#if BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
        auto packed = _mm_packus_epi32(rgb565, _mm_setzero_si128());
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + i * 4), packed);
#else
        alignas(16) std::uint32_t tmp[4];
        _mm_store_si128(reinterpret_cast<__m128i*>(tmp), rgb565);
        dst[i * 4 + 0] = static_cast<std::uint16_t>(tmp[0]);
        dst[i * 4 + 1] = static_cast<std::uint16_t>(tmp[1]);
        dst[i * 4 + 2] = static_cast<std::uint16_t>(tmp[2]);
        dst[i * 4 + 3] = static_cast<std::uint16_t>(tmp[3]);
#endif
    }

    // Tail
    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        dst[i] = argb8888_to_rgb565_single(src[i]);
    }
}

#if BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
void replace_pixels_ssse3(const std::uint32_t* src,
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

void replace_pixels_mask_ssse3(const std::uint32_t* src,
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

// Helper to load 4 bytes as __m128i (used in replace_pixels_ssse3 above)
[[nodiscard]] inline __m128i _mm_loadu_si32(const std::uint8_t* p) noexcept
{
    return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
}
#endif // SSSE3+

#if BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
void composite_scanline_sse41(const std::uint32_t* base,
                              const std::uint32_t* overlay,
                              std::uint32_t* dst,
                              std::size_t pixel_count) noexcept
{
    // Process 4 pixels at a time
    const auto vec_count = pixel_count / 4u;
    const auto alpha_mask = _mm_set1_epi32(0xFF000000);
    for (std::size_t i = 0; i < vec_count; ++i) {
        auto b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(base + i * 4));
        auto o = _mm_loadu_si128(reinterpret_cast<const __m128i*>(overlay + i * 4));

        // Extract overlay alpha (bits 24-31) -> replicate to all channels
        auto oa = _mm_srli_epi32(o, 24);                       // 0x000000AA
        // Blend: result_channel = (overlay_channel * alpha + base_channel * (255 - alpha) + 128) / 255
        // Using SSE4.1 pblendv to select between base (alpha==0) and blended (alpha>0)

        // Extract color channels from overlay
        auto or_vec = _mm_and_si128(_mm_srli_epi32(o, 16), _mm_set1_epi32(0x000000FF));
        auto og_vec = _mm_and_si128(_mm_srli_epi32(o, 8), _mm_set1_epi32(0x000000FF));
        auto ob_vec = _mm_and_si128(o, _mm_set1_epi32(0x000000FF));

        // Extract color channels from base
        auto br_vec = _mm_and_si128(_mm_srli_epi32(b, 16), _mm_set1_epi32(0x000000FF));
        auto bg_vec = _mm_and_si128(_mm_srli_epi32(b, 8), _mm_set1_epi32(0x000000FF));
        auto bb_vec = _mm_and_si128(b, _mm_set1_epi32(0x000000FF));

        // inv_alpha = 255 - alpha (for each channel, alpha is same in all)
        auto inv_oa = _mm_sub_epi32(_mm_set1_epi32(255), oa);

        // Multiply: overlay_c * alpha + base_c * inv_alpha + 128
        // Use pmulhuw on packed words for efficiency, but since we have dwords, use mul+shr
        auto r_num = _mm_add_epi32(
            _mm_add_epi32(
                _mm_mullo_epi32(or_vec, oa),
                _mm_mullo_epi32(br_vec, inv_oa)),
            _mm_set1_epi32(128));

        auto g_num = _mm_add_epi32(
            _mm_add_epi32(
                _mm_mullo_epi32(og_vec, oa),
                _mm_mullo_epi32(bg_vec, inv_oa)),
            _mm_set1_epi32(128));

        auto b_num = _mm_add_epi32(
            _mm_add_epi32(
                _mm_mullo_epi32(ob_vec, oa),
                _mm_mullo_epi32(bb_vec, inv_oa)),
            _mm_set1_epi32(128));

        auto r_blend = _mm_srli_epi32(_mm_add_epi32(r_num, _mm_srli_epi32(r_num, 8)), 8);
        auto g_blend = _mm_srli_epi32(_mm_add_epi32(g_num, _mm_srli_epi32(g_num, 8)), 8);
        auto b_blend = _mm_srli_epi32(_mm_add_epi32(b_num, _mm_srli_epi32(b_num, 8)), 8);

        r_blend = _mm_and_si128(r_blend, _mm_set1_epi32(0x000000FF));
        g_blend = _mm_and_si128(g_blend, _mm_set1_epi32(0x000000FF));
        b_blend = _mm_and_si128(b_blend, _mm_set1_epi32(0x000000FF));

        auto result = _mm_or_si128(
            _mm_and_si128(o, alpha_mask),
            _mm_or_si128(
                _mm_slli_epi32(r_blend, 16),
                _mm_or_si128(_mm_slli_epi32(g_blend, 8), b_blend)));

        // Where overlay alpha == 0, use base instead
        auto zero_mask = _mm_cmpeq_epi32(oa, _mm_setzero_si128());
        result = _mm_blendv_epi8(result, b, zero_mask);

        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4), result);
    }

    // Tail
    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        const auto o = overlay[i];
        const auto oa = (o >> 24u) & 0xFFu;
        if (oa == 0u) {
            dst[i] = base[i];
        } else {
            const auto b_px = base[i];
            const auto or_c = static_cast<std::uint32_t>((o >> 16u) & 0xFFu);
            const auto og_c = static_cast<std::uint32_t>((o >> 8u) & 0xFFu);
            const auto ob_c = static_cast<std::uint32_t>(o & 0xFFu);
            const auto br_c = static_cast<std::uint32_t>((b_px >> 16u) & 0xFFu);
            const auto bg_c = static_cast<std::uint32_t>((b_px >> 8u) & 0xFFu);
            const auto bb_c = static_cast<std::uint32_t>(b_px & 0xFFu);
            const auto inv_oa = 255u - oa;

            const auto ra = (or_c * oa + br_c * inv_oa + 128u) / 255u;
            const auto ga = (og_c * oa + bg_c * inv_oa + 128u) / 255u;
            const auto ba = (ob_c * oa + bb_c * inv_oa + 128u) / 255u;

            dst[i] = (oa << 24u) | (ra << 16u) | (ga << 8u) | ba;
        }
    }
}
#endif // SSE4.1+

void fill_pixels_sse2(std::uint32_t* dst,
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

#if BMMQ_SIMD_AVX2
void convert_argb8888_to_rgb565_avx2(const std::uint32_t* src,
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

void replace_pixels_avx2(const std::uint32_t* src,
                         const std::uint8_t* mask,
                         std::uint32_t* dst,
                         std::uint32_t replacement_color,
                         std::size_t pixel_count) noexcept
{
    const auto vec_count = pixel_count / 8u;
    const auto repl_vec = _mm256_set1_epi32(replacement_color);
    const auto shuffle_mask = _mm256_setr_epi8(
        0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
        4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7);

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

void replace_pixels_mask_avx2(const std::uint32_t* src,
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

void composite_scanline_avx2(const std::uint32_t* base,
                             const std::uint32_t* overlay,
                             std::uint32_t* dst,
                             std::size_t pixel_count) noexcept
{
    const auto vec_count = pixel_count / 8u;
    const auto alpha_mask_vec = _mm256_set1_epi32(0xFF000000);

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(base + i * 8));
        auto o = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(overlay + i * 8));

        auto oa = _mm256_srli_epi32(o, 24);
        auto or_vec = _mm256_and_si256(_mm256_srli_epi32(o, 16), _mm256_set1_epi32(0x000000FF));
        auto og_vec = _mm256_and_si256(_mm256_srli_epi32(o, 8), _mm256_set1_epi32(0x000000FF));
        auto ob_vec = _mm256_and_si256(o, _mm256_set1_epi32(0x000000FF));

        auto br_vec = _mm256_and_si256(_mm256_srli_epi32(b, 16), _mm256_set1_epi32(0x000000FF));
        auto bg_vec = _mm256_and_si256(_mm256_srli_epi32(b, 8), _mm256_set1_epi32(0x000000FF));
        auto bb_vec = _mm256_and_si256(b, _mm256_set1_epi32(0x000000FF));

        auto inv_oa = _mm256_sub_epi32(_mm256_set1_epi32(255), oa);

        auto r_num = _mm256_add_epi32(
            _mm256_add_epi32(
                _mm256_mullo_epi32(or_vec, oa),
                _mm256_mullo_epi32(br_vec, inv_oa)),
            _mm256_set1_epi32(128));
        auto g_num = _mm256_add_epi32(
            _mm256_add_epi32(
                _mm256_mullo_epi32(og_vec, oa),
                _mm256_mullo_epi32(bg_vec, inv_oa)),
            _mm256_set1_epi32(128));
        auto b_num = _mm256_add_epi32(
            _mm256_add_epi32(
                _mm256_mullo_epi32(ob_vec, oa),
                _mm256_mullo_epi32(bb_vec, inv_oa)),
            _mm256_set1_epi32(128));

        auto r_blend = _mm256_srli_epi32(_mm256_add_epi32(r_num, _mm256_srli_epi32(r_num, 8)), 8);
        auto g_blend = _mm256_srli_epi32(_mm256_add_epi32(g_num, _mm256_srli_epi32(g_num, 8)), 8);
        auto b_blend = _mm256_srli_epi32(_mm256_add_epi32(b_num, _mm256_srli_epi32(b_num, 8)), 8);

        r_blend = _mm256_and_si256(r_blend, _mm256_set1_epi32(0x000000FF));
        g_blend = _mm256_and_si256(g_blend, _mm256_set1_epi32(0x000000FF));
        b_blend = _mm256_and_si256(b_blend, _mm256_set1_epi32(0x000000FF));

        auto result = _mm256_or_si256(
            _mm256_and_si256(o, alpha_mask_vec),
            _mm256_or_si256(
                _mm256_slli_epi32(r_blend, 16),
                _mm256_or_si256(_mm256_slli_epi32(g_blend, 8), b_blend)));

        auto zero_mask = _mm256_cmpeq_epi32(oa, _mm256_setzero_si256());
        result = _mm256_blendv_epi8(result, b, zero_mask);

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i * 8), result);
    }

    for (std::size_t i = vec_count * 8u; i < pixel_count; ++i) {
        const auto o = overlay[i];
        const auto oa = (o >> 24u) & 0xFFu;
        if (oa == 0u) {
            dst[i] = base[i];
        } else {
            const auto b_px = base[i];
            const auto or_c = static_cast<std::uint32_t>((o >> 16u) & 0xFFu);
            const auto og_c = static_cast<std::uint32_t>((o >> 8u) & 0xFFu);
            const auto ob_c = static_cast<std::uint32_t>(o & 0xFFu);
            const auto br_c = static_cast<std::uint32_t>((b_px >> 16u) & 0xFFu);
            const auto bg_c = static_cast<std::uint32_t>((b_px >> 8u) & 0xFFu);
            const auto bb_c = static_cast<std::uint32_t>(b_px & 0xFFu);
            const auto inv_oa = 255u - oa;

            const auto ra = (or_c * oa + br_c * inv_oa + 128u) / 255u;
            const auto ga = (og_c * oa + bg_c * inv_oa + 128u) / 255u;
            const auto ba = (ob_c * oa + bb_c * inv_oa + 128u) / 255u;

            dst[i] = (oa << 24u) | (ra << 16u) | (ga << 8u) | ba;
        }
    }
}

void fill_pixels_avx2(std::uint32_t* dst,
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
#endif // AVX2

} // namespace (anonymous)

// ===========================================================================
// Public function implementations
// ===========================================================================

void convert_argb8888_to_rgb565(const std::uint32_t* src,
                                std::uint16_t* dst,
                                std::size_t pixel_count) noexcept
{
    if (pixel_count == 0u) {
        return;
    }

#if BMMQ_SIMD_AVX2
    convert_argb8888_to_rgb565_avx2(src, dst, pixel_count);
#elif BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE2
    convert_argb8888_to_rgb565_sse2(src, dst, pixel_count);
#else
    convert_argb8888_to_rgb565_scalar(src, dst, pixel_count);
#endif
}

void replace_pixels_with_color(const std::uint32_t* src,
                               const std::uint8_t* mask,
                               std::uint32_t* dst,
                               std::uint32_t replacement_color,
                               std::size_t pixel_count) noexcept
{
    if (pixel_count == 0u) {
        return;
    }

#if BMMQ_SIMD_AVX2
    replace_pixels_avx2(src, mask, dst, replacement_color, pixel_count);
#elif BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
    replace_pixels_ssse3(src, mask, dst, replacement_color, pixel_count);
#else
    replace_pixels_scalar(src, mask, dst, replacement_color, pixel_count);
#endif
}

void replace_pixels_with_mask(const std::uint32_t* src,
                              const std::uint8_t* mask,
                              const std::uint32_t* replacements,
                              std::uint32_t* dst,
                              std::size_t pixel_count) noexcept
{
    if (pixel_count == 0u) {
        return;
    }

#if BMMQ_SIMD_AVX2
    replace_pixels_mask_avx2(src, mask, replacements, dst, pixel_count);
#elif BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
    replace_pixels_mask_ssse3(src, mask, replacements, dst, pixel_count);
#else
    replace_pixels_mask_scalar(src, mask, replacements, dst, pixel_count);
#endif
}

void composite_scanline_alpha(const std::uint32_t* base,
                              const std::uint32_t* overlay,
                              std::uint32_t* dst,
                              std::size_t pixel_count) noexcept
{
    if (pixel_count == 0u) {
        return;
    }

#if BMMQ_SIMD_AVX2
    composite_scanline_avx2(base, overlay, dst, pixel_count);
#elif BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
    composite_scanline_sse41(base, overlay, dst, pixel_count);
#else
    composite_scanline_scalar(base, overlay, dst, pixel_count);
#endif
}

void fill_pixels(std::uint32_t* dst,
                 std::uint32_t color,
                 std::size_t pixel_count) noexcept
{
    if (pixel_count == 0u) {
        return;
    }

#if BMMQ_SIMD_AVX2
    fill_pixels_avx2(dst, color, pixel_count);
#elif BMMQ_SIMD_SSE2 || BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
    fill_pixels_sse2(dst, color, pixel_count);
#else
    fill_pixels_scalar(dst, color, pixel_count);
#endif
}

// ---------------------------------------------------------------------------
// Pixel comparison: count differing pixels between two ARGB8888 frames
// ---------------------------------------------------------------------------

#if BMMQ_SIMD_SSE2 || BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
void count_different_pixels_sse2(const std::uint32_t* a,
                                 const std::uint32_t* b,
                                 std::size_t pixel_count,
                                 std::size_t& diff) noexcept
{
    const auto vec_count = pixel_count / 4u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i * 4));
        auto vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i * 4));

        auto eq = _mm_cmpeq_epi32(va, vb);
        const auto equal_mask = static_cast<unsigned>(_mm_movemask_ps(_mm_castsi128_ps(eq)));
        diff += 4u - static_cast<std::size_t>(__builtin_popcount(equal_mask & 0x0Fu));
    }

    for (std::size_t i = vec_count * 4u; i < pixel_count; ++i) {
        if (a[i] != b[i]) {
            ++diff;
        }
    }
}
#endif // SSE2+

#if BMMQ_SIMD_AVX2
void count_different_pixels_avx2(const std::uint32_t* a,
                                 const std::uint32_t* b,
                                 std::size_t pixel_count,
                                 std::size_t& diff) noexcept
{
    const auto vec_count = pixel_count / 8u;

    for (std::size_t i = 0; i < vec_count; ++i) {
        auto va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i * 8));
        auto vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + i * 8));

        auto eq = _mm256_cmpeq_epi32(va, vb);
        const auto equal_mask = static_cast<unsigned>(_mm256_movemask_ps(_mm256_castsi256_ps(eq)));
        diff += 8u - static_cast<std::size_t>(__builtin_popcount(equal_mask & 0xFFu));
    }

    for (std::size_t i = vec_count * 8u; i < pixel_count; ++i) {
        if (a[i] != b[i]) {
            ++diff;
        }
    }
}
#endif // AVX2

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

std::size_t count_different_pixels(const std::uint32_t* a,
                                   const std::uint32_t* b,
                                   std::size_t pixel_count) noexcept
{
    std::size_t diff = 0;

#if BMMQ_SIMD_AVX2
    count_different_pixels_avx2(a, b, pixel_count, diff);
#elif BMMQ_SIMD_SSE2 || BMMQ_SIMD_SSSE3 || BMMQ_SIMD_SSE4_1 || BMMQ_SIMD_AVX2
    count_different_pixels_sse2(a, b, pixel_count, diff);
#else
    count_different_pixels_scalar(a, b, pixel_count, diff);
#endif

    return diff;
}

} // namespace SimdPixelOps
} // namespace BMMQ
