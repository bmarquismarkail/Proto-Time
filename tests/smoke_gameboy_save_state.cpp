#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <vector>

#include <unistd.h>

#include "cores/gameboy/GameBoyMachine.hpp"

namespace {

std::vector<uint8_t> makeRom()
{
    std::vector<uint8_t> rom(0x100000u, 0x00u);
    rom[0x0147u] = 0x13u; // MBC3 + RAM + battery
    rom[0x0148u] = 0x05u;
    rom[0x0149u] = 0x03u;
    rom[0x0100u] = 0x3Eu; // LD A,$42
    rom[0x0101u] = 0x42u;
    rom[0x0102u] = 0x00u;
    return rom;
}

std::filesystem::path makeSavePath()
{
    std::ostringstream name;
    name << "proto-time-gb-machine-" << ::getpid() << "-" << ::getppid() << "-"
         << std::chrono::steady_clock::now().time_since_epoch().count() << ".ptss";
    return std::filesystem::temp_directory_path() / name.str();
}

void removeIfExists(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

int main()
{
    const auto savePath = makeSavePath();
    removeIfExists(savePath);

    const auto rom = makeRom();
    GB::GameBoyMachine machine;
    machine.loadRom(rom);
    machine.runtimeContext().writeRegister16(GB::RegisterId::BC, 0xBEEFu);
    machine.runtimeContext().writeRegister16(GB::RegisterId::SP, 0xC123u);
    machine.runtimeContext().write8(0xC000u, 0xA5u);
    machine.runtimeContext().write8(0xFF26u, 0x80u);
    machine.runtimeContext().write8(0xFF24u, 0x77u);
    machine.setJoypadState(0x05u);
    machine.runtimeContext().write8(0x0000u, 0x0Au);
    machine.runtimeContext().write8(0xA000u, 0x12u);
    machine.runtimeContext().write8(0x4000u, 0x01u);
    machine.runtimeContext().write8(0xA000u, 0x34u);
    machine.step();
    const auto originalFingerprint = machine.deterministicStateFingerprint();
    assert(originalFingerprint.size() == 16u);
    const auto savedPc = machine.runtimeContext().readRegister16(GB::RegisterId::PC);

    machine.save_state(savePath);
    assert(std::filesystem::exists(savePath));

    machine.runtimeContext().writeRegister16(GB::RegisterId::BC, 0x1111u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::SP, 0x2222u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x3333u);
    machine.runtimeContext().write8(0xC000u, 0x00u);
    machine.runtimeContext().write8(0xFF24u, 0x00u);
    machine.setJoypadState(0x00u);
    machine.runtimeContext().write8(0x4000u, 0x00u);
    machine.runtimeContext().write8(0xA000u, 0x99u);

    machine.load_state(savePath);

    assert(machine.deterministicStateFingerprint() == originalFingerprint);

    assert(machine.runtimeContext().readRegister16(GB::RegisterId::BC) == 0xBEEFu);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::SP) == 0xC123u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == savedPc);
    assert(machine.runtimeContext().read8(0xC000u) == 0xA5u);
    assert(machine.runtimeContext().read8(0xFF24u) == 0x77u);
    assert(machine.currentDigitalInputMask().has_value());
    assert(*machine.currentDigitalInputMask() == 0x05u);
    machine.runtimeContext().write8(0x0000u, 0x0Au);
    machine.runtimeContext().write8(0x4000u, 0x00u);
    assert(machine.runtimeContext().read8(0xA000u) == 0x12u);
    machine.runtimeContext().write8(0x4000u, 0x01u);
    assert(machine.runtimeContext().read8(0xA000u) == 0x34u);
    machine.runtimeContext().write8(0xC000u, 0xA4u);
    assert(machine.deterministicStateFingerprint() != originalFingerprint);

    GB::GameBoyMachine unloaded;
    bool rejectedWithoutRom = false;
    try {
        unloaded.load_state(savePath);
    } catch (const std::runtime_error&) {
        rejectedWithoutRom = true;
    }
    assert(rejectedWithoutRom);

    removeIfExists(savePath);
    return 0;
}
