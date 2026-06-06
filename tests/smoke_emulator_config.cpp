#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "emulator/EmulatorConfig.hpp"

#define CHECK_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "check failed: " << #expr << '\n'; \
            return 1; \
        } \
    } while (false)

namespace {

std::filesystem::path makeTempDir()
{
    auto dir = std::filesystem::temp_directory_path() / "proto_time_emulator_config_smoke";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void writeTextFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("unable to create test file: " + path.string());
    }
    output << text;
}

bool throwsInvalidArgumentContaining(std::string_view expected, auto&& fn)
{
    try {
        fn();
    } catch (const std::invalid_argument& ex) {
        return std::string_view(ex.what()).find(expected) != std::string_view::npos;
    }
    return false;
}

BMMQ::ParsedEmulatorArguments parseArgs(std::initializer_list<const char*> args)
{
    auto storage = std::vector<char*>{};
    storage.reserve(args.size());
    for (const char* arg : args) {
        // Safe const_cast: BMMQ::parseEmulatorArguments only reads argv strings and does not modify them.
        // If parseEmulatorArguments ever writes to argv, this would be undefined behavior—future maintainers: verify this assumption before changing usage here.
        storage.push_back(const_cast<char*>(arg));
    }
    return BMMQ::parseEmulatorArguments(
        static_cast<int>(storage.size()),
        storage.data());
}

} // namespace

