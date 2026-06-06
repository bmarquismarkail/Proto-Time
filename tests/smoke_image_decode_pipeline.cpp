#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "machine/BackgroundTaskService.hpp"
#include "machine/ImageDecoder.hpp"
#include "tests/visual_test_helpers.hpp"

int main()
{
    namespace Visual = BMMQ::Tests::Visual;
    const auto makeSnapshot = [](std::string id, std::vector<std::uint8_t> pngData) {
        BMMQ::DecodeSnapshot snapshot{};
        snapshot.decodeId = std::move(id);
        snapshot.pngData = std::move(pngData);
        return snapshot;
    };

    // Test 1: Synchronous fallback (no background service)
    {
        BMMQ::ImageDecoder decoder(nullptr);

        auto pngData = Visual::makePng2x2Rgba();
        auto snapshot = makeSnapshot("test-sync", std::move(pngData));

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

        auto pngData = Visual::makePng2x2Rgba();
        auto snapshot = makeSnapshot("test-async", std::move(pngData));

        auto future = decoder.decodeAsync(snapshot);
        assert(future.valid());

        // Wait for completion (with timeout)
        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(500)));

        const auto result = future.get();
        assert(result.success);
        assert(result.image.width == 2u);
        assert(result.image.height == 2u);
        assert(result.image.argbPixels.size() == 4u);

        const auto stats = decoder.stats();
        assert(stats.decodeSubmissions == 1u);
        assert(stats.decodeSuccesses == 1u);
        assert(stats.decodeSynchronouslyFallbacks == 0u);

        bgService.shutdown();
    }

    // Test 3: Invalid PNG data
    {
        BMMQ::ImageDecoder decoder(nullptr);

        std::vector<std::uint8_t> badData{0xFF, 0xFF, 0xFF};  // Not a PNG
        auto snapshot = makeSnapshot("test-invalid", std::move(badData));

        auto future = decoder.decodeAsync(snapshot);
        assert(future.valid());
        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(10)));

        const auto result = future.get();
        assert(!result.success);
        assert(!result.error.empty());

        const auto stats = decoder.stats();
        assert(stats.decodeSubmissions == 1u);
        assert(stats.decodeFailures == 1u);
    }

    // Test 4: Transform (flip) applied during decode
    {
        BMMQ::ImageDecoder decoder(nullptr);

        auto pngData = Visual::makePng2x2Rgba();
        auto snapshot = makeSnapshot("test-transform", std::move(pngData));
        snapshot.transform.flipX = true;

        auto future = decoder.decodeAsync(snapshot);
        assert(future.valid());
        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(10)));

        const auto result = future.get();
        assert(result.success);
        assert(result.image.width == 2u);
        assert(result.image.height == 2u);
    }

    // Test 5: Async decode owns PNG bytes past caller lifetime
    {
        BMMQ::BackgroundTaskService bgService;
        bgService.start();

        BMMQ::ImageDecoder decoder(&bgService);

        std::future<BMMQ::DecodeResult> future;
        {
            auto transientPngData = Visual::makePng2x2Rgba();
            auto snapshot = makeSnapshot("test-owned-png-bytes", std::move(transientPngData));
            future = decoder.decodeAsync(snapshot);
        }

        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(500)));
        const auto result = future.get();
        assert(result.success);
        assert(result.image.width == 2u);
        assert(result.image.height == 2u);

        bgService.shutdown();
    }

    // Test 6: Successful decode must produce PNG pixels, not placeholder pattern
    {
        BMMQ::ImageDecoder decoder(nullptr);

        constexpr std::uint32_t kExpectedArgb = 0xFF3366CCu;
        auto snapshot = makeSnapshot("test-no-placeholder", Visual::makeSolidPng2x2Rgba(kExpectedArgb));

        auto future = decoder.decodeAsync(snapshot);
        assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(50)));
        const auto result = future.get();
        assert(result.success);
        assert(result.image.argbPixels.size() == 4u);
        for (const auto pixel : result.image.argbPixels) {
            assert(pixel == kExpectedArgb);
        }
    }

    // Test 7: Stats sampling remains valid while async decode tasks are active
    {
        BMMQ::BackgroundTaskService bgService;
        bgService.start();

        BMMQ::ImageDecoder decoder(&bgService);
        std::vector<std::future<BMMQ::DecodeResult>> futures;
        futures.reserve(32u);

        std::atomic<bool> keepSampling{true};
        std::thread sampler([&decoder, &keepSampling]() {
            while (keepSampling.load(std::memory_order_relaxed)) {
                const auto s = decoder.stats();
                assert(s.decodeSubmissions >= s.decodeSuccesses + s.decodeFailures);
            }
        });

        for (int i = 0; i < 32; ++i) {
            auto snapshot = makeSnapshot("test-concurrent-" + std::to_string(i), Visual::makePng2x2Rgba());
            futures.push_back(decoder.decodeAsync(snapshot));
        }

        for (auto& future : futures) {
            assert(BMMQ::ImageDecoder::waitDecodeResult(future, std::chrono::milliseconds(500)));
            assert(future.get().success);
        }

        keepSampling.store(false, std::memory_order_relaxed);
        sampler.join();

        const auto stats = decoder.stats();
        assert(stats.decodeSubmissions == 32u);
        assert(stats.decodeSuccesses == 32u);

        bgService.shutdown();
    }

    return 0;
}
