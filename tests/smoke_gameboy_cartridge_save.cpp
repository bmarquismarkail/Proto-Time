#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/cartridge/GameBoyCartridge.hpp"

namespace {

std::vector<uint8_t> makeRom(uint8_t cartridgeType, uint8_t romSizeCode, uint8_t ramSizeCode)
{
    std::vector<uint8_t> rom(0x100000u, 0x00u);
    rom[0x0147] = cartridgeType;
    rom[0x0148] = romSizeCode;
    rom[0x0149] = ramSizeCode;
    rom[0x0100] = 0x00u;
    rom[0x4000] = 0x11u;
    rom[0x8000] = 0x22u;
    return rom;
}

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

} // namespace

int main()
{
    const auto tempRoot = std::filesystem::temp_directory_path() / "proto-time-cartridge-save-smoke";
    std::filesystem::create_directories(tempRoot);

    const auto batteryRom = makeRom(0x13u, 0x05u, 0x03u);
    const auto batteryMetadata = GB::parseCartridgeMetadata(batteryRom);
    assert(batteryMetadata.mapper == GB::CartridgeMapper::MBC3);
    assert(batteryMetadata.externalRamSize == 0x8000u);
    assert(batteryMetadata.hasBattery);
    assert(!batteryMetadata.hasRtc);

    GB::GameBoyCartridge cartridge;
    cartridge.load(batteryRom);
    assert(cartridge.supportsBatterySave());
    assert(!cartridge.supportsRtc());
    assert(!cartridge.hasDirtySaveData());

    cartridge.write(0x0000u, 0x0Au);
    cartridge.write(0xA000u, 0x12u);
    assert(cartridge.hasDirtySaveData());
    cartridge.write(0x4000u, 0x01u);
    cartridge.write(0xA000u, 0x34u);

    const auto exported = cartridge.exportSaveData();
    assert(exported.externalRam.size() == 0x8000u);
    assert(exported.externalRam[0x0000u] == 0x12u);
    assert(exported.externalRam[0x2000u] == 0x34u);

    GB::GameBoyCartridge imported;
    imported.load(batteryRom);
    imported.importSaveData(exported);
    imported.write(0x0000u, 0x0Au);
    assert(imported.read(0xA000u) == 0x12u);
    imported.write(0x4000u, 0x01u);
    assert(imported.read(0xA000u) == 0x34u);
    imported.markSaveClean();
    assert(!imported.hasDirtySaveData());

    const auto romPath = tempRoot / "battery.gb";
    const auto savePath = tempRoot / "battery.sav";
    const auto rtcPath = tempRoot / "battery.rtc";
    removeIfExists(romPath);
    removeIfExists(savePath);
    removeIfExists(rtcPath);
    writeBinary(romPath, batteryRom);

    {
        GameBoyMachine machine;
        machine.loadRomFromPath(romPath);
        assert(!machine.cartridge().hasDirtySaveData());
        machine.runtimeContext().write8(0x0000u, 0x0Au);
        machine.runtimeContext().write8(0xA000u, 0x56u);
        machine.runtimeContext().write8(0x4000u, 0x01u);
        machine.runtimeContext().write8(0xA000u, 0x78u);
        assert(machine.cartridge().hasDirtySaveData());
        assert(machine.flushCartridgeSave());
        assert(!machine.cartridge().hasDirtySaveData());
    }
    assert(std::filesystem::exists(savePath));
    assert(!std::filesystem::exists(rtcPath));
    const auto savedRam = readBinary(savePath);
    assert(savedRam.size() == 0x8000u);
    assert(savedRam[0x0000u] == 0x56u);
    assert(savedRam[0x2000u] == 0x78u);

    {
        GameBoyMachine machine;
        machine.loadRomFromPath(romPath);
        machine.runtimeContext().write8(0x0000u, 0x0Au);
        assert(machine.runtimeContext().read8(0xA000u) == 0x56u);
        machine.runtimeContext().write8(0x4000u, 0x01u);
        assert(machine.runtimeContext().read8(0xA000u) == 0x78u);
        assert(!machine.cartridge().hasDirtySaveData());
    }

    const auto nonBatteryRom = makeRom(0x12u, 0x05u, 0x03u);
    const auto nonBatteryPath = tempRoot / "volatile.gb";
    const auto nonBatterySavePath = tempRoot / "volatile.sav";
    removeIfExists(nonBatteryPath);
    removeIfExists(nonBatterySavePath);
    writeBinary(nonBatteryPath, nonBatteryRom);
    {
        GameBoyMachine machine;
        machine.loadRomFromPath(nonBatteryPath);
        machine.runtimeContext().write8(0x0000u, 0x0Au);
        machine.runtimeContext().write8(0xA000u, 0x99u);
        assert(!machine.cartridge().supportsBatterySave());
        assert(!machine.flushCartridgeSave());
    }
    assert(!std::filesystem::exists(nonBatterySavePath));

    const auto rtcRom = makeRom(0x10u, 0x05u, 0x03u);
    const auto rtcMetadata = GB::parseCartridgeMetadata(rtcRom);
    assert(rtcMetadata.hasBattery);
    assert(rtcMetadata.hasRtc);
    const auto rtcRomPath = tempRoot / "rtc.gb";
    const auto rtcSavePath = tempRoot / "rtc.sav";
    const auto rtcSidecarPath = tempRoot / "rtc.rtc";
    removeIfExists(rtcRomPath);
    removeIfExists(rtcSavePath);
    removeIfExists(rtcSidecarPath);
    writeBinary(rtcRomPath, rtcRom);
    {
        GameBoyMachine machine;
        machine.loadRomFromPath(rtcRomPath);
        assert(machine.cartridge().supportsRtc());
        machine.runtimeContext().write8(0x0000u, 0x0Au);
        machine.runtimeContext().write8(0x4000u, 0x08u);
        machine.runtimeContext().write8(0xA000u, 0x2Au);
        assert(machine.cartridge().hasDirtySaveData());
        assert(machine.flushCartridgeSave());
    }
    assert(std::filesystem::exists(rtcSavePath));
    assert(std::filesystem::exists(rtcSidecarPath));
    assert(readBinary(rtcSavePath).size() == 0x8000u);

    {
        GameBoyMachine machine;
        machine.loadRomFromPath(rtcRomPath);
        machine.runtimeContext().write8(0x0000u, 0x0Au);
        machine.runtimeContext().write8(0x4000u, 0x08u);
        assert(machine.runtimeContext().read8(0xA000u) == 0x2Au);
    }

    return 0;
}
