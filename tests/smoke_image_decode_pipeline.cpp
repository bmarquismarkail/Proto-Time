#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "machine/BackgroundTaskService.hpp"
#include "machine/ImageDecoder.hpp"

namespace {

// Minimal valid PNG file (1x1 white pixel)
std::vector<std::uint8_t> makeMinimalPng()
{
    return {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // PNG signature
        0x00, 0x00, 0x00, 0x0D,  // IHDR chunk length
        0x49, 0x48, 0x44, 0x52,  // IHDR
        0x00, 0x00, 0x00, 0x02,  // width: 2
        0x00, 0x00, 0x00, 0x02,  // height: 2
        0x08, 0x06, 0x00, 0x00, 0x00,  // 8-bit RGBA
        0x1F, 0x15, 0xC4, 0x89,  // CRC
        0x00, 0x00, 0x00, 0x0C,  // IDAT chunk length
        0x49, 0x44, 0x41, 0x54,  // IDAT
        0x08, 0x99, 0x01, 0x01, 0x00, 0x00, 0xFE, 0xFF,
        0x00, 0x00, 0x00, 0x05,  // IDAT data
        0x4B, 0x07, 0xF8, 0xC1,  // CRC
        0x00, 0x00, 0x00, 0x00,  // IEND chunk length
        0x49, 0x45, 0x4E, 0x44,  // IEND
        0xAE, 0x42, 0x60, 0x82,  // CRC
    };
}

} // namespace

int main()
{
    // Test 1: Synchronous fallback (no background service)
    {
        BMMQ::ImageDecoder decoder(nullptr);

        auto pngData = makeMinimalPng();
        BMMQ::DecodeSnapshot snapshot{
            .decodeId = "test-sync",
            .pngData = pngData,
        };

        auto future = decoder.decodeAsync(snapshot);
        assert(future.valid());

        // Should complete immediately (synchronous fallback)
        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(10)));

        const auto result = future.get();
        assert(result.success);
        assert(result.image.width == 2u);
        assert(result.image.height == 2u);
        assert(result.image.argbPixels.size() == 4u);

        const auto& stats = decoder.stats();
        assert(stats.decodeSubmissions == 1u);
        assert(stats.decodeSuccesses == 1u);
        assert(stats.decodeSynchronouslyFallbacks == 1u);
    }

    // Test 2: Async decode with background service
    {
        BMMQ::BackgroundTaskService bgService;
        bgService.start();

        BMMQ::ImageDecoder decoder(&bgService);

        auto pngData = makeMinimalPng();
        BMMQ::DecodeSnapshot snapshot{
            .decodeId = "test-async",
            .pngData = pngData,
        };

        auto future = decoder.decodeAsync(snapshot);
        assert(future.valid());

        // Wait for completion (with timeout)
        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(500)));

        const auto result = future.get();
        assert(result.success);
        assert(result.image.width == 2u);
        assert(result.image.height == 2u);
        assert(result.image.argbPixels.size() == 4u);

        const auto& stats = decoder.stats();
        assert(stats.decodeSubmissions == 1u);
        assert(stats.decodeSuccesses == 1u);
        assert(stats.decodeSynchronouslyFallbacks == 0u);

        bgService.shutdown();
    }

    // Test 3: Invalid PNG data
    {
        BMMQ::ImageDecoder decoder(nullptr);

        std::vector<std::uint8_t> badData{0xFF, 0xFF, 0xFF};  // Not a PNG
        BMMQ::DecodeSnapshot snapshot{
            .decodeId = "test-invalid",
            .pngData = badData,
        };

        auto future = decoder.decodeAsync(snapshot);
        assert(future.valid());
        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(10)));

        const auto result = future.get();
        assert(!result.success);
        assert(!result.error.empty());

        const auto& stats = decoder.stats();
        assert(stats.decodeSubmissions == 1u);
        assert(stats.decodeFailures == 1u);
    }

    // Test 4: Transform (flip) applied during decode
    {
        BMMQ::ImageDecoder decoder(nullptr);

        auto pngData = makeMinimalPng();
        BMMQ::DecodeSnapshot snapshot{
            .decodeId = "test-transform",
            .pngData = pngData,
            .transform = {.flipX = true},
        };

        auto future = decoder.decodeAsync(snapshot);
        assert(future.valid());
        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(10)));

        const auto result = future.get();
        assert(result.success);
        assert(result.image.width == 2u);
        assert(result.image.height == 2u);
    }

    // Test 5: Multiple concurrent decodes
    {
        BMMQ::BackgroundTaskService bgService;
        bgService.start();

        BMMQ::ImageDecoder decoder(&bgService);

        auto pngData = makeMinimalPng();
        std::vector<std::future<BMMQ::DecodeResult>> futures;

        // Submit multiple decode tasks
        for (int i = 0; i < 3; ++i) {
            BMMQ::DecodeSnapshot snapshot{
                .decodeId = "test-concurrent-" + std::to_string(i),
                .pngData = pngData,
            };
            futures.push_back(decoder.decodeAsync(snapshot));
        }

        // Wait for all to complete
        for (auto& future : futures) {
            assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(500)));
            const auto result = future.get();
            assert(result.success);
        }

        const auto& stats = decoder.stats();
        assert(stats.decodeSubmissions == 3u);
        assert(stats.decodeSuccesses == 3u);

        bgService.shutdown();
    }

    return 0;
}
