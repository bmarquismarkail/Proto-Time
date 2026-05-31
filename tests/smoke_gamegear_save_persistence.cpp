#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/BackgroundTaskService.hpp"

namespace {

void writeBinary(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(output);
}

std::vector<uint8_t> readBinary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void removeIfExists(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

}

int main()
{
    const auto tempRoot = std::filesystem::temp_directory_path() / "proto-time-gamegear-save-smoke";
    std::filesystem::create_directories(tempRoot);

    const auto romPath = tempRoot / "battery.gg";
    const auto savePath = tempRoot / "battery.sav";
    removeIfExists(romPath);
    removeIfExists(savePath);

    std::vector<uint8_t> rom(0x4000u, 0x00u);
    writeBinary(romPath, rom);

    {
        BMMQ::GameGearMachine machine;
        machine.setRomSourcePath(romPath);
        machine.loadRom(rom);
        machine.runtimeContext().write8(0xFFFCu, 0x08u);
        machine.runtimeContext().write8(0x8000u, 0x56u);
        machine.runtimeContext().write8(0xBFFFu, 0x78u);
        assert(machine.flushCartridgeSave());
    }

    assert(std::filesystem::exists(savePath));
    const auto saved = readBinary(savePath);
    assert(saved.size() == 0x8000u);
    assert(saved[0x0000u] == 0x56u);
    assert(saved[0x3FFFu] == 0x78u);

    removeIfExists(savePath);
    {
        BMMQ::BackgroundTaskService backgroundTasks;
        backgroundTasks.start();

        BMMQ::GameGearMachine machine;
        machine.setBackgroundTaskService(&backgroundTasks);
        machine.setRomSourcePath(romPath);
        machine.loadRom(rom);
        machine.runtimeContext().write8(0xFFFCu, 0x08u);
        machine.runtimeContext().write8(0x8000u, 0xA5u);
        machine.runtimeContext().write8(0xBFFFu, 0x5Au);
        for (int i = 0; i < 30000; ++i) {
            machine.step();
            if (backgroundTasks.stats().tasksSubmitted != 0u) {
                break;
            }
        }
        assert(backgroundTasks.stats().tasksSubmitted >= 1u);
        backgroundTasks.shutdown();
    }

    assert(waitUntil([&]() { return std::filesystem::exists(savePath); }, std::chrono::seconds(2)));
    const auto backgroundSaved = readBinary(savePath);
    assert(backgroundSaved.size() == 0x8000u);
    assert(backgroundSaved[0x0000u] == 0xA5u);
    assert(backgroundSaved[0x3FFFu] == 0x5Au);

    {
        BMMQ::GameGearMachine machine;
        machine.setRomSourcePath(romPath);
        machine.loadRom(rom);
        machine.runtimeContext().write8(0xFFFCu, 0x08u);
        assert(machine.runtimeContext().read8(0x8000u) == 0xA5u);
        assert(machine.runtimeContext().read8(0xBFFFu) == 0x5Au);
        assert(!machine.flushCartridgeSave());
    }

    const auto smsRomPath = tempRoot / "diagnostic.sms";
    const auto smsSavePath = tempRoot / "diagnostic.sav";
    removeIfExists(smsRomPath);
    removeIfExists(smsSavePath);
    writeBinary(smsRomPath, rom);

    {
        BMMQ::GameGearMachine machine;
        machine.setRomSourcePath(smsRomPath);
        machine.loadRom(rom);
        machine.runtimeContext().write8(0xFFFCu, 0x08u);
        machine.runtimeContext().write8(0x8000u, 0x9Au);
        assert(!machine.flushCartridgeSave());
    }
    assert(!std::filesystem::exists(smsSavePath));

    return 0;
}
