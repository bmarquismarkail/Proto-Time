#ifndef BMMQ_EMULATOR_CONFIG_HPP
#define BMMQ_EMULATOR_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace BMMQ {

struct EmulatorConfig {
    std::filesystem::path romPath;
    std::optional<std::filesystem::path> bootRomPath;
    std::optional<std::filesystem::path> pluginPath;
    std::optional<std::uint64_t> stepLimit;
    std::uint32_t windowScale = 3;
    bool headless = false;
    bool unthrottled = false;
    double speedMultiplier = 1.0;
    bool startPaused = false;
    bool audioEnabled = true;
    std::string audioBackend = "sdl";
    std::optional<std::filesystem::path> texturePackPath;
    std::optional<std::filesystem::path> visualDumpPath;
};

struct CommandLineConfigOverrides {
    std::optional<std::filesystem::path> romPath;
    std::optional<std::filesystem::path> bootRomPath;
    std::optional<std::filesystem::path> pluginPath;
    std::optional<std::uint64_t> stepLimit;
    std::optional<std::uint32_t> windowScale;
    std::optional<bool> headless;
    std::optional<bool> unthrottled;
    std::optional<double> speedMultiplier;
    std::optional<bool> startPaused;
    std::optional<bool> audioEnabled;
    std::optional<std::string> audioBackend;
    std::optional<std::filesystem::path> texturePackPath;
    std::optional<std::filesystem::path> visualDumpPath;
};

struct ParsedEmulatorArguments {
    std::optional<std::filesystem::path> configPath;
    CommandLineConfigOverrides overrides;
    bool helpRequested = false;
};

[[nodiscard]] EmulatorConfig loadEmulatorConfig(const std::filesystem::path& path);
void applyOverrides(EmulatorConfig& config, const CommandLineConfigOverrides& overrides);
void validateEmulatorConfig(const EmulatorConfig& config);
[[nodiscard]] ParsedEmulatorArguments parseEmulatorArguments(int argc, char** argv);
[[nodiscard]] EmulatorConfig resolveEmulatorConfig(const ParsedEmulatorArguments& arguments);

} // namespace BMMQ

#endif // BMMQ_EMULATOR_CONFIG_HPP
