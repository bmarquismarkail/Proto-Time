#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BMMQ {

// Input snapshot for async image decode
struct DecodeSnapshot {
    // Unique identifier for this decode operation (for logging/diagnostics)
    std::string decodeId;

    // PNG binary data owned by the snapshot so async workers never observe
    // dangling caller memory.
    std::vector<std::uint8_t> pngData;

    // Target output dimensions (0 = preserve original)
    std::uint32_t targetWidth = 0;
    std::uint32_t targetHeight = 0;

    // Post-processing options
    struct TransformOptions {
        bool flipX = false;
        bool flipY = false;
        std::uint32_t rotationDegrees = 0;  // 0, 90, 180, 270
    } transform;

    struct ScalePolicy {
        enum class Kind {
            Preserve,    // keep original size
            Fit,         // fit within target
            Fill,        // fill target
            Crop,        // crop to target
            Exact,       // must match exactly; fail if mismatch
        } kind = Kind::Preserve;
    } scalePolicy;

    struct Slicing {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    } slicing;
};

// Output of successful image decode
struct DecodedImage {
    // Final RGBA pixel data (8 bytes per pixel: AAAABBGGRR in little-endian)
    std::vector<std::uint32_t> argbPixels;

    // Final dimensions
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    // Metadata
    bool hasPalette = false;
    bool hasAlpha = false;
};

// Decode result: success or error
struct DecodeResult {
    bool success = false;
    DecodedImage image;
    std::string error;

    explicit operator bool() const noexcept { return success; }
};

} // namespace BMMQ
