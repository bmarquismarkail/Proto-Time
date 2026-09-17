#pragma once

#include "ModHost.hpp"
#include <filesystem>
#include <optional>

namespace BMMQ::Modding {

struct LoadedMod {
    std::string id;
    std::string version;
    std::uint32_t priority = 0;
    std::unordered_map<std::string, std::uint32_t> regions;
    std::filesystem::path nativeModule;
    struct Trampoline { std::string symbol; std::uint32_t hookId = 0; };
    std::vector<Trampoline> trampolines;
};

struct PreparedMods {
    ModHost host;
    std::vector<std::uint8_t> rom;
    std::vector<LoadedMod> mods;
};

struct ModDirectoryLoadResult {
    std::optional<PreparedMods> prepared;
    std::string error;
};

// Preparation only: no guest writes, module execution, or window installation.
// Each explicitly selected directory contains manifest.json. Equal priorities
// are ordered by mod ID. All packages match the ORIGINAL ROM identity.
[[nodiscard]] ModDirectoryLoadResult loadModDirectories(
    std::span<const std::filesystem::path> directories,
    std::span<const std::uint8_t> rom, std::string_view machineId);

} // namespace BMMQ::Modding
