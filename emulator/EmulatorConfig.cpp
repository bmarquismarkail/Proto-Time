#include "emulator/EmulatorConfig.hpp"
#include "inst_cycle/executor/ExecutorPolicyRegistry.hpp"
#include "machine/plugins/DynamicPluginModule.hpp"

#include "emulator/MachineFactory.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace BMMQ {
namespace {

[[nodiscard]] std::string trim(std::string_view value)
{
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
        --end;
    }
    return std::string(begin, end);
}

[[nodiscard]] std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

[[nodiscard]] std::filesystem::path resolveConfigPath(
    const std::filesystem::path& configDirectory,
    const std::string& value)
{
    const auto path = std::filesystem::path(value);
    if (path.is_relative() && !configDirectory.empty()) {
        return configDirectory / path;
    }
    return path;
}

[[nodiscard]] std::uint64_t parseUnsigned(std::string_view value, std::string_view label)
{
    const auto text = trim(value);
    if (text.empty() || text.front() == '-') {
        throw std::invalid_argument("Invalid unsigned value for " + std::string(label) + ": " + text);
    }

    try {
        std::size_t parsedChars = 0;
        const auto parsedValue = std::stoull(text, &parsedChars, 0);
        if (parsedChars != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return static_cast<std::uint64_t>(parsedValue);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid unsigned value for " + std::string(label) + ": " + text);
    }
}

[[nodiscard]] double parseDouble(std::string_view value, std::string_view label)
{
    const auto text = trim(value);
    try {
        std::size_t parsedChars = 0;
        const auto parsedValue = std::stod(text, &parsedChars);
        if (parsedChars != text.size() || !std::isfinite(parsedValue)) {
            throw std::invalid_argument("invalid numeric value");
        }
        return parsedValue;
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid numeric value for " + std::string(label) + ": " + text);
    }
}

[[nodiscard]] bool parseBool(std::string_view value, std::string_view label)
{
    const auto text = lowerAscii(trim(value));
    if (text == "true" || text == "yes" || text == "on" || text == "1") {
        return true;
    }
    if (text == "false" || text == "no" || text == "off" || text == "0") {
        return false;
    }
    throw std::invalid_argument("Invalid boolean value for " + std::string(label) + ": " + std::string(value));
}

void assignRomPath(CommandLineConfigOverrides& overrides, const char* value)
{
    if (overrides.romPath.has_value()) {
        throw std::invalid_argument("ROM path was provided more than once");
    }
    overrides.romPath = std::filesystem::path(value);
}

void applyConfigValue(EmulatorConfig& config,
                      const std::filesystem::path& configDirectory,
                      std::string_view section,
                      std::string_view key,
                      std::string_view value)
{
    const auto label = std::string(section) + "." + std::string(key);
    const auto text = trim(value);
    if (section == "emulator") {
        if (key == "core") {
            config.machineKind = lowerAscii(text);
        } else if (key == "rom") {
            config.romPath = resolveConfigPath(configDirectory, text);
        } else if (key == "boot_rom") {
            config.bootRomPath = resolveConfigPath(configDirectory, text);
        } else if (key == "plugin" || key == "frontend_plugin") {
            config.pluginPath = resolveConfigPath(configDirectory, text);
        } else if (key == "frontend") {
            config.frontendId = text;
            config.headless = text == "headless";
        } else if (key == "executor_plugin") {
            config.executorPluginPath = resolveConfigPath(configDirectory, text);
        } else if (key == "executor_policy") {
            config.executorPolicyId = text;
        } else if (key == "ir_adapter_plugin") {
            config.irAdapterPluginPath = resolveConfigPath(configDirectory, text);
        } else if (key == "ir_adapter_id") {
            config.irAdapterId = text;
        } else if (key == "ir_backend_plugin") {
            config.irBackendPluginPath = resolveConfigPath(configDirectory, text);
        } else if (key == "ir_backend_id") {
            config.irBackendId = text;
        } else if (key == "steps") {
            config.stepLimit = parseUnsigned(text, label);
        } else if (key == "headless") {
            config.headless = parseBool(text, label);
        } else if (key == "cpu_mode") {
            config.cpuMode = lowerAscii(text);
        } else if (key == "cpu_detailed_timing") {
            config.cpuDetailedTiming = parseBool(text, label);
        } else {
            throw std::invalid_argument("Unknown config key: " + label);
        }
    } else if (section == "video") {
        if (key == "scale") {
            std::uint64_t parsed = parseUnsigned(text, label);
            constexpr std::uint64_t kMaxScale = 20;
            if (parsed > kMaxScale) {
                throw std::invalid_argument("Scale value too large (max 20): " + std::to_string(parsed));
            }
            config.windowScale = static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, parsed));
        } else if (key == "hd_scale") {
            std::uint64_t parsed = parseUnsigned(text, label);
            constexpr std::uint64_t kMaxHdScale = 8;
            config.hdScale = static_cast<std::uint32_t>(
                std::clamp<std::uint64_t>(parsed, 1u, kMaxHdScale));
        } else {
            throw std::invalid_argument("Unknown config key: " + label);
        }
    } else if (section == "timing") {
        if (key == "unthrottled") {
            config.unthrottled = parseBool(text, label);
        } else if (key == "speed") {
            config.speedMultiplier = parseDouble(text, label);
        } else if (key == "pause") {
            config.startPaused = parseBool(text, label);
        } else if (key == "profile") {
            config.timingProfile = lowerAscii(text);
        } else if (key == "diagnostics_report") {
            config.diagnosticsReportPath = resolveConfigPath(configDirectory, text);
        } else if (key == "diagnostics_interval_ms") {
            const auto parsed = parseUnsigned(text, label);
            const auto clamped = std::min(parsed, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
            config.diagnosticsIntervalMs = static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, clamped));
        } else {
            throw std::invalid_argument("Unknown config key: " + label);
        }
    } else if (section == "audio") {
        if (key == "enabled") {
            config.audioEnabled = parseBool(text, label);
        } else if (key == "backend") {
            config.audioBackend = text;
        } else if (key == "plugin") {
            config.audioPluginPath = resolveConfigPath(configDirectory, text);
        } else if (key == "processor_plugin") {
            config.audioProcessorPluginPaths.push_back(resolveConfigPath(configDirectory, text));
        } else if (key == "processor_config") {
            const auto equals = text.find('=');
            if (equals == std::string::npos || equals == 0u || equals + 1u >= text.size()) {
                throw std::invalid_argument(label + " requires <plugin-id>=<json-file>");
            }
            config.audioProcessorConfigs.push_back({
                text.substr(0u, equals),
                resolveConfigPath(configDirectory, text.substr(equals + 1u)),
            });
        } else if (key == "output_file") {
            config.audioOutputFilePath = resolveConfigPath(configDirectory, text);
        } else if (key == "midi_file") {
            config.midiOutputFilePath = resolveConfigPath(configDirectory, text);
        } else if (key == "midi_output") {
            config.midiOutputPort = text;
        } else if (key == "ready_queue_chunks") {
            const auto parsed = parseUnsigned(text, label);
            const auto clamped = std::min<std::uint64_t>(parsed, 64u);
            config.audioReadyQueueChunks =
                static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, clamped));
        } else if (key == "batch_chunks") {
            const auto parsed = parseUnsigned(text, label);
            const auto clamped = std::min<std::uint64_t>(parsed, 16u);
            config.audioBatchChunks =
                static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, clamped));
        } else {
            throw std::invalid_argument("Unknown config key: " + label);
        }
    } else if (section == "visual") {
        if (key == "pack" || key == "visual_pack" || key == "texture_pack") {
            config.visualPackPaths.push_back(resolveConfigPath(configDirectory, text));
        } else if (key == "capture" || key == "visual_capture" || key == "dump_resources") {
            config.visualCapturePath = resolveConfigPath(configDirectory, text);
        } else if (key == "reload") {
            config.visualPackReload = parseBool(text, label);
        } else {
            throw std::invalid_argument("Unknown config key: " + label);
        }
    } else if (section == "background") {
        if (key == "workers") {
            const auto parsed = std::min<std::uint64_t>(parseUnsigned(text, label), 256u);
            config.backgroundWorkers = static_cast<std::uint32_t>(parsed);
        } else if (key == "queue_capacity") {
            const auto parsed = std::clamp<std::uint64_t>(parseUnsigned(text, label), 1u, 65536u);
            config.backgroundQueueCapacity = static_cast<std::uint32_t>(parsed);
        } else if (key == "debug_snapshots") {
            config.debugSnapshotsEnabled = parseBool(text, label);
        } else {
            throw std::invalid_argument("Unknown config key: " + label);
        }
    } else {
        throw std::invalid_argument("Unknown config section: " + std::string(section));
    }
}

} // namespace

