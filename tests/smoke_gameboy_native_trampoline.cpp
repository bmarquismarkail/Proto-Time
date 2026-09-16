#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/modding/NativeMod.hpp"
#include <cassert>
#include <fstream>

int main(int argc, char** argv)
{
    assert(argc == 2);
    GB::GameBoyMachine machine;
    std::vector<std::uint8_t> rom(0x8000u, 0u);
    rom[0x100] = 0xCD; rom[0x101] = 0x00; rom[0x102] = 0x02; // CALL $0200
    rom[0x103] = 0x00; // return target: NOP
    rom[0x180] = 0x76; // HALT, used to verify stalled fetches do not invoke hooks.
    rom[0x200] = 0xC9; // native trampoline address: RET placeholder
    machine.loadRom(rom);
    const auto region = machine.modHost().createRegion("fixture:pool", 4u);
    assert(region != 0u);
    assert(machine.modHost().addSymbol("Target", {2u, 0x4000u}));
    BMMQ::Modding::LoadedMod metadata{"fixture", "1", 0u, {{"pool", region}}, argv[1]};
    auto module = BMMQ::Modding::NativeMod::load(metadata, machine.modHost());
    assert(machine.installNativeTrampoline(0x0200u, std::move(module), 1u));
    assert(!machine.installNativeTrampoline(0x0200u, nullptr, 1u));
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0100u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::HL, 0u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::SP, 0xFFFEu);
    machine.step(); // CALL enters trampoline; native path is taken on next slice.
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0200u);
    const auto result = machine.runSlice(BMMQ::ExecutionBudget{1u, 100u, true});
    assert(result.progress.retiredInstructions == 1u);
    assert(result.progress.retiredCycles == 16u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::HL) == 1u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0103u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::SP) == 0xFFFEu);
    assert(machine.modHost().region(region)[0] == 1u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0100u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::HL, 0u);
    const auto multi = machine.runSlice(BMMQ::ExecutionBudget{3u, 100u, false});
    assert(multi.progress.retiredInstructions == 3u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0104u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::HL) == 2u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0200u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::HL, 0u);
    const auto empty = machine.runSlice(BMMQ::ExecutionBudget{0u, 100u, false});
    assert(empty.progress.retiredInstructions == 0u);
    assert(machine.modHost().region(region)[0] == 2u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0180u);
    machine.runtimeContext().write8(0xFFFFu, 0u);
    machine.step();
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0200u);
    (void)machine.runSlice(BMMQ::ExecutionBudget{1u, 100u, false});
    assert(machine.modHost().region(region)[0] == 2u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0200u);
    // Reloading also ends HALT and invalidates the native registration.
    machine.loadRom(rom);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0200u);
    const auto fallback = machine.runSlice(BMMQ::ExecutionBudget{1u, 100u, true});
    assert(fallback.progress.retiredInstructions == 1u);
    assert(machine.modHost().region(region)[0] == 2u);
}
