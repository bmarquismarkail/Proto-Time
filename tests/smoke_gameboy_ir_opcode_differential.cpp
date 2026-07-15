#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/RegisterId.hpp"

namespace {

std::uint32_t nextRandom(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

bool usesHlMemory(std::uint8_t opcode)
{
    if (opcode >= 0x40u && opcode <= 0x7Fu) {
        return (opcode & 0x07u) == 6u || ((opcode >> 3u) & 0x07u) == 6u;
    }
    if (opcode >= 0x80u && opcode <= 0xBFu) return (opcode & 0x07u) == 6u;
    return ((opcode & 0xC7u) == 0x04u ||
            (opcode & 0xC7u) == 0x05u ||
            (opcode & 0xC7u) == 0x06u) && ((opcode >> 3u) & 0x07u) == 6u;
}

std::vector<std::uint8_t> supportedOpcodes()
{
    std::vector<std::uint8_t> result{0x00u, 0x18u};
    for (std::uint16_t opcode = 0x40u; opcode <= 0x7Fu; ++opcode) {
        if (opcode != 0x76u) result.push_back(static_cast<std::uint8_t>(opcode));
    }
    for (std::uint8_t base : {0x04u, 0x05u, 0x06u}) {
        for (std::uint8_t reg = 0u; reg < 8u; ++reg) {
            result.push_back(static_cast<std::uint8_t>(base + reg * 8u));
        }
    }
    for (std::uint16_t opcode = 0x80u; opcode <= 0xBFu; ++opcode) {
        result.push_back(static_cast<std::uint8_t>(opcode));
    }
    return result;
}

void writeInstruction(GB::GameBoyMachine& machine, std::uint8_t opcode,
                      std::uint8_t immediate)
{
    machine.runtimeContext().write8(0xC000u, opcode);
    machine.runtimeContext().write8(0xC001u, opcode == 0x18u ? 0xFEu : immediate);
    machine.runtimeContext().write8(0xC002u, 0x00u);
}

void setState(GB::GameBoyMachine& machine, std::uint8_t opcode,
              std::uint32_t& random)
{
    machine.runtimeContext().writeRegister16(
        GB::RegisterId::AF, static_cast<std::uint16_t>(nextRandom(random) & 0xFFF0u));
    machine.runtimeContext().writeRegister16(
        GB::RegisterId::BC, static_cast<std::uint16_t>(nextRandom(random)));
    machine.runtimeContext().writeRegister16(
        GB::RegisterId::DE, static_cast<std::uint16_t>(nextRandom(random)));
    machine.runtimeContext().writeRegister16(
        GB::RegisterId::HL, usesHlMemory(opcode)
            ? 0xC100u
            : static_cast<std::uint16_t>(nextRandom(random)));
    machine.runtimeContext().writeRegister16(
        GB::RegisterId::SP, static_cast<std::uint16_t>(nextRandom(random)));
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
    machine.runtimeContext().write8(0xC100u, static_cast<std::uint8_t>(nextRandom(random)));
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
    const auto& baselineFeedback = baseline.runtimeContext().getLastFeedback();
    const auto& irFeedback = portableIr.runtimeContext().getLastFeedback();
    assert(baselineFeedback.pcBefore == irFeedback.pcBefore);
    assert(baselineFeedback.pcAfter == irFeedback.pcAfter);
    assert(baselineFeedback.retiredCycles == irFeedback.retiredCycles);
    assert(baselineFeedback.isControlFlow == irFeedback.isControlFlow);
    assert(baseline.recentAudioSamples() == portableIr.recentAudioSamples());
    assert(baseline.audioFrameCounter() == portableIr.audioFrameCounter());
    const auto baselineVideo = baseline.videoStateSnapshot();
    const auto irVideo = portableIr.videoStateSnapshot();
    assert(baselineVideo.has_value() && irVideo.has_value());
    assert(baselineVideo->ly == irVideo->ly);
    assert(baselineVideo->stat == irVideo->stat);
}

void testEveryLoweredOpcodeAgainstCanonicalExecution()
{
    std::uint32_t random = 0x11B2026u;
    for (const auto opcode : supportedOpcodes()) {
        GB::GameBoyMachine baseline;
        GB::GameBoyMachine portableIr;
        BMMQ::Plugin::VisibleStatePreservingStepPolicy policy;
        portableIr.attachExecutorPolicy(policy);
        portableIr.setPortableIrEnabled(true);
        const std::vector<std::uint8_t> rom(0x8000u, 0x00u);
        baseline.loadRom(rom);
        portableIr.loadRom(rom);
        const auto immediate = static_cast<std::uint8_t>(nextRandom(random));
        writeInstruction(baseline, opcode, immediate);
        writeInstruction(portableIr, opcode, immediate);

        // Populate the translated entry using the same warm-up retirement on
        // both machines, then restore identical randomized architectural state.
        baseline.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
        portableIr.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
        baseline.step();
        portableIr.step();
        const auto stateSeed = random;
        auto baselineSeed = stateSeed;
        auto irSeed = stateSeed;
        setState(baseline, opcode, baselineSeed);
        setState(portableIr, opcode, irSeed);
        random = baselineSeed;

        baseline.step();
        portableIr.step();
        assert(portableIr.runtimeContext().getLastFeedback().executionPath ==
               BMMQ::ExecutionPathHint::PortableIr);
        assertEquivalent(baseline, portableIr);
    }
}

} // namespace

int main()
{
    testEveryLoweredOpcodeAgainstCanonicalExecution();
    return 0;
}
