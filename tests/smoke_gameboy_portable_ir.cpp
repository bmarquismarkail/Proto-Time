#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/GameBoyIrExecution.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/RegisterId.hpp"

namespace {

std::vector<std::uint8_t> makeIrSubsetRom()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    std::vector<std::uint8_t> program{
        0x06u, 0x12u, // LD B,0x12
        0x0Eu, 0x07u, // LD C,0x07
        0x16u, 0x20u, // LD D,0x20
        0x1Eu, 0x02u, // LD E,0x02
        0x3Eu, 0xF0u, // LD A,0xF0
        0x04u,        // INC B
        0x0Du,        // DEC C
        0x77u,        // LD (HL),A
        0x34u,        // INC (HL)
        0x35u,        // DEC (HL)
        0x7Eu,        // LD A,(HL)
        0x80u,        // ADD A,B
        0x89u,        // ADC A,C
        0x92u,        // SUB D
        0x9Bu,        // SBC A,E
        0xA4u,        // AND H
        0xADu,        // XOR L
        0xB0u,        // OR B
        0xB9u,        // CP C
        0x18u, 0x00u, // JR displacement filled below
    };
    program.back() = static_cast<std::uint8_t>(
        -static_cast<std::int32_t>(program.size()));
    std::copy(program.begin(), program.end(), rom.begin() + 0x0100u);
    return rom;
}

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
    assert(baseline.runtimeContext().read8(0xC100u) ==
           portableIr.runtimeContext().read8(0xC100u));
    assert(baseline.runtimeContext().getLastFeedback().retiredCycles ==
           portableIr.runtimeContext().getLastFeedback().retiredCycles);
    assert(baseline.recentAudioSamples() == portableIr.recentAudioSamples());
    assert(baseline.audioFrameCounter() == portableIr.audioFrameCounter());
    const auto baselineVideo = baseline.videoStateSnapshot();
    const auto irVideo = portableIr.videoStateSnapshot();
    assert(baselineVideo.has_value() && irVideo.has_value());
    assert(baselineVideo->ly == irVideo->ly);
    assert(baselineVideo->stat == irVideo->stat);
}

void testPortableIrMatchesCanonicalRetirement()
{
    GB::GameBoyMachine baseline;
    GB::GameBoyMachine portableIr;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy irPolicy;
    portableIr.attachExecutorPolicy(irPolicy);
    portableIr.setPortableIrEnabled(true);
    assert(portableIr.portableIrEnabled());

    const auto rom = makeIrSubsetRom();
    baseline.loadRom(rom);
    portableIr.loadRom(rom);
    baseline.runtimeContext().writeRegister16(GB::RegisterId::HL, 0xC100u);
    portableIr.runtimeContext().writeRegister16(GB::RegisterId::HL, 0xC100u);

    bool observedIrExecution = false;
    for (std::size_t step = 0u; step < 128u; ++step) {
        baseline.step();
        portableIr.step();
        assertEquivalent(baseline, portableIr);
        observedIrExecution = observedIrExecution ||
            portableIr.runtimeContext().getLastFeedback().executionPath ==
                BMMQ::ExecutionPathHint::PortableIr;
    }

    const auto stats = portableIr.blockCacheStats();
    assert(observedIrExecution);
    assert(stats.irTranslations.load() > 0u);
    assert(stats.irExecutions.load() > 0u);
}

void testUnsupportedOpcodeFallsBackAndModeToggleInvalidates()
{
    GB::GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy policy;
    machine.attachExecutorPolicy(policy);
    machine.setPortableIrEnabled(true);
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    machine.loadRom(rom);
    machine.runtimeContext().write8(0xC000u, 0x04u); // INC B (lowered)
    machine.runtimeContext().write8(0xC001u, 0x2Fu); // CPL (portable fallback)
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);

    machine.step();
    machine.step();
    assert(machine.runtimeContext().getLastFeedback().executionPath !=
           BMMQ::ExecutionPathHint::PortableIr);

    const auto invalidationsBefore = machine.blockCacheStats().invalidations.load();
    machine.setPortableIrEnabled(false);
    assert(!machine.portableIrEnabled());
    assert(machine.blockCacheStats().invalidations.load() > invalidationsBefore);
}

