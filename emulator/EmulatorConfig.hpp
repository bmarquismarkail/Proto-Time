#ifndef BMMQ_EMULATOR_CONFIG_HPP
#define BMMQ_EMULATOR_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace BMMQ {

struct EmulatorConfig {
    std::optional<std::string> machineKind;
    std::filesystem::path romPath;
    std::optional<std::filesystem::path> bootRomPath;
    std::optional<std::filesystem::path> pluginPath;
    std::optional<std::uint64_t> stepLimit;
    std::uint32_t windowScale = 3;
    bool headless = false;
    bool unthrottled = false;
    double speedMultiplier = 1.0;
    bool startPaused = false;
    std::optional<std::string> timingProfile;
    std::optional<std::filesystem::path> diagnosticsReportPath;
    std::uint32_t diagnosticsIntervalMs = 1000;
    bool audioEnabled = true;
    std::string audioBackend = "sdl";
    std::uint32_t audioReadyQueueChunks = 3;
    std::uint32_t audioBatchChunks = 1;
    std::vector<std::filesystem::path> visualPackPaths;
    std::optional<std::filesystem::path> visualCapturePath;
    bool visualPackReload = false;
};

struct CommandLineConfigOverrides {
    std::optional<std::string> machineKind;
    std::optional<std::filesystem::path> romPath;
    std::optional<std::filesystem::path> bootRomPath;
    std::optional<std::filesystem::path> pluginPath;
    std::optional<std::uint64_t> stepLimit;
    std::optional<std::uint32_t> windowScale;
    std::optional<bool> headless;
    std::optional<bool> unthrottled;
    std::optional<double> speedMultiplier;
    std::optional<bool> startPaused;
    std::optional<std::string> timingProfile;
    std::optional<std::filesystem::path> diagnosticsReportPath;
    std::optional<std::uint32_t> diagnosticsIntervalMs;
    std::optional<bool> audioEnabled;
    std::optional<std::string> audioBackend;
    std::optional<std::uint32_t> audioReadyQueueChunks;
    std::optional<std::uint32_t> audioBatchChunks;
    std::optional<std::vector<std::filesystem::path>> visualPackPaths;
    std::optional<std::filesystem::path> visualCapturePath;
    std::optional<bool> visualPackReload;
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