int main()
{
    const auto tempDir = makeTempDir();
    const auto configPath = tempDir / "proto-time.ini";

    {
        BMMQ::EmulatorConfig defaults;
        CHECK_TRUE(!defaults.machineKind.has_value());
        CHECK_TRUE(defaults.romPath.empty());
        CHECK_TRUE(!defaults.bootRomPath.has_value());
        CHECK_TRUE(!defaults.pluginPath.has_value());
        CHECK_TRUE(!defaults.stepLimit.has_value());
        CHECK_TRUE(defaults.windowScale == 3u);
        CHECK_TRUE(!defaults.headless);
        CHECK_TRUE(!defaults.unthrottled);
        CHECK_TRUE(std::abs(defaults.speedMultiplier - 1.0) < 0.000001);
        CHECK_TRUE(!defaults.startPaused);
        CHECK_TRUE(!defaults.diagnosticsReportPath.has_value());
        CHECK_TRUE(defaults.diagnosticsIntervalMs == 1000u);
        CHECK_TRUE(defaults.audioEnabled);
        CHECK_TRUE(defaults.audioBackend == "sdl");
        CHECK_TRUE(defaults.audioReadyQueueChunks == 3u);
        CHECK_TRUE(defaults.audioBatchChunks == 1u);
        CHECK_TRUE(defaults.visualPackPaths.empty());
        CHECK_TRUE(!defaults.visualCapturePath.has_value());
        CHECK_TRUE(!defaults.visualPackReload);
    }

    writeTextFile(configPath,
        "# comment\n"
        "[emulator]\n"
        "core = gameboy\n"
        "rom = roms/game.gb\n"
        "boot_rom = boot/dmg.bin\n"
        "plugin = plugins/frontend.so\n"
        "steps = 1000000\n"
        "headless = yes\n"
        "\n"
        "[video]\n"
        "scale = 5\n"
        "\n"
        "[timing]\n"
        "unthrottled = on\n"
        "speed = 2.5\n"
        "pause = 1\n"
        "profile = low_latency\n"
        "\n"
        "[audio]\n"
        "enabled = false\n"
        "backend = file\n"
        "ready_queue_chunks = 8\n"
        "batch_chunks = 4\n"
        "\n"
        "[visual]\n"
        "pack = packs/base.json\n"
        "texture_pack = packs/compat.json\n"
        "capture = capture/gameboy\n"
        "reload = true\n");

    auto fileConfig = BMMQ::loadEmulatorConfig(configPath);
    CHECK_TRUE(fileConfig.machineKind.has_value());
    CHECK_TRUE(*fileConfig.machineKind == "gameboy");
    CHECK_TRUE(fileConfig.romPath == tempDir / "roms/game.gb");
    CHECK_TRUE(fileConfig.bootRomPath == tempDir / "boot/dmg.bin");
    CHECK_TRUE(fileConfig.pluginPath == tempDir / "plugins/frontend.so");
    CHECK_TRUE(fileConfig.stepLimit == 1000000u);
    CHECK_TRUE(fileConfig.windowScale == 5u);
    CHECK_TRUE(fileConfig.headless);
    CHECK_TRUE(fileConfig.unthrottled);
    CHECK_TRUE(std::abs(fileConfig.speedMultiplier - 2.5) < 0.000001);
    CHECK_TRUE(fileConfig.startPaused);
    CHECK_TRUE(fileConfig.timingProfile.has_value());
    CHECK_TRUE(*fileConfig.timingProfile == "low_latency");
    CHECK_TRUE(!fileConfig.diagnosticsReportPath.has_value());
    CHECK_TRUE(fileConfig.diagnosticsIntervalMs == 1000u);
    CHECK_TRUE(!fileConfig.audioEnabled);
    CHECK_TRUE(fileConfig.audioBackend == "file");
    CHECK_TRUE(fileConfig.audioReadyQueueChunks == 8u);
    CHECK_TRUE(fileConfig.audioBatchChunks == 4u);
    CHECK_TRUE(fileConfig.visualPackPaths.size() == 2u);
    CHECK_TRUE(fileConfig.visualPackPaths[0] == tempDir / "packs/base.json");
    CHECK_TRUE(fileConfig.visualPackPaths[1] == tempDir / "packs/compat.json");
    CHECK_TRUE(fileConfig.visualCapturePath == tempDir / "capture/gameboy");
    CHECK_TRUE(fileConfig.visualPackReload);

    BMMQ::CommandLineConfigOverrides overrides;
    overrides.machineKind = std::string("gamegear");
    overrides.romPath = std::filesystem::path("cli.gb");
    overrides.bootRomPath = std::filesystem::path("cli-boot.bin");
    overrides.pluginPath = std::filesystem::path("cli-plugin.so");
    overrides.stepLimit = 42u;
    overrides.windowScale = 1u;
    overrides.headless = false;
    overrides.unthrottled = false;
    overrides.speedMultiplier = 0.5;
    overrides.startPaused = false;
    overrides.timingProfile = std::string("power_saver");
    overrides.diagnosticsReportPath = std::filesystem::path("diag-report.jsonl");
    overrides.diagnosticsIntervalMs = 250u;
    overrides.audioEnabled = true;
    overrides.audioBackend = "dummy";
    overrides.audioReadyQueueChunks = 6u;
    overrides.audioBatchChunks = 3u;
    overrides.visualPackPaths = std::vector<std::filesystem::path>{
        std::filesystem::path("cli-pack-a.json"),
        std::filesystem::path("cli-pack-b.json"),
    };
    overrides.visualCapturePath = std::filesystem::path("cli-capture");
    overrides.visualPackReload = false;
    BMMQ::applyOverrides(fileConfig, overrides);
    CHECK_TRUE(fileConfig.machineKind.has_value());
    CHECK_TRUE(*fileConfig.machineKind == "gamegear");
    CHECK_TRUE(fileConfig.romPath == "cli.gb");
    CHECK_TRUE(fileConfig.bootRomPath == "cli-boot.bin");
    CHECK_TRUE(fileConfig.pluginPath == "cli-plugin.so");
    CHECK_TRUE(fileConfig.stepLimit == 42u);
    CHECK_TRUE(fileConfig.windowScale == 1u);
    CHECK_TRUE(!fileConfig.headless);
    CHECK_TRUE(!fileConfig.unthrottled);
    CHECK_TRUE(std::abs(fileConfig.speedMultiplier - 0.5) < 0.000001);
    CHECK_TRUE(!fileConfig.startPaused);
    CHECK_TRUE(fileConfig.timingProfile.has_value());
    CHECK_TRUE(*fileConfig.timingProfile == "power_saver");
    CHECK_TRUE(fileConfig.diagnosticsReportPath == "diag-report.jsonl");
    CHECK_TRUE(fileConfig.diagnosticsIntervalMs == 250u);
    CHECK_TRUE(fileConfig.audioEnabled);
    CHECK_TRUE(fileConfig.audioBackend == "dummy");
    CHECK_TRUE(fileConfig.audioReadyQueueChunks == 6u);
    CHECK_TRUE(fileConfig.audioBatchChunks == 3u);
    CHECK_TRUE(fileConfig.visualPackPaths.size() == 2u);
    CHECK_TRUE(fileConfig.visualPackPaths[0] == "cli-pack-a.json");
    CHECK_TRUE(fileConfig.visualPackPaths[1] == "cli-pack-b.json");
    CHECK_TRUE(fileConfig.visualCapturePath == "cli-capture");
    CHECK_TRUE(!fileConfig.visualPackReload);

    CHECK_TRUE(throwsInvalidArgumentContaining("ROM path was provided more than once", [] {
        (void)parseArgs({"timeEmulator", "--rom", "a.gb", "b.gb"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Missing core selection", [] {
        BMMQ::EmulatorConfig config;
        config.romPath = "missing-core.gb";
        BMMQ::validateEmulatorConfig(config);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Missing ROM path", [] {
        BMMQ::EmulatorConfig config;
        config.machineKind = std::string("gameboy");
        BMMQ::validateEmulatorConfig(config);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Unknown machine core", [] {
        BMMQ::EmulatorConfig config;
        config.machineKind = std::string("genesis");
        config.romPath = "bad-core.bin";
        BMMQ::validateEmulatorConfig(config);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Unknown config section", [&] {
        const auto path = tempDir / "unknown-section.ini";
        writeTextFile(path, "[network]\nenabled=true\n");
        (void)BMMQ::loadEmulatorConfig(path);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Unknown config key", [&] {
        const auto path = tempDir / "unknown-key.ini";
        writeTextFile(path, "[emulator]\nunknown=true\n");
        (void)BMMQ::loadEmulatorConfig(path);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Duplicate config key", [&] {
        const auto path = tempDir / "duplicate.ini";
        writeTextFile(path, "[audio]\nbackend=sdl\nbackend=dummy\n");
        (void)BMMQ::loadEmulatorConfig(path);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Malformed config line", [&] {
        const auto path = tempDir / "malformed.ini";
        writeTextFile(path, "[audio]\nbackend dummy\n");
        (void)BMMQ::loadEmulatorConfig(path);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Invalid boolean value", [&] {
        const auto path = tempDir / "bad-bool.ini";
        writeTextFile(path, "[audio]\nenabled=maybe\n");
        (void)BMMQ::loadEmulatorConfig(path);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Invalid unsigned value", [&] {
        const auto path = tempDir / "bad-uint.ini";
        writeTextFile(path, "[emulator]\nsteps=12x\n");
        (void)BMMQ::loadEmulatorConfig(path);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Invalid numeric value", [&] {
        const auto path = tempDir / "bad-speed.ini";
        writeTextFile(path, "[timing]\nspeed=fast\n");
        (void)BMMQ::loadEmulatorConfig(path);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("--timing-profile requires a value", [] {
        (void)parseArgs({"timeEmulator", "--timing-profile"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("--diagnostics-report requires a path", [] {
        (void)parseArgs({"timeEmulator", "--diagnostics-report"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("--diagnostics-interval-ms requires a positive integer", [] {
        (void)parseArgs({"timeEmulator", "--diagnostics-interval-ms"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("--audio-ready-queue-chunks requires a positive integer", [] {
        (void)parseArgs({"timeEmulator", "--audio-ready-queue-chunks"});
    }));
    CHECK_TRUE(throwsInvalidArgumentContaining("--audio-batch-chunks requires a positive integer", [] {
        (void)parseArgs({"timeEmulator", "--audio-batch-chunks"});
    }));

    const auto help = parseArgs({"timeEmulator", "--help", "--unknown-after-help"});
    CHECK_TRUE(help.helpRequested);

    CHECK_TRUE(throwsInvalidArgumentContaining("--texture-pack requires a path", [] {
        (void)parseArgs({"timeEmulator", "--texture-pack"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("--visual-pack requires a path", [] {
        (void)parseArgs({"timeEmulator", "--visual-pack"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("--dump-visual-resources requires a directory", [] {
        (void)parseArgs({"timeEmulator", "--dump-visual-resources"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("--visual-capture requires a directory", [] {
        (void)parseArgs({"timeEmulator", "--visual-capture"});
    }));

    const auto resolvedConfigPath = tempDir / "resolved.ini";
    writeTextFile(resolvedConfigPath,
        "[emulator]\ncore=gameboy\nrom=config.gb\nheadless=true\n"
        "[audio]\nbackend=file\n"
        "[visual]\npack=config-pack.json\ncapture=config-capture\nreload=false\n");
    auto parsed = parseArgs({"timeEmulator",
                             "--config", resolvedConfigPath.c_str(),
                             "--core", "gameboy",
                             "--rom", "cli.gb",
                             "--diagnostics-report", "runtime-diag.jsonl",
                             "--diagnostics-interval-ms", "125",
                             "--audio-backend", "dummy",
                             "--audio-ready-queue-chunks", "12",
                             "--audio-batch-chunks", "5",
                             "--visual-pack", "cli-pack-a.json",
                             "--texture-pack", "cli-pack-b.json",
                             "--visual-capture", "cli-capture",
                             "--visual-pack-reload"});
    auto resolved = BMMQ::resolveEmulatorConfig(parsed);
    CHECK_TRUE(resolved.machineKind.has_value());
    CHECK_TRUE(*resolved.machineKind == "gameboy");
    CHECK_TRUE(resolved.romPath == "cli.gb");
    CHECK_TRUE(resolved.headless);
    CHECK_TRUE(resolved.diagnosticsReportPath == "runtime-diag.jsonl");
    CHECK_TRUE(resolved.diagnosticsIntervalMs == 125u);
    CHECK_TRUE(resolved.audioBackend == "dummy");
    CHECK_TRUE(resolved.audioReadyQueueChunks == 12u);
    CHECK_TRUE(resolved.audioBatchChunks == 5u);
    CHECK_TRUE(resolved.visualPackPaths.size() == 2u);
    CHECK_TRUE(resolved.visualPackPaths[0] == "cli-pack-a.json");
    CHECK_TRUE(resolved.visualPackPaths[1] == "cli-pack-b.json");
    CHECK_TRUE(resolved.visualCapturePath == "cli-capture");
    CHECK_TRUE(resolved.visualPackReload);

    CHECK_TRUE(throwsInvalidArgumentContaining("does not support visual packs", [&] {
        auto invalid = parseArgs({"timeEmulator",
                                  "--core", "gamegear",
                                  "--rom", "cli.gg",
                                  "--visual-pack", "gg-pack.json"});
        (void)BMMQ::resolveEmulatorConfig(invalid);
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("does not support visual capture", [&] {
        auto invalid = parseArgs({"timeEmulator",
                                  "--core", "gamegear",
                                  "--rom", "cli.gg",
                                  "--visual-capture", "gg-capture"});
        (void)BMMQ::resolveEmulatorConfig(invalid);
    }));

    {
        auto gameGearBootRom = parseArgs({"timeEmulator",
                                          "--core", "gamegear",
                                          "--rom", "cli.gg",
                                          "--boot-rom", "gg-boot.bin"});
        const auto gameGearBootRomConfig = BMMQ::resolveEmulatorConfig(gameGearBootRom);
        CHECK_TRUE(gameGearBootRomConfig.bootRomPath == "gg-boot.bin");
    }

    const auto explicitGameBoy = parseArgs({"timeEmulator", "--core", "gameboy", "--rom", "gb.gb"});
    CHECK_TRUE(explicitGameBoy.overrides.machineKind.has_value());
    CHECK_TRUE(*explicitGameBoy.overrides.machineKind == "gameboy");

    const auto explicitGameGear = parseArgs({"timeEmulator", "--core", "gamegear", "--rom", "gg.gg"});
    CHECK_TRUE(explicitGameGear.overrides.machineKind.has_value());
    CHECK_TRUE(*explicitGameGear.overrides.machineKind == "gamegear");

    CHECK_TRUE(throwsInvalidArgumentContaining("--core requires a value", [] {
        (void)parseArgs({"timeEmulator", "--core"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Unknown machine core", [] {
        auto parsedUnknown = parseArgs({"timeEmulator", "--core", "mystery", "--rom", "bad.bin"});
        (void)BMMQ::resolveEmulatorConfig(parsedUnknown);
    }));

    std::filesystem::remove_all(tempDir);
    return 0;
}

#undef CHECK_TRUE
