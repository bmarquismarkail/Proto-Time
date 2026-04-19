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
        CHECK_TRUE(defaults.romPath.empty());
        CHECK_TRUE(!defaults.bootRomPath.has_value());
        CHECK_TRUE(!defaults.pluginPath.has_value());
        CHECK_TRUE(!defaults.stepLimit.has_value());
        CHECK_TRUE(defaults.windowScale == 3u);
        CHECK_TRUE(!defaults.headless);
        CHECK_TRUE(!defaults.unthrottled);
        CHECK_TRUE(std::abs(defaults.speedMultiplier - 1.0) < 0.000001);
        CHECK_TRUE(!defaults.startPaused);
        CHECK_TRUE(defaults.audioEnabled);
        CHECK_TRUE(defaults.audioBackend == "sdl");
        CHECK_TRUE(!defaults.texturePackPath.has_value());
        CHECK_TRUE(!defaults.visualDumpPath.has_value());
    }

    writeTextFile(configPath,
        "# comment\n"
        "[emulator]\n"
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
        "\n"
        "[audio]\n"
        "enabled = false\n"
        "backend = file\n"
        "\n"
        "[visual]\n"
        "texture_pack = packs/pack.json\n"
        "dump_resources = capture/gameboy\n");

    auto fileConfig = BMMQ::loadEmulatorConfig(configPath);
    CHECK_TRUE(fileConfig.romPath == tempDir / "roms/game.gb");
    CHECK_TRUE(fileConfig.bootRomPath == tempDir / "boot/dmg.bin");
    CHECK_TRUE(fileConfig.pluginPath == tempDir / "plugins/frontend.so");
    CHECK_TRUE(fileConfig.stepLimit == 1000000u);
    CHECK_TRUE(fileConfig.windowScale == 5u);
    CHECK_TRUE(fileConfig.headless);
    CHECK_TRUE(fileConfig.unthrottled);
    CHECK_TRUE(std::abs(fileConfig.speedMultiplier - 2.5) < 0.000001);
    CHECK_TRUE(fileConfig.startPaused);
    CHECK_TRUE(!fileConfig.audioEnabled);
    CHECK_TRUE(fileConfig.audioBackend == "file");
    CHECK_TRUE(fileConfig.texturePackPath == tempDir / "packs/pack.json");
    CHECK_TRUE(fileConfig.visualDumpPath == tempDir / "capture/gameboy");

    BMMQ::CommandLineConfigOverrides overrides;
    overrides.romPath = std::filesystem::path("cli.gb");
    overrides.bootRomPath = std::filesystem::path("cli-boot.bin");
    overrides.pluginPath = std::filesystem::path("cli-plugin.so");
    overrides.stepLimit = 42u;
    overrides.windowScale = 1u;
    overrides.headless = false;
    overrides.unthrottled = false;
    overrides.speedMultiplier = 0.5;
    overrides.startPaused = false;
    overrides.audioEnabled = true;
    overrides.audioBackend = "dummy";
    overrides.texturePackPath = std::filesystem::path("cli-pack.json");
    overrides.visualDumpPath = std::filesystem::path("cli-capture");
    BMMQ::applyOverrides(fileConfig, overrides);
    CHECK_TRUE(fileConfig.romPath == "cli.gb");
    CHECK_TRUE(fileConfig.bootRomPath == "cli-boot.bin");
    CHECK_TRUE(fileConfig.pluginPath == "cli-plugin.so");
    CHECK_TRUE(fileConfig.stepLimit == 42u);
    CHECK_TRUE(fileConfig.windowScale == 1u);
    CHECK_TRUE(!fileConfig.headless);
    CHECK_TRUE(!fileConfig.unthrottled);
    CHECK_TRUE(std::abs(fileConfig.speedMultiplier - 0.5) < 0.000001);
    CHECK_TRUE(!fileConfig.startPaused);
    CHECK_TRUE(fileConfig.audioEnabled);
    CHECK_TRUE(fileConfig.audioBackend == "dummy");
    CHECK_TRUE(fileConfig.texturePackPath == "cli-pack.json");
    CHECK_TRUE(fileConfig.visualDumpPath == "cli-capture");

    CHECK_TRUE(throwsInvalidArgumentContaining("ROM path was provided more than once", [] {
        (void)parseArgs({"timeEmulator", "--rom", "a.gb", "b.gb"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("Missing ROM path", [] {
        BMMQ::validateEmulatorConfig(BMMQ::EmulatorConfig{});
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

    const auto help = parseArgs({"timeEmulator", "--help", "--unknown-after-help"});
    CHECK_TRUE(help.helpRequested);

    CHECK_TRUE(throwsInvalidArgumentContaining("--texture-pack requires a path", [] {
        (void)parseArgs({"timeEmulator", "--texture-pack"});
    }));

    CHECK_TRUE(throwsInvalidArgumentContaining("--dump-visual-resources requires a directory", [] {
        (void)parseArgs({"timeEmulator", "--dump-visual-resources"});
    }));

    const auto resolvedConfigPath = tempDir / "resolved.ini";
    writeTextFile(resolvedConfigPath,
        "[emulator]\nrom=config.gb\nheadless=true\n"
        "[audio]\nbackend=file\n"
        "[visual]\ntexture_pack=config-pack.json\ndump_resources=config-capture\n");
    auto parsed = parseArgs({"timeEmulator",
                             "--config", resolvedConfigPath.c_str(),
                             "--rom", "cli.gb",
                             "--audio-backend", "dummy",
                             "--texture-pack", "cli-pack.json",
                             "--dump-visual-resources", "cli-capture"});
    auto resolved = BMMQ::resolveEmulatorConfig(parsed);
    CHECK_TRUE(resolved.romPath == "cli.gb");
    CHECK_TRUE(resolved.headless);
    CHECK_TRUE(resolved.audioBackend == "dummy");
    CHECK_TRUE(resolved.texturePackPath == "cli-pack.json");
    CHECK_TRUE(resolved.visualDumpPath == "cli-capture");

    std::filesystem::remove_all(tempDir);
    return 0;
}

#undef CHECK_TRUE
