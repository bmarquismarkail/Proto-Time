#pragma once
#include <memory>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <filesystem>
#include "GameGearMapper.hpp"

// Factory helper to construct the appropriate mapper instance for a ROM.
// The returned mapper will have had `load(...)` called on it. If a
// `romPath` is provided the factory will consult the filename when
// attempting to select a specific mapper.
std::unique_ptr<GameGearMapper> createMapperFromRom(const uint8_t* data,
												   size_t size,
												   const std::optional<std::filesystem::path>& romPath = std::nullopt);