void testEveryPortableIrGuardFailureIsClassified()
{
    const BMMQ::TranslatedInstruction<std::uint16_t, std::uint8_t> instruction{
        .address = 0xC000u,
        .bytes = {0x00u, 0x00u, 0x00u},
        .length = 1u,
    };
    const auto block = GB::IRExecution::lowerBlock(
        std::span(&instruction, 1u), 7u, 0u);
    assert(block);

    struct Code {
        std::uint8_t byte = 0x00u;
        bool eligible = true;
    } code;
    auto peek = [](const void* opaque, std::uint16_t, std::uint8_t& value) noexcept {
        const auto& code = *static_cast<const Code*>(opaque);
        if (!code.eligible) return false;
        value = code.byte;
        return true;
    };
    GB::IRExecution::GuardContext context{
        .mappingGeneration = 7u,
        .executionState = 0u,
        .opaque = &code,
        .peekCodeByte = peek,
    };
    assert(GB::IRExecution::validateGuards(*block, context) ==
           GB::IRExecution::GuardFailure::None);

    context.mappingGeneration = 8u;
    assert(GB::IRExecution::validateGuards(*block, context) ==
           GB::IRExecution::GuardFailure::MappingGeneration);
    context.mappingGeneration = 7u;

    auto helperMismatch = *block;
    for (auto& guard : helperMismatch.guards) {
        if (guard.kind == BMMQ::IR::GuardKind::HelperAbi) ++guard.expected;
    }
    assert(GB::IRExecution::validateGuards(helperMismatch, context) ==
           GB::IRExecution::GuardFailure::HelperAbi);

    for (const auto boundary : {
             GB::IRExecution::Stop,
             GB::IRExecution::Halt,
             GB::IRExecution::DmaRestricted,
             GB::IRExecution::InterruptPending,
             GB::IRExecution::HaltBugPending,
             GB::IRExecution::PendingCycleCharge,
         }) {
        context.executionState = boundary;
        assert(GB::IRExecution::validateGuards(*block, context) ==
               GB::IRExecution::GuardFailure::ExecutionState);
    }
    context.executionState = 0u;

    code.byte = 0xFFu;
    assert(GB::IRExecution::validateGuards(*block, context) ==
           GB::IRExecution::GuardFailure::CodeBytes);
    assert(GB::IRExecution::validateContinuationGuards(*block, 7u, 0u) ==
           GB::IRExecution::GuardFailure::None);
    assert(GB::IRExecution::validateContinuationGuards(
               *block, 7u, GB::IRExecution::InterruptPending) ==
           GB::IRExecution::GuardFailure::ExecutionState);
    code.byte = 0x00u;
    code.eligible = false;
    assert(GB::IRExecution::validateGuards(*block, context) ==
           GB::IRExecution::GuardFailure::IneligibleCode);
}

void testMappedDeviceCodeIsNeverLowered()
{
    GB::GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy policy;
    machine.attachExecutorPolicy(policy);
    machine.setPortableIrEnabled(true);
    machine.loadRom(std::vector<std::uint8_t>(0x8000u, 0x00u));
    machine.runtimeContext().write8(0x8000u, 0x00u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x8000u);
    machine.step();
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x8000u);
    machine.step();
    const auto stats = machine.blockCacheStats();
    assert(stats.irExecutions.load() == 0u);
    assert(stats.irIneligibleTranslations.load() > 0u);
}

} // namespace

int main()
{
    testPortableIrMatchesCanonicalRetirement();
    testUnsupportedOpcodeFallsBackAndModeToggleInvalidates();
    testEveryPortableIrGuardFailureIsClassified();
    testMappedDeviceCodeIsNeverLowered();
    return 0;
}
