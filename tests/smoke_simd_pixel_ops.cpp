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
    std::cout << "SIMD backend: " << active_backend_name()
              << ", ARGB->RGB565 width: " << vw << " pixels" << std::endl;
#if defined(BMMQ_EXPECT_SCALAR)
    if (active_backend() != Backend::Scalar || vw != 1u) {
        std::cerr << "FAIL: forced scalar build selected " << active_backend_name() << std::endl;
        return false;
    }
#endif
    return true;
}

static bool test_indexed_to_rgb565_with_pitch()
{
    BMMQ::RealtimeVideoSurface surface;
    surface.encoding = BMMQ::RealtimeVideoEncoding::Indexed2;
    surface.strideBytes = 1u;
    surface.indexedBytes = {0xE4u, 0x1Bu};
    surface.paletteArgb = {0xFF000000u, 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu};
    std::vector<std::uint16_t> destination(12u, 0xCAFEu);
    if (!convert_indexed_to_rgb565(surface, 4, 2, destination.data(), 6u)) return false;
    const std::array<std::uint16_t, 4> expected0{0x0000u, 0xF800u, 0x07E0u, 0x001Fu};
    const std::array<std::uint16_t, 4> expected1{0x001Fu, 0x07E0u, 0xF800u, 0x0000u};
    return std::equal(expected0.begin(), expected0.end(), destination.begin()) &&
        std::equal(expected1.begin(), expected1.end(), destination.begin() + 6) &&
        destination[4] == 0xCAFEu && destination[5] == 0xCAFEu;
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
        {"indexed_to_rgb565_with_pitch", test_indexed_to_rgb565_with_pitch},
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
