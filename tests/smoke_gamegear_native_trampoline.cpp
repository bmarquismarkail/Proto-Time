#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/modding/NativeMod.hpp"

#include <cassert>
#include <filesystem>

int main(int argc, char** argv)
{
    assert(argc == 2);
    BMMQ::GameGearMachine machine;
    std::vector<std::uint8_t> rom(0xC000u, 0x00u);
    rom[0x0100u] = 0xCDu; // CALL $4000
    rom[0x0101u] = 0x00u;
    rom[0x0102u] = 0x40u;
    rom[0x0103u] = 0x00u;
    rom[0x4000u] = 0xC9u; // RET placeholder in physical bank 1
    machine.loadRom(rom);

    const auto region = machine.modHost().createRegion("fixture:pool", 4u);
    assert(region != 0u);
    BMMQ::Modding::LoadedMod metadata{"fixture", "1", 0u, {{"pool", region}}, argv[1], {}};
    auto module = BMMQ::Modding::NativeMod::load(metadata, machine.modHost());
    assert(machine.installNativeTrampoline(0x4000u, 1u, std::move(module), 1u));
    auto duplicate = BMMQ::Modding::NativeMod::load(metadata, machine.modHost());
    assert(!machine.installNativeTrampoline(0x4000u, 1u, std::move(duplicate), 1u));

    machine.runtimeContext().writeRegister16("PC", 0x0100u);
    machine.runtimeContext().writeRegister16("SP", 0xD000u);
    machine.runtimeContext().writeRegister16("HL", 0u);
    machine.step();
    assert(machine.runtimeContext().readRegister16("PC") == 0x4000u);
    const auto result = machine.runSlice(BMMQ::ExecutionBudget{1u, 100u, true});
    assert(result.progress.retiredInstructions == 1u);
    assert(result.progress.retiredCycles == 10u);
    assert(machine.runtimeContext().readRegister16("HL") == 1u);
    assert(machine.runtimeContext().readRegister16("PC") == 0x0103u);
    assert(machine.modHost().region(region)[0] == 1u);

    bool saveRejected = false;
    try {
        machine.save_state(std::filesystem::temp_directory_path() / "gg-active-mod.state");
    } catch (const std::runtime_error&) {
        saveRejected = true;
    }
    assert(saveRejected);
    bool loadRejected = false;
    try {
        machine.load_state(std::filesystem::temp_directory_path() / "gg-active-mod.state");
    } catch (const std::runtime_error&) {
        loadRejected = true;
    }
    assert(loadRejected);

    machine.runtimeContext().write8(0xFFFEu, 2u); // Bank 2 replaces the expected bank in $4000.
    machine.runtimeContext().writeRegister16("PC", 0x4000u);
    machine.runtimeContext().writeRegister16("HL", 0u);
    const auto mismatched = machine.runSlice(BMMQ::ExecutionBudget{1u, 100u, true});
    assert(mismatched.progress.retiredInstructions == 1u);
    assert(machine.runtimeContext().readRegister16("PC") == 0x4001u);
    assert(machine.runtimeContext().readRegister16("HL") == 0u);
    assert(machine.modHost().region(region)[0] == 1u);

    machine.clearNativeTrampolines();
    const auto statePath = std::filesystem::temp_directory_path() / "gg-no-active-mod.state";
    machine.save_state(statePath);
    std::filesystem::remove(statePath);
}
