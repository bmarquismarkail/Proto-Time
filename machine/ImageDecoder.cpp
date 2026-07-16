#include "ImageDecoder.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>

#include "machine/BackgroundTaskService.hpp"
#include "machine/PngDecode.hpp"
#include "machine/VisualPackLimits.hpp"

namespace BMMQ {

namespace {

// Apply transform to decoded image (flip, rotate)
void applyTransform(DecodedImage& image, const DecodeSnapshot::TransformOptions& transform) {
    if (transform.flipX) {
        for (std::uint32_t y = 0; y < image.height; ++y) {
            for (std::uint32_t x = 0; x < image.width / 2; ++x) {
                const auto left = y * image.width + x;
                const auto right = y * image.width + (image.width - 1 - x);
                std::swap(image.argbPixels[left], image.argbPixels[right]);
            }
        }
    }

    if (transform.flipY) {
        for (std::uint32_t y = 0; y < image.height / 2; ++y) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                const auto top = y * image.width + x;
                const auto bottom = (image.height - 1 - y) * image.width + x;
                std::swap(image.argbPixels[top], image.argbPixels[bottom]);
            }
        }
    }

    // Rotation (90° only for MVP)
    if (transform.rotationDegrees == 90) {
        std::vector<std::uint32_t> rotated(image.argbPixels.size());
        for (std::uint32_t y = 0; y < image.height; ++y) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                const auto src = y * image.width + x;
                const auto dst = x * image.height + (image.height - 1 - y);
                rotated[dst] = image.argbPixels[src];
            }
        }
        image.argbPixels = std::move(rotated);
        std::swap(image.width, image.height);
    }
}

} // namespace

ImageDecoder::ImageDecoder(BackgroundTaskService* bgTaskService) noexcept
    : bgTaskService_(bgTaskService)
{
}

std::future<DecodeResult> ImageDecoder::decodeAsync(const DecodeSnapshot& snapshot)
{
    stats_->decodeSubmissions.fetch_add(1u, std::memory_order_relaxed);

    // Create promise for result
    auto promise = std::make_shared<std::promise<DecodeResult>>();
    auto future = promise->get_future();

    if (bgTaskService_ == nullptr) {
        // Fallback to synchronous decode
        stats_->decodeSynchronouslyFallbacks.fetch_add(1u, std::memory_order_relaxed);
        auto result = decodePngToRgba(snapshot.pngData);
        if (result.success) {
            applyTransform(result.image, snapshot.transform);
            stats_->decodeSuccesses.fetch_add(1u, std::memory_order_relaxed);
        } else {
            stats_->decodeFailures.fetch_add(1u, std::memory_order_relaxed);
        }
        promise->set_value(std::move(result));
        return future;
    }

    auto stats = stats_;

    // Submit async decode task
    const bool queued = bgTaskService_->submit(BackgroundJobCategory::VisualDecode,
        [promise, stats, snapshot = DecodeSnapshot{snapshot}]() mutable {
        auto result = decodePngToRgba(snapshot.pngData);
        if (result.success) {
            applyTransform(result.image, snapshot.transform);
            stats->decodeSuccesses.fetch_add(1u, std::memory_order_relaxed);
        } else {
            stats->decodeFailures.fetch_add(1u, std::memory_order_relaxed);
        }
        promise->set_value(std::move(result));
    });

    if (!queued) {
        stats_->decodeRejected.fetch_add(1u, std::memory_order_relaxed);
        promise->set_value(DecodeResult{.success = false, .image = {}, .error = "background decode queue full"});
    }

    return future;
}

std::future<DecodeResult> ImageDecoder::decodeFileAsync(const std::filesystem::path& path)
{
    stats_->decodeSubmissions.fetch_add(1u, std::memory_order_relaxed);
    auto promise = std::make_shared<std::promise<DecodeResult>>();
    auto future = promise->get_future();
    if (bgTaskService_ == nullptr) {
        stats_->decodeRejected.fetch_add(1u, std::memory_order_relaxed);
        promise->set_value(DecodeResult{.success = false, .image = {}, .error = "background decoder unavailable"});
        return future;
    }
    auto stats = stats_;
    const bool queued = bgTaskService_->submit(BackgroundJobCategory::VisualDecode,
        [promise, stats, path]() mutable {
            std::error_code sizeError;
            const auto fileSize = std::filesystem::file_size(path, sizeError);
            if (!sizeError && fileSize > VisualPackLimits::kMaxPngBytes) {
                stats->decodeFailures.fetch_add(1u, std::memory_order_relaxed);
                promise->set_value(DecodeResult{
                    .success = false,
                    .image = {},
                    .error = "replacement PNG too large: " + path.string(),
                });
                return;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                stats->decodeFailures.fetch_add(1u, std::memory_order_relaxed);
                promise->set_value(DecodeResult{
                    .success = false,
                    .image = {},
                    .error = "unable to read PNG: " + path.string(),
                });
                return;
            }
            std::vector<std::uint8_t> bytes(
                (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            auto result = decodePngToRgba(bytes);
            if (result.success) {
                stats->decodeSuccesses.fetch_add(1u, std::memory_order_relaxed);
            } else {
                stats->decodeFailures.fetch_add(1u, std::memory_order_relaxed);
            }
            promise->set_value(std::move(result));
        });
    if (!queued) {
        stats_->decodeRejected.fetch_add(1u, std::memory_order_relaxed);
        promise->set_value(DecodeResult{.success = false, .image = {}, .error = "background decode queue full"});
    }
    return future;
}

bool ImageDecoder::waitDecodeResult(std::future<DecodeResult>& future,
                                     std::chrono::milliseconds timeout) noexcept
{
    if (!future.valid()) {
        return false;
    }

    const auto status = future.wait_for(timeout);
    return status == std::future_status::ready;
}

ImageDecoder::Statistics ImageDecoder::snapshotStatistics(const AtomicStatistics& stats) noexcept
{
    return Statistics{
        .decodeSubmissions = stats.decodeSubmissions.load(std::memory_order_relaxed),
        .decodeSuccesses = stats.decodeSuccesses.load(std::memory_order_relaxed),
        .decodeFailures = stats.decodeFailures.load(std::memory_order_relaxed),
        .decodeSynchronouslyFallbacks = stats.decodeSynchronouslyFallbacks.load(std::memory_order_relaxed),
        .decodeRejected = stats.decodeRejected.load(std::memory_order_relaxed),
    };
}

ImageDecoder::Statistics ImageDecoder::stats() const noexcept
{
    return snapshotStatistics(*stats_);
}

} // namespace BMMQ