EmulatorConfig loadEmulatorConfig(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open config file: " + path.string());
    }

    EmulatorConfig config;
    const auto configDirectory = path.parent_path();
    std::string section;
    std::set<std::string> seenKeys;
    std::string line;
    std::uint64_t lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        const auto text = trim(line);
        if (text.empty() || text.front() == '#' || text.front() == ';') {
            continue;
        }

        if (text.front() == '[') {
            if (text.back() != ']') {
                throw std::invalid_argument("Malformed config line " + std::to_string(lineNumber) + ": " + text);
            }
            section = trim(std::string_view(text).substr(1, text.size() - 2));
            if (section != "emulator" && section != "video" && section != "timing" &&
                section != "audio" && section != "visual" && section != "background") {
                throw std::invalid_argument("Unknown config section: " + section);
            }
            continue;
        }

        const auto separator = text.find('=');
        if (separator == std::string::npos) {
            throw std::invalid_argument("Malformed config line " + std::to_string(lineNumber) + ": " + text);
        }
        if (section.empty()) {
            throw std::invalid_argument("Malformed config line " + std::to_string(lineNumber) + ": key outside section");
        }

        const auto key = trim(std::string_view(text).substr(0, separator));
        const auto value = trim(std::string_view(text).substr(separator + 1));
        if (key.empty()) {
            throw std::invalid_argument("Malformed config line " + std::to_string(lineNumber) + ": empty key");
        }

        const auto qualifiedKey = section + "." + key;
        const bool repeatableKey =
            (section == "visual" && (key == "pack" || key == "visual_pack" || key == "texture_pack")) ||
            (section == "audio" && key == "processor_plugin");
        if (!repeatableKey && !seenKeys.insert(qualifiedKey).second) {
            throw std::invalid_argument("Duplicate config key: " + qualifiedKey);
        }

        applyConfigValue(config, configDirectory, section, key, value);
    }

    return config;
}

