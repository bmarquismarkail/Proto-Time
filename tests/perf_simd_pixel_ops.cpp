#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>

#include "machine/plugins/video/SimdPixelOps.hpp"

namespace {

using Clock = std::chrono::steady_clock;

void scalarConvert(const std::uint32_t* source, std::uint16_t* destination, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        const auto pixel = source[i];
        destination[i] = static_cast<std::uint16_t>(
            (((pixel >> 19u) & 0x1Fu) << 11u) |
            (((pixel >> 10u) & 0x3Fu) << 5u) |
            ((pixel >> 3u) & 0x1Fu));
    }
}

void scalarReplace(const std::uint32_t* source, const std::uint8_t* mask,
                   const std::uint32_t* replacements, std::uint32_t* destination,
                   std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        destination[i] = mask[i] != 0u ? replacements[i] : source[i];
    }
}

template <typename Operation>
std::array<std::int64_t, 3> percentiles(Operation&& operation, std::size_t iterations)
{
    std::vector<std::int64_t> samples;
    samples.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = Clock::now();
        operation();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double percentile) {
        return samples[std::min(samples.size() - 1u,
            static_cast<std::size_t>(percentile * static_cast<double>(samples.size() - 1u)))];
    };
    return {at(0.50), at(0.95), at(0.99)};
}

void printResult(std::string_view sizeName, std::string_view operation,
                 const std::array<std::int64_t, 3>& active,
                 const std::array<std::int64_t, 3>& scalar)
{
    std::cout << sizeName << ' ' << operation
              << " active_p50_ns=" << active[0] << " active_p95_ns=" << active[1]
              << " active_p99_ns=" << active[2] << " scalar_p50_ns=" << scalar[0]
              << " scalar_p95_ns=" << scalar[1] << " scalar_p99_ns=" << scalar[2]
              << " p50_speedup=" << static_cast<double>(scalar[0]) /
                     static_cast<double>(std::max(active[0], std::int64_t{1}))
              << '\n';
}

bool benchmarkSize(std::string_view name, std::size_t pixelCount)
{
    std::mt19937 random(0x50484153u + static_cast<unsigned>(pixelCount));
    std::vector<std::uint32_t> source(pixelCount), replacements(pixelCount), activeArgb(pixelCount), scalarArgb(pixelCount);
    std::vector<std::uint16_t> active565(pixelCount), scalar565(pixelCount);
    std::vector<std::uint8_t> mask(pixelCount);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        source[i] = random();
        replacements[i] = random();
        mask[i] = static_cast<std::uint8_t>((random() & 3u) == 0u);
    }

    BMMQ::SimdPixelOps::convert_argb8888_to_rgb565(source.data(), active565.data(), pixelCount);
    scalarConvert(source.data(), scalar565.data(), pixelCount);
    BMMQ::SimdPixelOps::replace_pixels_with_mask(
        source.data(), mask.data(), replacements.data(), activeArgb.data(), pixelCount);
    scalarReplace(source.data(), mask.data(), replacements.data(), scalarArgb.data(), pixelCount);
    if (active565 != scalar565 || activeArgb != scalarArgb) return false;

    constexpr std::size_t iterations = 400u;
    const auto activeConvert = percentiles([&] {
        BMMQ::SimdPixelOps::convert_argb8888_to_rgb565(source.data(), active565.data(), pixelCount);
    }, iterations);
    const auto scalarConvertResult = percentiles([&] {
        scalarConvert(source.data(), scalar565.data(), pixelCount);
    }, iterations);
    const auto activeReplace = percentiles([&] {
        BMMQ::SimdPixelOps::replace_pixels_with_mask(
            source.data(), mask.data(), replacements.data(), activeArgb.data(), pixelCount);
    }, iterations);
    const auto scalarReplaceResult = percentiles([&] {
        scalarReplace(source.data(), mask.data(), replacements.data(), scalarArgb.data(), pixelCount);
    }, iterations);
    printResult(name, "argb_to_rgb565", activeConvert, scalarConvertResult);
    printResult(name, "masked_replace", activeReplace, scalarReplaceResult);
    const auto checksum = std::accumulate(active565.begin(), active565.end(), std::uint64_t{0}) +
        std::accumulate(activeArgb.begin(), activeArgb.end(), std::uint64_t{0});
    std::cout << name << " checksum=" << checksum << '\n';
    return true;
}

} // namespace

int main()
{
    std::cout << "simd_backend=" << BMMQ::SimdPixelOps::active_backend_name() << '\n';
    return benchmarkSize("gameboy_160x144", 160u * 144u) &&
                   benchmarkSize("gamegear_256x192", 256u * 192u)
               ? 0 : 1;
}
