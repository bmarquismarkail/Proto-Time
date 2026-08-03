#ifndef BMMQ_EMULATOR_CONFIG_HPP
#define BMMQ_EMULATOR_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace BMMQ {

struct AudioProcessorConfigSpec {
    std::string pluginId;
    std::filesystem::path jsonPath;
};

struct EmulatorConfig {
    std::optional<std::string> machineKind;
    std::filesystem::path romPath;
    std::optional<std::filesystem::path> bootRomPath;
    std::optional<std::filesystem::path> pluginPath;
    std::optional<std::string> frontendId;
    std::optional<std::filesystem::path> executorPluginPath;
    std::optional<std::string> executorPolicyId;
    std::optional<std::filesystem::path> irAdapterPluginPath;
    std::optional<std::string> irAdapterId;
    std::optional<std::filesystem::path> irBackendPluginPath;
    std::optional<std::string> irBackendId;
    std::optional<std::uint64_t> stepLimit;
    std::uint32_t windowScale = 3;
    // HD texture replacement scale factor. When > 1, output frames are scaled
    // by this factor and replaced tiles sample their replacement images at the
    // higher resolution. Default 1 preserves existing behavior. Clamped to [1, 8].
    std::uint32_t hdScale = 1;
    bool headless = false;
    std::string cpuMode = "baseline";
    bool cpuDetailedTiming = false;
    bool unthrottled = false;
    double speedMultiplier = 1.0;
    bool startPaused = false;
    std::optional<std::string> timingProfile;
    std::optional<std::filesystem::path> diagnosticsReportPath;
    std::uint32_t diagnosticsIntervalMs = 1000;
    bool audioEnabled = true;
    std::string audioBackend = "sdl";
    std::optional<std::filesystem::path> audioPluginPath;
    std::optional<std::filesystem::path> audioOutputFilePath;
    std::optional<std::filesystem::path> midiOutputFilePath;
    std::optional<std::string> midiOutputPort;
    std::uint32_t audioReadyQueueChunks = 3;
    std::uint32_t audioBatchChunks = 1;
    std::vector<std::filesystem::path> audioProcessorPluginPaths;
    std::vector<AudioProcessorConfigSpec> audioProcessorConfigs;
    std::uint32_t backgroundWorkers = 0;
    std::uint32_t backgroundQueueCapacity = 1024;
    bool debugSnapshotsEnabled = false;
    std::vector<std::filesystem::path> visualPackPaths;
    std::optional<std::filesystem::path> visualCapturePath;
    bool visualPackReload = false;
};

struct CommandLineConfigOverrides {
    std::optional<std::string> machineKind;
    std::optional<std::filesystem::path> romPath;
    std::optional<std::filesystem::path> bootRomPath;
    std::optional<std::filesystem::path> pluginPath;
    std::optional<std::string> frontendId;
    std::optional<std::filesystem::path> executorPluginPath;
    std::optional<std::string> executorPolicyId;
    std::optional<std::filesystem::path> irAdapterPluginPath;
    std::optional<std::string> irAdapterId;
    std::optional<std::filesystem::path> irBackendPluginPath;
    std::optional<std::string> irBackendId;
    std::optional<std::uint64_t> stepLimit;
    std::optional<std::uint32_t> windowScale;
    // HD texture replacement scale factor. Clamped to [1, 8] on application.
    std::optional<std::uint32_t> hdScale;
    std::optional<bool> headless;
    std::optional<std::string> cpuMode;
    std::optional<bool> cpuDetailedTiming;
    std::optional<bool> unthrottled;
    std::optional<double> speedMultiplier;
    std::optional<bool> startPaused;
    std::optional<std::string> timingProfile;
    std::optional<std::filesystem::path> diagnosticsReportPath;
    std::optional<std::uint32_t> diagnosticsIntervalMs;
    std::optional<bool> audioEnabled;
    std::optional<std::string> audioBackend;
    std::optional<std::filesystem::path> audioPluginPath;
    std::optional<std::filesystem::path> audioOutputFilePath;
    std::optional<std::filesystem::path> midiOutputFilePath;
    std::optional<std::string> midiOutputPort;
    std::optional<std::uint32_t> audioReadyQueueChunks;
    std::optional<std::uint32_t> audioBatchChunks;
    std::optional<std::vector<std::filesystem::path>> audioProcessorPluginPaths;
    std::optional<std::vector<AudioProcessorConfigSpec>> audioProcessorConfigs;
    std::optional<std::uint32_t> backgroundWorkers;
    std::optional<std::uint32_t> backgroundQueueCapacity;
    std::optional<bool> debugSnapshotsEnabled;
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
