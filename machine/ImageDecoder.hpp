#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <string>

#include "machine/ImageDecoderTypes.hpp"

namespace BMMQ {

class BackgroundTaskService;

// Async PNG → RGBA image decoder
// Decodes PNG files asynchronously on background thread while keeping
// result available to emulation thread via std::future.
class ImageDecoder {
public:
    explicit ImageDecoder(BackgroundTaskService* bgTaskService) noexcept;
    ~ImageDecoder() noexcept = default;

    // Non-copyable, non-movable (manages futures in-flight)
    ImageDecoder(const ImageDecoder&) = delete;
    ImageDecoder& operator=(const ImageDecoder&) = delete;

    // Submit PNG for async decode
    // Returns a future that will contain the decoded image or error.
    // The future is ready after decode completes on background thread.
    std::future<DecodeResult> decodeAsync(const DecodeSnapshot& snapshot);

    // Wait synchronously for a future (with optional timeout)
    // Called by emulation thread when it needs the decoded result immediately.
    // Returns true if result available; false if timeout exceeded.
    static bool waitDecodeResult(std::future<DecodeResult>& future,
                                 std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) noexcept;

    // Diagnostics
    struct Statistics {
        std::size_t decodeSubmissions = 0;
        std::size_t decodeSuccesses = 0;
        std::size_t decodeFailures = 0;
        std::size_t decodeSynchronouslyFallbacks = 0;
    };

    [[nodiscard]] Statistics stats() const noexcept;

private:
    struct AtomicStatistics {
        std::atomic<std::size_t> decodeSubmissions{0};
        std::atomic<std::size_t> decodeSuccesses{0};
        std::atomic<std::size_t> decodeFailures{0};
        std::atomic<std::size_t> decodeSynchronouslyFallbacks{0};
    };

    [[nodiscard]] static Statistics snapshotStatistics(const AtomicStatistics& stats) noexcept;

    BackgroundTaskService* bgTaskService_;
    AtomicStatistics stats_{};
};

} // namespace BMMQ
