#include "emulator/EmulatorConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
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
        if (key == "rom") {
            config.romPath = resolveConfigPath(configDirectory, text);
        } else if (key == "boot_rom") {
            config.bootRomPath = resolveConfigPath(configDirectory, text);
        } else if (key == "plugin") {
            config.pluginPath = resolveConfigPath(configDirectory, text);
        } else if (key == "steps") {
            config.stepLimit = parseUnsigned(text, label);
        } else if (key == "headless") {
            config.headless = parseBool(text, label);
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
        } else {
            throw std::invalid_argument("Unknown config key: " + label);
        }
    } else if (section == "audio") {
        if (key == "enabled") {
            config.audioEnabled = parseBool(text, label);
        } else if (key == "backend") {
            config.audioBackend = text;
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
                section != "audio" && section != "visual") {
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
            section == "visual" && (key == "pack" || key == "visual_pack" || key == "texture_pack");
        if (!repeatableKey && !seenKeys.insert(qualifiedKey).second) {
            throw std::invalid_argument("Duplicate config key: " + qualifiedKey);
        }

        applyConfigValue(config, configDirectory, section, key, value);
    }

    return config;
}

void applyOverrides(EmulatorConfig& config, const CommandLineConfigOverrides& overrides)
{
    if (overrides.romPath.has_value()) {
        config.romPath = *overrides.romPath;
    }
    if (overrides.bootRomPath.has_value()) {
        config.bootRomPath = *overrides.bootRomPath;
    }
    if (overrides.pluginPath.has_value()) {
        config.pluginPath = *overrides.pluginPath;
    }
    if (overrides.stepLimit.has_value()) {
        config.stepLimit = *overrides.stepLimit;
    }
    if (overrides.windowScale.has_value()) {
        config.windowScale = *overrides.windowScale;
    }
    if (overrides.headless.has_value()) {
        config.headless = *overrides.headless;
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
    if (overrides.audioEnabled.has_value()) {
        config.audioEnabled = *overrides.audioEnabled;
    }
    if (overrides.audioBackend.has_value()) {
        config.audioBackend = *overrides.audioBackend;
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
    if (config.romPath.empty()) {
        throw std::invalid_argument("Missing ROM path. Use --rom <file.gb>.");
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
        } else if (arg == "--plugin") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--plugin requires a path");
            }
            arguments.overrides.pluginPath = std::filesystem::path(argv[++i]);
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
        } else if (arg == "--unthrottled") {
            arguments.overrides.unthrottled = true;
        } else if (arg == "--speed") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--speed requires a numeric multiplier");
            }
            arguments.overrides.speedMultiplier = parseDouble(argv[++i], "--speed");
        } else if (arg == "--pause") {
            arguments.overrides.startPaused = true;
        } else if (arg == "--no-audio") {
            arguments.overrides.audioEnabled = false;
        } else if (arg == "--audio-backend") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--audio-backend requires a backend name");
            }
            arguments.overrides.audioBackend = argv[++i];
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
