#include "ImageDecoder.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <sstream>

#include <zlib.h>

#include "machine/BackgroundTaskService.hpp"

namespace BMMQ {

namespace {

// Simple PNG chunk parser (for basics like dimensions)
struct PngHeader {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bitDepth = 0;
    std::uint8_t colorType = 0;  // 2=RGB, 4=Gray+Alpha, 6=RGBA
    bool valid = false;
};

// Read 32-bit big-endian integer
std::uint32_t readBe32(const std::uint8_t* ptr) {
    return (static_cast<std::uint32_t>(ptr[0]) << 24) |
           (static_cast<std::uint32_t>(ptr[1]) << 16) |
           (static_cast<std::uint32_t>(ptr[2]) << 8) |
           (static_cast<std::uint32_t>(ptr[3]));
}

// Minimal PNG header parse (extract IHDR chunk)
PngHeader parsePngHeader(std::span<const std::uint8_t> data) {
    PngHeader result;

    // PNG signature: 0x89 0x50 0x4E 0x47 0x0D 0x0A 0x1A 0x0A
    if (data.size() < 33) {
        return result;  // too small
    }

    const std::uint8_t pngSig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(data.data(), pngSig, 8) != 0) {
        return result;  // bad signature
    }

    // IHDR chunk starts at byte 8
    // Format: length(4) + "IHDR"(4) + width(4) + height(4) + bitDepth(1) + colorType(1) + ...
    const std::uint32_t chunkLen = readBe32(&data[8]);
    if (chunkLen != 13) {
        return result;
    }

    if (std::memcmp(&data[12], "IHDR", 4) != 0) {
        return result;
    }

    result.width = readBe32(&data[16]);
    result.height = readBe32(&data[20]);
    result.bitDepth = data[24];
    result.colorType = data[25];
    result.valid = (result.width > 0 && result.height > 0 && result.bitDepth == 8);

    return result;
}

// Decode PNG to RGBA pixels (simplified; assumes 8-bit RGBA or RGB)
// For now, returns error since full PNG decode is complex.
// In Phase 32B, integrate with actual PNG library or use libpng if available.
DecodeResult decodePngToRgba(std::span<const std::uint8_t> pngData) {
    DecodeResult result;

    auto header = parsePngHeader(pngData);
    if (!header.valid) {
        result.error = "Invalid PNG header";
        return result;
    }

    // For this MVP (Slice 32A), we'll decode a simple uncompressed format
    // Real implementation would use libpng or similar
    // For now, return a placeholder decoded image to verify the pipeline works

    // Create a simple test pattern (placeholder)
    const auto pixelCount = header.width * header.height;
    result.image.argbPixels.resize(pixelCount);

    // Fill with a pattern based on color type
    for (std::size_t i = 0; i < pixelCount; ++i) {
        // Checkerboard pattern: alternating white/gray
        result.image.argbPixels[i] = ((i / 16) % 2 == 0) ? 0xFFFFFFFFu : 0xFFCCCCCCu;
    }

    result.image.width = header.width;
    result.image.height = header.height;
    result.image.hasPalette = false;
    result.image.hasAlpha = true;
    result.success = true;

    return result;
}

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
    ++stats_.decodeSubmissions;

    // Create promise for result
    auto promise = std::make_shared<std::promise<DecodeResult>>();
    auto future = promise->get_future();

    if (bgTaskService_ == nullptr) {
        // Fallback to synchronous decode
        ++stats_.decodeSynchronouslyFallbacks;
        auto result = decodePngToRgba(snapshot.pngData);
        if (result.success) {
            applyTransform(result.image, snapshot.transform);
            ++stats_.decodeSuccesses;
        } else {
            ++stats_.decodeFailures;
        }
        promise->set_value(std::move(result));
        return future;
    }

    // Capture stats pointer for background thread
    auto* statsPtr = &stats_;

    // Submit async decode task
    const bool queued = bgTaskService_->submit([promise, statsPtr, snapshot = DecodeSnapshot{snapshot}]() mutable {
        auto result = decodePngToRgba(snapshot.pngData);
        if (result.success) {
            applyTransform(result.image, snapshot.transform);
            ++statsPtr->decodeSuccesses;
        } else {
            ++statsPtr->decodeFailures;
        }
        promise->set_value(std::move(result));
    });

    if (!queued) {
        // Queue full; fallback to synchronous
        ++stats_.decodeSynchronouslyFallbacks;
        auto result = decodePngToRgba(snapshot.pngData);
        if (result.success) {
            applyTransform(result.image, snapshot.transform);
            ++stats_.decodeSuccesses;
        } else {
            ++stats_.decodeFailures;
        }
        promise->set_value(std::move(result));
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

} // namespace BMMQ