void applyOverrides(EmulatorConfig& config, const CommandLineConfigOverrides& overrides)
{
    if (overrides.machineKind.has_value()) {
        config.machineKind = lowerAscii(*overrides.machineKind);
    }
    if (overrides.romPath.has_value()) {
        config.romPath = *overrides.romPath;
    }
    if (overrides.bootRomPath.has_value()) {
        config.bootRomPath = *overrides.bootRomPath;
    }
    if (overrides.pluginPath.has_value()) {
        config.pluginPath = *overrides.pluginPath;
    }
    if (overrides.frontendId.has_value()) {
        config.frontendId = *overrides.frontendId;
        config.headless = *overrides.frontendId == "headless";
    }
    if (overrides.executorPluginPath.has_value()) {
        config.executorPluginPath = *overrides.executorPluginPath;
    }
    if (overrides.executorPolicyId.has_value()) {
        config.executorPolicyId = *overrides.executorPolicyId;
    }
    if (overrides.irAdapterPluginPath.has_value()) {
        config.irAdapterPluginPath = *overrides.irAdapterPluginPath;
    }
    if (overrides.irAdapterId.has_value()) config.irAdapterId = *overrides.irAdapterId;
    if (overrides.irBackendPluginPath.has_value()) {
        config.irBackendPluginPath = *overrides.irBackendPluginPath;
    }
    if (overrides.irBackendId.has_value()) config.irBackendId = *overrides.irBackendId;
    if (overrides.stepLimit.has_value()) {
        config.stepLimit = *overrides.stepLimit;
    }
    if (overrides.windowScale.has_value()) {
        config.windowScale = *overrides.windowScale;
    }
    if (overrides.hdScale.has_value()) {
        config.hdScale = std::clamp(*overrides.hdScale, 1u, 8u);
    }
    if (overrides.headless.has_value()) {
        config.headless = *overrides.headless;
    }
    if (overrides.cpuMode.has_value()) {
        config.cpuMode = lowerAscii(*overrides.cpuMode);
    }
    if (overrides.cpuDetailedTiming.has_value()) {
        config.cpuDetailedTiming = *overrides.cpuDetailedTiming;
    }
    if (overrides.unthrottled.has_value()) {
        config.unthrottled = *overrides.unthrottled;
    }
    if (overrides.speedMultiplier.has_value()) {
        config.speedMultiplier = *overrides.speedMultiplier;
    }
    if (overrides.startPaused.has_value()) {
        config.startPaused = *overrides.startPaused;
    }
    if (overrides.timingProfile.has_value()) {
        config.timingProfile = lowerAscii(*overrides.timingProfile);
    }
    if (overrides.diagnosticsReportPath.has_value()) {
        config.diagnosticsReportPath = *overrides.diagnosticsReportPath;
    }
    if (overrides.diagnosticsIntervalMs.has_value()) {
        config.diagnosticsIntervalMs = *overrides.diagnosticsIntervalMs;
    }
    if (overrides.audioEnabled.has_value()) {
        config.audioEnabled = *overrides.audioEnabled;
    }
    if (overrides.audioBackend.has_value()) {
        config.audioBackend = *overrides.audioBackend;
    }
    if (overrides.audioPluginPath.has_value()) {
        config.audioPluginPath = *overrides.audioPluginPath;
    }
    if (overrides.audioOutputFilePath.has_value()) {
        config.audioOutputFilePath = *overrides.audioOutputFilePath;
    }
    if (overrides.midiOutputFilePath.has_value()) {
        config.midiOutputFilePath = *overrides.midiOutputFilePath;
    }
    if (overrides.midiOutputPort.has_value()) {
        config.midiOutputPort = *overrides.midiOutputPort;
    }
    if (overrides.audioReadyQueueChunks.has_value()) {
        config.audioReadyQueueChunks =
            static_cast<std::uint32_t>(std::clamp<std::uint32_t>(*overrides.audioReadyQueueChunks, 1u, 64u));
    }
    if (overrides.audioBatchChunks.has_value()) {
        config.audioBatchChunks =
            static_cast<std::uint32_t>(std::clamp<std::uint32_t>(*overrides.audioBatchChunks, 1u, 16u));
    }
    if (overrides.audioProcessorPluginPaths.has_value()) {
        config.audioProcessorPluginPaths = *overrides.audioProcessorPluginPaths;
    }
    if (overrides.audioProcessorConfigs.has_value()) {
        config.audioProcessorConfigs = *overrides.audioProcessorConfigs;
    }
    if (overrides.backgroundWorkers.has_value()) {
        config.backgroundWorkers = std::min<std::uint32_t>(*overrides.backgroundWorkers, 256u);
    }
    if (overrides.backgroundQueueCapacity.has_value()) {
        config.backgroundQueueCapacity =
            std::clamp<std::uint32_t>(*overrides.backgroundQueueCapacity, 1u, 65536u);
    }
    if (overrides.debugSnapshotsEnabled.has_value()) {
        config.debugSnapshotsEnabled = *overrides.debugSnapshotsEnabled;
    }
    if (overrides.visualPackPaths.has_value()) {
        config.visualPackPaths = *overrides.visualPackPaths;
    }
    if (overrides.visualCapturePath.has_value()) {
        config.visualCapturePath = *overrides.visualCapturePath;
    }
    if (overrides.visualPackReload.has_value()) {
        config.visualPackReload = *overrides.visualPackReload;
    }
}

