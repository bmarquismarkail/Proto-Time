#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "machine/plugins/video/SimdPixelOps.hpp"

using namespace BMMQ::SimdPixelOps;

// Simple seeded PRNG for deterministic test data
static std::mt19937 rng(42);

[[nodiscard]] std::uint32_t random_argb() noexcept
{
    return static_cast<std::uint32_t>(rng());
}

// ---------------------------------------------------------------------------
// Test: ARGB8888 -> RGB565 conversion
// ---------------------------------------------------------------------------
static bool test_argb8888_to_rgb565()
{
    constexpr std::size_t kPixelCount = 1000;
    std::vector<std::uint32_t> src(kPixelCount);
    std::vector<std::uint16_t> dst_simd(kPixelCount);
    std::vector<std::uint16_t> dst_ref(kPixelCount);

    for (std::size_t i = 0; i < kPixelCount; ++i) {
        src[i] = random_argb();
    }

    // SIMD conversion
    convert_argb8888_to_rgb565(src.data(), dst_simd.data(), kPixelCount);

    // Reference scalar conversion
    for (std::size_t i = 0; i < kPixelCount; ++i) {
        const auto r = (src[i] >> 16u) & 0xFFu;
        const auto g = (src[i] >> 8u) & 0xFFu;
        const auto b = src[i] & 0xFFu;
        dst_ref[i] = static_cast<std::uint16_t>(
            ((r >> 3u) << 11u) |
            ((g >> 2u) << 5u) |
            (b >> 3u));
    }

    if (dst_simd != dst_ref) {
        std::cerr << "FAIL: argb8888_to_rgb565 mismatch at pixel ";
        for (std::size_t i = 0; i < kPixelCount; ++i) {
            if (dst_simd[i] != dst_ref[i]) {
                std::cerr << i << " (SIMD=0x" << std::hex << dst_simd[i]
                          << " ref=0x" << dst_ref[i] << ")" << std::dec;
                break;
            }
        }
        std::cerr << std::endl;
        return false;
    }

    // Edge cases: empty, single pixel, small counts
    convert_argb8888_to_rgb565(nullptr, nullptr, 0);
    std::uint32_t single_src = 0xFF123456u;
    std::uint16_t single_dst;
    convert_argb8888_to_rgb565(&single_src, &single_dst, 1);
    const auto expected_single = static_cast<std::uint16_t>(
        (((0x12u >> 3u)) << 11u) |
        (((0x34u >> 2u)) << 5u) |
        (0x56u >> 3u));
    if (single_dst != expected_single) {
        std::cerr << "FAIL: single pixel conversion failed" << std::endl;
        return false;
    }

    // Test various sizes to exercise all tail paths
    for (int sz = 1; sz <= 20; ++sz) {
        std::vector<std::uint32_t> s(sz);
        std::vector<std::uint16_t> d_simd(sz);
        std::vector<std::uint16_t> d_ref(sz);
        for (int j = 0; j < sz; ++j) s[j] = random_argb();
        convert_argb8888_to_rgb565(s.data(), d_simd.data(), static_cast<std::size_t>(sz));
        for (int j = 0; j < sz; ++j) {
            const auto r = (s[j] >> 16u) & 0xFFu;
            const auto g = (s[j] >> 8u) & 0xFFu;
            const auto b = s[j] & 0xFFu;
            d_ref[j] = static_cast<std::uint16_t>(
                ((r >> 3u) << 11u) | ((g >> 2u) << 5u) | (b >> 3u));
        }
        if (d_simd != d_ref) {
            std::cerr << "FAIL: size=" << sz << " mismatch" << std::endl;
            return false;
        }
    }

    // Known values
    struct { std::uint32_t in; std::uint16_t out; } known[] = {
        {0xFFFFFFFFu, 0xFFFFu},   // white
        {0xFF000000u, 0x0000u},   // black
        {0xFFFF0000u, 0xF800u},   // red
        {0xFF00FF00u, 0x07E0u},   // green
        {0xFF0000FFu, 0x001Fu},   // blue
        {0xFF808080u, 0x8410u},   // gray
    };
    for (const auto& k : known) {
        std::uint16_t result;
        convert_argb8888_to_rgb565(&k.in, &result, 1);
        if (result != k.out) {
            std::cerr << "FAIL: known value 0x" << std::hex << k.in
                      << " expected 0x" << k.out << " got 0x" << result << std::dec << std::endl;
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test: replace_pixels_with_color
// ---------------------------------------------------------------------------
static bool test_replace_pixels_with_color()
{
    constexpr std::size_t kPixelCount = 1000;
    std::vector<std::uint32_t> src(kPixelCount);
    std::vector<std::uint8_t> mask(kPixelCount);
    std::vector<std::uint32_t> dst_simd(kPixelCount);
    std::vector<std::uint32_t> dst_ref(kPixelCount);

    for (std::size_t i = 0; i < kPixelCount; ++i) {
        src[i] = random_argb();
        mask[i] = (rng() % 3 == 0) ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
    }

    const std::uint32_t replacement = 0xFF00FF00u;

    replace_pixels_with_color(src.data(), mask.data(), dst_simd.data(), replacement, kPixelCount);

    for (std::size_t i = 0; i < kPixelCount; ++i) {
        dst_ref[i] = (mask[i] != 0u) ? replacement : src[i];
    }

    if (dst_simd != dst_ref) {
        std::cerr << "FAIL: replace_pixels_with_color mismatch" << std::endl;
        return false;
    }

    // Edge cases
    replace_pixels_with_color(nullptr, nullptr, nullptr, 0, 0);

    // All masked
    std::fill(mask.begin(), mask.end(), static_cast<std::uint8_t>(1));
    replace_pixels_with_color(src.data(), mask.data(), dst_simd.data(), replacement, kPixelCount);
    std::fill(dst_ref.begin(), dst_ref.end(), replacement);
    if (dst_simd != dst_ref) {
        std::cerr << "FAIL: all masked" << std::endl;
        return false;
    }

    // None masked
    std::fill(mask.begin(), mask.end(), static_cast<std::uint8_t>(0));
    replace_pixels_with_color(src.data(), mask.data(), dst_simd.data(), replacement, kPixelCount);
    dst_ref = src;
    if (dst_simd != dst_ref) {
        std::cerr << "FAIL: none masked" << std::endl;
        return false;
    }

    // Various sizes
    for (int sz = 1; sz <= 20; ++sz) {
        std::vector<std::uint32_t> s(sz), d_sz(sz);
        std::vector<std::uint8_t> m(sz);
        for (int j = 0; j < sz; ++j) {
            s[j] = random_argb();
            m[j] = static_cast<std::uint8_t>(rng() % 2);
        }
        replace_pixels_with_color(s.data(), m.data(), d_sz.data(), replacement, static_cast<std::size_t>(sz));
        for (int j = 0; j < sz; ++j) {
            if (m[j] != 0 && d_sz[j] != replacement) {
                std::cerr << "FAIL: size=" << sz << " masked pixel not replaced" << std::endl;
                return false;
            }
            if (m[j] == 0 && d_sz[j] != s[j]) {
                std::cerr << "FAIL: size=" << sz << " unmasked pixel changed" << std::endl;
                return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test: replace_pixels_with_mask
// ---------------------------------------------------------------------------
static bool test_replace_pixels_with_mask()
{
    constexpr std::size_t kPixelCount = 1000;
    std::vector<std::uint32_t> src(kPixelCount);
    std::vector<std::uint32_t> replacements(kPixelCount);
    std::vector<std::uint8_t> mask(kPixelCount);
    std::vector<std::uint32_t> dst_simd(kPixelCount);
    std::vector<std::uint32_t> dst_ref(kPixelCount);

    for (std::size_t i = 0; i < kPixelCount; ++i) {
        src[i] = random_argb();
        replacements[i] = random_argb();
        mask[i] = (rng() % 4 == 0) ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
        dst_ref[i] = (mask[i] != 0u) ? replacements[i] : src[i];
    }

    replace_pixels_with_mask(src.data(), mask.data(), replacements.data(), dst_simd.data(), kPixelCount);
    if (dst_simd != dst_ref) {
        std::cerr << "FAIL: replace_pixels_with_mask mismatch" << std::endl;
        return false;
    }

    replace_pixels_with_mask(nullptr, nullptr, nullptr, nullptr, 0);

    for (int sz = 1; sz <= 20; ++sz) {
        std::vector<std::uint32_t> s(sz), r(sz), d(sz), expected(sz);
        std::vector<std::uint8_t> m(sz);
        for (int j = 0; j < sz; ++j) {
            s[j] = random_argb();
            r[j] = random_argb();
            m[j] = static_cast<std::uint8_t>(rng() % 2);
            expected[j] = (m[j] != 0u) ? r[j] : s[j];
        }
        replace_pixels_with_mask(s.data(), m.data(), r.data(), d.data(), static_cast<std::size_t>(sz));
        if (d != expected) {
            std::cerr << "FAIL: replace_pixels_with_mask size=" << sz << " mismatch" << std::endl;
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test: composite_scanline_alpha
// ---------------------------------------------------------------------------
static bool test_composite_scanline_alpha()
{
    constexpr std::size_t kPixelCount = 1000;
    std::vector<std::uint32_t> base(kPixelCount);
    std::vector<std::uint32_t> overlay(kPixelCount);
    std::vector<std::uint32_t> dst_simd(kPixelCount);
    std::vector<std::uint32_t> dst_ref(kPixelCount);

    for (std::size_t i = 0; i < kPixelCount; ++i) {
        base[i] = random_argb();
        overlay[i] = random_argb();
    }

    composite_scanline_alpha(base.data(), overlay.data(), dst_simd.data(), kPixelCount);

    for (std::size_t i = 0; i < kPixelCount; ++i) {
        const auto o = overlay[i];
        const auto oa = (o >> 24u) & 0xFFu;
        if (oa == 0u) {
            dst_ref[i] = base[i];
        } else {
            const auto b = base[i];
            const auto or_c = static_cast<std::uint32_t>((o >> 16u) & 0xFFu);
            const auto og_c = static_cast<std::uint32_t>((o >> 8u) & 0xFFu);
            const auto ob_c = static_cast<std::uint32_t>(o & 0xFFu);
            const auto br_c = static_cast<std::uint32_t>((b >> 16u) & 0xFFu);
            const auto bg_c = static_cast<std::uint32_t>((b >> 8u) & 0xFFu);
            const auto bb_c = static_cast<std::uint32_t>(b & 0xFFu);
            const auto inv_oa = 255u - oa;

            const auto ra = (or_c * oa + br_c * inv_oa + 128u) / 255u;
            const auto ga = (og_c * oa + bg_c * inv_oa + 128u) / 255u;
            const auto ba = (ob_c * oa + bb_c * inv_oa + 128u) / 255u;

            dst_ref[i] = (oa << 24u) | (ra << 16u) | (ga << 8u) | ba;
        }
    }

    // Check for exact or near-exact match (SIMD approximation may differ by 1 in blending)
    int max_channel_diff = 0;
    for (std::size_t i = 0; i < kPixelCount; ++i) {
        const auto sim_alpha = (dst_simd[i] >> 24u) & 0xFFu;
        const auto ref_alpha = (dst_ref[i] >> 24u) & 0xFFu;
        if (sim_alpha != ref_alpha) {
            std::cerr << "FAIL: alpha mismatch at " << i << std::endl;
            return false;
        }
        for (int shift : {16, 8, 0}) {
            const auto sim_channel = static_cast<int>((dst_simd[i] >> static_cast<unsigned>(shift)) & 0xFFu);
            const auto ref_channel = static_cast<int>((dst_ref[i] >> static_cast<unsigned>(shift)) & 0xFFu);
            max_channel_diff = std::max(max_channel_diff, std::abs(sim_channel - ref_channel));
        }
    }

    // Allow per-channel difference of up to 1 due to SIMD approximation
    if (max_channel_diff > 1) {
        std::cerr << "FAIL: composite_scanline_alpha max channel diff too large: " << max_channel_diff << std::endl;
        return false;
    }

    // Edge cases
    composite_scanline_alpha(nullptr, nullptr, nullptr, 0);

    // Fully transparent overlay -> should equal base
    std::fill(overlay.begin(), overlay.end(), 0x00000000u);
    composite_scanline_alpha(base.data(), overlay.data(), dst_simd.data(), kPixelCount);
    if (dst_simd != base) {
        std::cerr << "FAIL: fully transparent overlay should equal base" << std::endl;
        return false;
    }

    // Fully opaque overlay -> should equal overlay
    for (std::size_t i = 0; i < kPixelCount; ++i) {
        overlay[i] = 0xFF000000u | (overlay[i] & 0x00FFFFFFu);
    }
    composite_scanline_alpha(base.data(), overlay.data(), dst_simd.data(), kPixelCount);
    for (std::size_t i = 0; i < kPixelCount; ++i) {
        const auto sim_rgb = dst_simd[i] & 0x00FFFFFFu;
        const auto ref_rgb = overlay[i] & 0x00FFFFFFu;
        if (sim_rgb != ref_rgb) {
            std::cerr << "FAIL: fully opaque mismatch at " << i << std::endl;
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test: fill_pixels
// ---------------------------------------------------------------------------
static bool test_fill_pixels()
{
    constexpr std::size_t kPixelCount = 1000;
    const std::uint32_t color = 0xFF123456u;

    std::vector<std::uint32_t> pixels(kPixelCount);
    fill_pixels(pixels.data(), color, kPixelCount);

    for (std::size_t i = 0; i < kPixelCount; ++i) {
        if (pixels[i] != color) {
            std::cerr << "FAIL: fill_pixels mismatch at " << i << std::endl;
            return false;
        }
    }

    // Edge cases
    fill_pixels(nullptr, 0, 0);

    for (int sz = 1; sz <= 20; ++sz) {
        std::vector<std::uint32_t> p(sz);
        fill_pixels(p.data(), color, static_cast<std::size_t>(sz));
        for (int j = 0; j < sz; ++j) {
            if (p[j] != color) {
                std::cerr << "FAIL: fill size=" << sz << " mismatch" << std::endl;
                return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test: count_different_pixels
// ---------------------------------------------------------------------------
static bool test_count_different_pixels()
{
    constexpr std::size_t kPixelCount = 1000;
    std::vector<std::uint32_t> a(kPixelCount);
    std::vector<std::size_t> diffs_to_test = {0, 1, 50, 100, 500, 1000};

    for (std::size_t target_diff : diffs_to_test) {
        for (std::size_t i = 0; i < kPixelCount; ++i) {
            a[i] = random_argb();
        }
        std::vector<std::uint32_t> b = a;
        // Introduce exactly target_diff differences
        for (std::size_t d = 0; d < target_diff && d < kPixelCount; ++d) {
            const auto idx = rng() % kPixelCount;
            b[idx] = a[idx] ^ 0xFFu; // flip lower byte to ensure difference
        }

        const auto simd_count = count_different_pixels(a.data(), b.data(), kPixelCount);
        std::size_t ref_count = 0;
        for (std::size_t i = 0; i < kPixelCount; ++i) {
            if (a[i] != b[i]) ++ref_count;
        }

        if (simd_count != ref_count) {
            std::cerr << "FAIL: count_different_pixels mismatch (expected " << ref_count
                      << " got " << simd_count << ")" << std::endl;
            return false;
        }
    }

    // Edge cases
    assert(count_different_pixels(nullptr, nullptr, 0) == 0);

    // Identical arrays
    for (std::size_t i = 0; i < kPixelCount; ++i) a[i] = random_argb();
    if (count_different_pixels(a.data(), a.data(), kPixelCount) != 0) {
        std::cerr << "FAIL: identical arrays should have 0 differences" << std::endl;
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test: vector width reporting
// ---------------------------------------------------------------------------
static bool test_vector_width()
{
    const auto vw = argb8888_to_rgb565_vector_width();
    if (vw < 1u) {
        std::cerr << "FAIL: vector_width should be >= 1" << std::endl;
        return false;
    }
    std::cout << "SIMD vector width for ARGB->RGB565: " << vw << " pixels" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    int failures = 0;

    struct TestCase {
        const char* name;
        bool (*fn)();
    };

    TestCase tests[] = {
        {"vector_width", test_vector_width},
        {"argb8888_to_rgb565", test_argb8888_to_rgb565},
        {"replace_pixels_with_color", test_replace_pixels_with_color},
        {"replace_pixels_with_mask", test_replace_pixels_with_mask},
        {"composite_scanline_alpha", test_composite_scanline_alpha},
        {"fill_pixels", test_fill_pixels},
        {"count_different_pixels", test_count_different_pixels},
    };

    for (const auto& t : tests) {
        std::cout << "Testing " << t.name << "... ";
        if (t.fn()) {
            std::cout << "PASS" << std::endl;
        } else {
            std::cout << "FAIL" << std::endl;
            ++failures;
        }
    }

    std::cout << "\nResults: " << (sizeof(tests)/sizeof(tests[0]) - failures)
              << "/" << sizeof(tests)/sizeof(tests[0]) << " tests passed" << std::endl;

    return failures == 0 ? 0 : 1;
}
