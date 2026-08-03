#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

namespace {

std::vector<std::uint8_t> makeRom()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0] = 0x06u; rom[1] = 0x7Fu; // LD B,7F
    rom[2] = 0x04u;                 // INC B
    rom[3] = 0x48u;                 // LD C,B
    rom[4] = 0x81u;                 // ADD A,C
    rom[5] = 0x0Du;                 // DEC C
    rom[6] = 0xAFu;                 // XOR A
    rom[7] = 0x18u; rom[8] = 0xF9u; // JR 0002
    return rom;
}

void assertCpuEquivalent(const BMMQ::GameGearMachine& baseline,
                         const BMMQ::GameGearMachine& ir)
{
    for (const std::string_view id : {"AF", "BC", "DE", "HL", "IX", "IY", "SP", "PC"}) {
        assert(baseline.readRegisterPair(id) == ir.readRegisterPair(id));
    }
    const auto& expected = baseline.runtimeContext().getLastFeedback();
    const auto& actual = ir.runtimeContext().getLastFeedback();
    assert(expected.pcBefore == actual.pcBefore);
    assert(expected.pcAfter == actual.pcAfter);
    assert(expected.retiredCycles == actual.retiredCycles);
}

void testDifferentialExecutionAndMetrics()
{
    BMMQ::GameGearMachine baseline;
    BMMQ::GameGearMachine ir;
    BMMQ::Plugin::PortableIrStepPolicy policy;
    ir.attachExecutorPolicy(policy);
    const auto rom = makeRom();
    baseline.loadRom(rom);
    ir.loadRom(rom);

    for (std::size_t instruction = 0u; instruction < 512u; ++instruction) {
        baseline.step();
        ir.step();
        assertCpuEquivalent(baseline, ir);
    }
    assert(baseline.recentAudioSamples() == ir.recentAudioSamples());
    assert(baseline.audioFrameCounter() == ir.audioFrameCounter());
    const auto stats = ir.irStats();
    assert(stats.translations > 0u);
    assert(stats.executions == 512u);
    assert(stats.executionNanos > 0u);
}

void testCodeChangeReplacesCachedIrBeforeExecution()
{
    BMMQ::GameGearMachine machine;
    BMMQ::Plugin::PortableIrStepPolicy policy;
    machine.attachExecutorPolicy(policy);
    machine.loadRom(makeRom());
    machine.runtimeContext().write8(0xC000u, 0x00u); // NOP
    machine.runtimeContext().writeRegister16("PC", 0xC000u);
    machine.step();
    const auto before = machine.irStats();

    machine.runtimeContext().write8(0xC000u, 0x04u); // INC B
    machine.runtimeContext().writeRegister16("PC", 0xC000u);
    const auto oldB = static_cast<std::uint8_t>(machine.readRegisterPair("BC") >> 8u);
    machine.step();
    const auto newB = static_cast<std::uint8_t>(machine.readRegisterPair("BC") >> 8u);
    const auto after = machine.irStats();
    assert(newB == static_cast<std::uint8_t>(oldB + 1u));
    assert(after.translations == before.translations + 1u);
}

} // namespace

int main()
{
    testDifferentialExecutionAndMetrics();
    testCodeChangeReplacesCachedIrBeforeExecution();
    return 0;
}