void validateEmulatorConfig(const EmulatorConfig& config)
{
    if (!config.machineKind.has_value()) {
        throw std::invalid_argument("Missing core selection. Use --core <gameboy|gamegear>.");
    }
    const auto kind = parseMachineKind(*config.machineKind);
    auto instance = createMachine(kind);
    const auto& descriptor = instance.descriptor;

    if (config.irAdapterPluginPath.has_value() != config.irAdapterId.has_value()) {
        throw std::invalid_argument(
            "--ir-adapter-plugin and --ir-adapter-id must be specified together");
    }
    if (config.irBackendPluginPath.has_value() != config.irBackendId.has_value()) {
        throw std::invalid_argument(
            "--ir-backend-plugin and --ir-backend-id must be specified together");
    }
    if ((config.irAdapterPluginPath.has_value() || config.irBackendPluginPath.has_value()) &&
        config.cpuMode != "ir") {
        throw std::invalid_argument("dynamic IR components require --cpu-mode ir");
    }

    if (config.cpuMode != "baseline" && config.cpuMode != "block" &&
        config.cpuMode != "ir" && config.cpuMode != "native") {
        throw std::invalid_argument("Unknown CPU mode: " + config.cpuMode +
                                    ". Use baseline, block, ir, or native.");
    }
    std::unique_ptr<Plugin::IExecutorPolicyPlugin> policy;
    if (config.executorPluginPath.has_value()) {
        const auto module = Plugin::DynamicPluginModule::load(*config.executorPluginPath);
        const auto ids = module.executorPolicyIds();
        const auto selectedId = config.executorPolicyId.has_value()
            ? *config.executorPolicyId
            : (ids.size() == 1u ? ids.front() : std::string{});
        if (selectedId.empty()) {
            throw std::invalid_argument("--executor-policy is required when a module exposes multiple policies");
        }
        policy = module.createExecutorPolicy(selectedId);
    } else {
        const auto policyId = config.executorPolicyId.has_value()
            ? std::string_view(*config.executorPolicyId)
            : Plugin::executorPolicyIdForLegacyMode(config.cpuMode);
        policy = Plugin::ExecutorPolicyRegistry::builtins().create(policyId);
    }
    try {
        // Validate the complete machine installation contract, including
        // platform-specific backend availability and clone ownership.
        instance.machine->attachExecutorPolicy(*policy);
    } catch (const std::runtime_error& ex) {
        const auto selection = config.executorPolicyId.value_or(config.cpuMode);
        throw std::invalid_argument("Executor selection '" + selection +
                                    "' is unsupported by core '" + *config.machineKind +
                                    "': " + ex.what());
    }
    if (config.cpuDetailedTiming && policy->backend() != ExecutionBackend::PortableIr &&
        policy->backend() != ExecutionBackend::NativeExperimental) {
        throw std::invalid_argument(
            "Detailed CPU timing requires a portable-IR or native-experimental executor policy");
    }

    if (config.romPath.empty()) {
        throw std::invalid_argument("Missing ROM path. Use --rom <file.gb>.");
    }
    if (config.bootRomPath.has_value() &&
        dynamic_cast<IExternalBootRomMachine*>(instance.machine.get()) == nullptr) {
        throw std::invalid_argument(
            std::string(descriptor.displayName) + " does not support external boot ROM loading");
    }
    if ((!config.visualPackPaths.empty() || config.visualPackReload) &&
        !instance.machine->supportsVisualPacks()) {
        throw std::invalid_argument(
            std::string(descriptor.displayName) + " does not support visual packs");
    }
    if (config.visualCapturePath.has_value() &&
        !instance.machine->supportsVisualCapture()) {
        throw std::invalid_argument(
            std::string(descriptor.displayName) + " does not support visual capture");
    }
}

