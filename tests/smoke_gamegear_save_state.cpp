#include <cassert>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"

namespace {

void removeIfExists(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

int main()
{
    const auto savePath = std::filesystem::temp_directory_path() / "proto-time-gg-machine.ptss";
    removeIfExists(savePath);

    std::vector<uint8_t> rom(0x8000u, 0x00u);
    rom[0x0000u] = 0x3Eu; // LD A,$42
    rom[0x0001u] = 0x42u;

    BMMQ::GameGearMachine machine;
    machine.loadRom(rom);
    machine.runtimeContext().writeRegister16("BC", 0xBEEFu);
    machine.runtimeContext().writeRegister16("SP", 0xC123u);
    machine.runtimeContext().write8(0xC000u, 0xA5u);
    machine.runtimeContext().write8(0xFFFCu, 0x08u);
    machine.runtimeContext().write8(0x8000u, 0x56u);
    machine.runtimeContext().write8(0xBFFFu, 0x78u);
    machine.runtimeContext().write8(0xFF10u, 0x80u);
    machine.runtimeContext().write8(0x8000u, 0x9Au); // VDP VRAM path through memory map
    machine.serviceInput();
    machine.step();
    const auto savedPc = machine.runtimeContext().readRegister16("PC");

    machine.save_state(savePath);
    assert(std::filesystem::exists(savePath));

    machine.runtimeContext().writeRegister16("BC", 0x1111u);
    machine.runtimeContext().writeRegister16("SP", 0x2222u);
    machine.runtimeContext().writeRegister16("PC", 0x3333u);
    machine.runtimeContext().write8(0xC000u, 0x00u);
    machine.runtimeContext().write8(0xFFFCu, 0x08u);
    machine.runtimeContext().write8(0x8000u, 0x00u);

    machine.load_state(savePath);

    assert(machine.runtimeContext().readRegister16("BC") == 0xBEEFu);
    assert(machine.runtimeContext().readRegister16("SP") == 0xC123u);
    assert(machine.runtimeContext().readRegister16("PC") == savedPc);
    assert(machine.runtimeContext().read8(0xC000u) == 0xA5u);
    machine.runtimeContext().write8(0xFFFCu, 0x08u);
    assert(machine.runtimeContext().read8(0xBFFFu) == 0x78u);

    BMMQ::GameGearMachine unloaded;
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
