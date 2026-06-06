#pragma once

#include <cstdint>
#include <span>

#include "machine/ImageDecoderTypes.hpp"

namespace BMMQ {

// Shared PNG decode helper used by both synchronous visual override loading
// and async ImageDecoder worker tasks.
[[nodiscard]] DecodeResult decodePngToRgba(std::span<const std::uint8_t> pngData) noexcept;

} // namespace BMMQ