ParsedEmulatorArguments parseEmulatorArguments(int argc, char** argv)
{
    ParsedEmulatorArguments arguments;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--config requires a path");
            }
            if (arguments.configPath.has_value()) {
                throw std::invalid_argument("--config was provided more than once");
            }
            arguments.configPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--core" || arg == "--machine") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--core requires a value");
            }
            arguments.overrides.machineKind = lowerAscii(argv[++i]);
        } else if (arg == "--rom") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--rom requires a path");
            }
            assignRomPath(arguments.overrides, argv[++i]);
        } else if (arg == "--boot-rom") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--boot-rom requires a path");
            }
            arguments.overrides.bootRomPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--plugin" || arg == "--frontend-plugin") {
            if (i + 1 >= argc) {
                throw std::invalid_argument(arg + " requires a path");
            }
            arguments.overrides.pluginPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--frontend") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--frontend requires an id");
            }
            arguments.overrides.frontendId = std::string(argv[++i]);
        } else if (arg == "--executor-plugin") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--executor-plugin requires a path");
            }
            arguments.overrides.executorPluginPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--executor-policy") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--executor-policy requires an id");
            }
            arguments.overrides.executorPolicyId = std::string(argv[++i]);
        } else if (arg == "--ir-adapter-plugin") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--ir-adapter-plugin requires a path");
            }
            arguments.overrides.irAdapterPluginPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--ir-adapter-id") {
            if (i + 1 >= argc) throw std::invalid_argument("--ir-adapter-id requires an id");
            arguments.overrides.irAdapterId = std::string(argv[++i]);
        } else if (arg == "--ir-backend-plugin") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--ir-backend-plugin requires a path");
            }
            arguments.overrides.irBackendPluginPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--ir-backend-id") {
            if (i + 1 >= argc) throw std::invalid_argument("--ir-backend-id requires an id");
            arguments.overrides.irBackendId = std::string(argv[++i]);
        } else if (arg == "--steps") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--steps requires a count");
            }
            arguments.overrides.stepLimit = parseUnsigned(argv[++i], "--steps");
        } else if (arg == "--scale") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--scale requires a positive integer");
            }
            {
                std::uint64_t parsed = parseUnsigned(argv[++i], "--scale");
                // Clamp to uint32_t max to avoid overflow
                parsed = std::min(parsed, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
                arguments.overrides.windowScale = static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, parsed));
            }
        } else if (arg == "--hd-scale") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--hd-scale requires a positive integer");
            }
            {
                std::uint64_t parsed = parseUnsigned(argv[++i], "--hd-scale");
                parsed = std::min(parsed, static_cast<std::uint64_t>(8u));
                arguments.overrides.hdScale = static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, parsed));
            }
        } else if (arg == "--unthrottled") {
            arguments.overrides.unthrottled = true;
        } else if (arg == "--cpu-mode") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--cpu-mode requires baseline, block, ir, or native");
            }
            arguments.overrides.cpuMode = lowerAscii(argv[++i]);
        } else if (arg == "--cpu-detailed-timing") {
            arguments.overrides.cpuDetailedTiming = true;
        } else if (arg == "--speed") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--speed requires a numeric multiplier");
            }
            arguments.overrides.speedMultiplier = parseDouble(argv[++i], "--speed");
        } else if (arg == "--pause") {
            arguments.overrides.startPaused = true;
        } else if (arg == "--timing-profile") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--timing-profile requires a value");
            }
            arguments.overrides.timingProfile = lowerAscii(argv[++i]);
        } else if (arg == "--diagnostics-report") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--diagnostics-report requires a path");
            }
            arguments.overrides.diagnosticsReportPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--diagnostics-interval-ms") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--diagnostics-interval-ms requires a positive integer");
            }
            auto parsed = parseUnsigned(argv[++i], "--diagnostics-interval-ms");
            parsed = std::min(parsed, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
            arguments.overrides.diagnosticsIntervalMs =
                static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, parsed));
        } else if (arg == "--no-audio") {
            arguments.overrides.audioEnabled = false;
        } else if (arg == "--audio-backend") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--audio-backend requires a backend name");
            }
            arguments.overrides.audioBackend = argv[++i];
        } else if (arg == "--audio-plugin") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--audio-plugin requires a path");
            }
            arguments.overrides.audioPluginPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--audio-processor-plugin") {
            if (i + 1 >= argc) throw std::invalid_argument("--audio-processor-plugin requires a path");
            if (!arguments.overrides.audioProcessorPluginPaths.has_value()) {
                arguments.overrides.audioProcessorPluginPaths = std::vector<std::filesystem::path>{};
            }
            arguments.overrides.audioProcessorPluginPaths->emplace_back(argv[++i]);
        } else if (arg == "--audio-processor-config") {
            if (i + 1 >= argc) throw std::invalid_argument("--audio-processor-config requires <plugin-id>=<json-file>");
            const std::string spec = argv[++i];
            const auto equals = spec.find('=');
            if (equals == std::string::npos || equals == 0u || equals + 1u >= spec.size()) {
                throw std::invalid_argument("--audio-processor-config requires <plugin-id>=<json-file>");
            }
            if (!arguments.overrides.audioProcessorConfigs.has_value()) {
                arguments.overrides.audioProcessorConfigs = std::vector<AudioProcessorConfigSpec>{};
            }
            arguments.overrides.audioProcessorConfigs->push_back({spec.substr(0u, equals), spec.substr(equals + 1u)});
        } else if (arg == "--audio-file") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--audio-file requires a path");
            }
            arguments.overrides.audioOutputFilePath = std::filesystem::path(argv[++i]);
        } else if (arg == "--midi-file") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--midi-file requires a path");
            }
            arguments.overrides.midiOutputFilePath = std::filesystem::path(argv[++i]);
        } else if (arg == "--midi-output") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--midi-output requires an ALSA client:port or subscribers");
            }
            arguments.overrides.midiOutputPort = argv[++i];
        } else if (arg == "--audio-ready-queue-chunks") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--audio-ready-queue-chunks requires a positive integer");
            }
            auto parsed = parseUnsigned(argv[++i], "--audio-ready-queue-chunks");
            parsed = std::min(parsed, static_cast<std::uint64_t>(64u));
            arguments.overrides.audioReadyQueueChunks =
                static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, parsed));
        } else if (arg == "--audio-batch-chunks") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--audio-batch-chunks requires a positive integer");
            }
            auto parsed = parseUnsigned(argv[++i], "--audio-batch-chunks");
            parsed = std::min(parsed, static_cast<std::uint64_t>(16u));
            arguments.overrides.audioBatchChunks =
                static_cast<std::uint32_t>(std::max<std::uint64_t>(1u, parsed));
        } else if (arg == "--background-workers") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--background-workers requires a non-negative integer");
            }
            const auto parsed = std::min<std::uint64_t>(parseUnsigned(argv[++i], "--background-workers"), 256u);
            arguments.overrides.backgroundWorkers = static_cast<std::uint32_t>(parsed);
        } else if (arg == "--background-queue-capacity") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--background-queue-capacity requires a positive integer");
            }
            const auto parsed = std::clamp<std::uint64_t>(
                parseUnsigned(argv[++i], "--background-queue-capacity"), 1u, 65536u);
            arguments.overrides.backgroundQueueCapacity = static_cast<std::uint32_t>(parsed);
        } else if (arg == "--debug-snapshots") {
            arguments.overrides.debugSnapshotsEnabled = true;
        } else if (arg == "--visual-pack" || arg == "--texture-pack") {
            if (i + 1 >= argc) {
                throw std::invalid_argument(arg + " requires a path");
            }
            if (!arguments.overrides.visualPackPaths.has_value()) {
                arguments.overrides.visualPackPaths = std::vector<std::filesystem::path>{};
            }
            arguments.overrides.visualPackPaths->push_back(std::filesystem::path(argv[++i]));
        } else if (arg == "--visual-capture" || arg == "--dump-visual-resources") {
            if (i + 1 >= argc) {
                throw std::invalid_argument(arg + " requires a directory");
            }
            arguments.overrides.visualCapturePath = std::filesystem::path(argv[++i]);
        } else if (arg == "--visual-pack-reload") {
            arguments.overrides.visualPackReload = true;
        } else if (arg == "--headless") {
            arguments.overrides.headless = true;
        } else if (arg == "-h" || arg == "--help") {
            arguments.helpRequested = true;
            return arguments;
        } else if (!arg.empty() && arg.front() == '-') {
            throw std::invalid_argument("Unknown option: " + arg);
        } else {
            assignRomPath(arguments.overrides, argv[i]);
        }
    }

    return arguments;
}

EmulatorConfig resolveEmulatorConfig(const ParsedEmulatorArguments& arguments)
{
    auto config = arguments.configPath.has_value()
        ? loadEmulatorConfig(*arguments.configPath)
        : EmulatorConfig{};
    applyOverrides(config, arguments.overrides);
    validateEmulatorConfig(config);
    return config;
}

} // namespace BMMQ
