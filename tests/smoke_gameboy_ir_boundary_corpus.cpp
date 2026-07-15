#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/RegisterId.hpp"

namespace {

void assertEquivalent(const GB::GameBoyMachine& baseline,
                      const GB::GameBoyMachine& portableIr)
{
    for (const std::string_view reg : {
             GB::RegisterId::AF, GB::RegisterId::BC, GB::RegisterId::DE,
             GB::RegisterId::HL, GB::RegisterId::SP, GB::RegisterId::PC,
         }) {
        assert(baseline.runtimeContext().readRegister16(reg) ==
               portableIr.runtimeContext().readRegister16(reg));
    }
    for (const auto address : {0xC100u, 0xFF0Fu, 0xFF42u, 0xFFFFu}) {
        assert(baseline.runtimeContext().peek8(address) ==
               portableIr.runtimeContext().peek8(address));
    }
    const auto& baselineFeedback = baseline.runtimeContext().getLastFeedback();
    const auto& irFeedback = portableIr.runtimeContext().getLastFeedback();
    assert(baselineFeedback.pcBefore == irFeedback.pcBefore);
    assert(baselineFeedback.pcAfter == irFeedback.pcAfter);
    assert(baselineFeedback.retiredCycles == irFeedback.retiredCycles);
    assert(baselineFeedback.isControlFlow == irFeedback.isControlFlow);
    assert(baseline.recentAudioSamples() == portableIr.recentAudioSamples());
    const auto baselineVideo = baseline.videoStateSnapshot();
    const auto irVideo = portableIr.videoStateSnapshot();
    assert(baselineVideo.has_value() && irVideo.has_value());
    assert(baselineVideo->ly == irVideo->ly);
    assert(baselineVideo->stat == irVideo->stat);
}

template <typename Configure>
std::uint64_t runDifferentialScenario(std::vector<std::uint8_t> rom,
                                      std::size_t steps,
                                      Configure&& configure)
{
    GB::GameBoyMachine baseline;
    GB::GameBoyMachine portableIr;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy policy;
    portableIr.attachExecutorPolicy(policy);
    portableIr.setPortableIrEnabled(true);
    baseline.loadRom(rom);
    portableIr.loadRom(rom);
    configure(baseline);
    configure(portableIr);

    for (std::size_t index = 0u; index < steps; ++index) {
        baseline.step();
        portableIr.step();
        assertEquivalent(baseline, portableIr);
    }
    return portableIr.blockCacheStats().irExecutions.load();
}

void testMixedSupportedAndFallbackCorpus()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0x06u; rom[0x0101u] = 0x03u; // LD B,3
    rom[0x0102u] = 0xCDu; rom[0x0103u] = 0x10u; rom[0x0104u] = 0x01u; // CALL 0110
    rom[0x0105u] = 0x0Cu;                       // INC C
    rom[0x0106u] = 0x18u; rom[0x0107u] = 0xF8u; // JR 0100
    rom[0x0110u] = 0x80u;                       // ADD A,B
    rom[0x0111u] = 0x2Fu;                       // CPL (fallback)
    rom[0x0112u] = 0xC9u;                       // RET (fallback)

    assert(runDifferentialScenario(std::move(rom), 1024u, [](auto&) {}) > 0u);
}

void testMappedOperandCorpus()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0x77u;                       // LD (HL),A
    rom[0x0101u] = 0x7Eu;                       // LD A,(HL)
    rom[0x0102u] = 0x3Cu;                       // INC A
    rom[0x0103u] = 0x18u; rom[0x0104u] = 0xFBu; // JR 0100
    assert(runDifferentialScenario(std::move(rom), 512u, [](auto& machine) {
        machine.runtimeContext().writeRegister16(GB::RegisterId::HL, 0xFF42u);
        machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x2200u);
    }) > 0u);
}

void testInterruptAndHaltBugBoundaries()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0x76u;       // HALT with pending interrupt and IME clear: HALT bug
    rom[0x0101u] = 0x06u;
    rom[0x0102u] = 0x06u;       // duplicated immediate under HALT-bug fetch
    rom[0x0103u] = 0xFBu;       // EI
    rom[0x0104u] = 0x00u;       // delayed-enable retirement
    rom[0x0105u] = 0x00u;
    rom[0x0040u] = 0x00u;       // VBlank vector
    runDifferentialScenario(std::move(rom), 16u, [](auto& machine) {
        machine.runtimeContext().write8(0xFFFFu, 0x01u);
        machine.runtimeContext().write8(0xFF0Fu, 0x01u);
    });
}

} // namespace

int main()
{
    testMixedSupportedAndFallbackCorpus();
    testMappedOperandCorpus();
    testInterruptAndHaltBugBoundaries();
    return 0;
}
