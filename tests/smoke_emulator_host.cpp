#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "emulator/EmulatorHost.hpp"

namespace {

std::filesystem::path makeTempDir()
{
    auto dir = std::filesystem::temp_directory_path() / "proto_time_emulator_host_smoke";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void writeBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("unable to create test file: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output.good()) {
        throw std::runtime_error("unable to write test file: " + path.string());
    }
}

} // namespace

int main()
{
    const auto tempDir = makeTempDir();
    const auto gameBoyRomPath = tempDir / "test.gb";
    const auto gameGearRomPath = tempDir / "test.gg";
    const auto bootRomPath = tempDir / "boot.bin";

    std::vector<std::uint8_t> gameBoyRom(0x8000u, 0x00u);
    gameBoyRom[0x0100] = 0x00u;
    std::vector<std::uint8_t> gameGearRom(0x4000u, 0x00u);
    std::vector<std::uint8_t> bootRom(0x100u, 0x00u);

    writeBinaryFile(gameBoyRomPath, gameBoyRom);
    writeBinaryFile(gameGearRomPath, gameGearRom);
    writeBinaryFile(bootRomPath, bootRom);

    {
        BMMQ::EmulatorConfig config;
        config.machineKind = std::string("gameboy");
        config.romPath = gameBoyRomPath;
        config.bootRomPath = bootRomPath;
        config.headless = true;

        auto bootstrapped = BMMQ::bootstrapMachine(config);
        assert(bootstrapped.descriptor.id == "gameboy");
        assert(bootstrapped.romSize == gameBoyRom.size());
        (void)bootstrapped.machine->pluginManager();
        (void)bootstrapped.machine->timingService();
        bootstrapped.machine->serviceInput();
        bootstrapped.machine->step();
    }

    {
        BMMQ::EmulatorConfig config;
        config.machineKind = std::string("gamegear");
        config.romPath = gameGearRomPath;
        config.headless = true;

        auto bootstrapped = BMMQ::bootstrapMachine(config);
        assert(bootstrapped.descriptor.id == "gamegear");
        assert(bootstrapped.romSize == gameGearRom.size());
        (void)bootstrapped.machine->pluginManager();
        (void)bootstrapped.machine->timingService();
        bootstrapped.machine->serviceInput();
        bootstrapped.machine->step();
    }

    {
        BMMQ::EmulatorConfig config;
        config.machineKind = std::string("gamegear");
        config.romPath = gameGearRomPath;
        config.bootRomPath = bootRomPath;
        config.headless = true;

        auto bootstrapped = BMMQ::bootstrapMachine(config);
        assert(bootstrapped.descriptor.id == "gamegear");
        assert(bootstrapped.romSize == gameGearRom.size());
        bootstrapped.machine->step();
    }

    std::filesystem::remove_all(tempDir);
    return 0;
}
