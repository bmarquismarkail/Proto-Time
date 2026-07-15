#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/RegisterId.hpp"

namespace {

std::vector<std::uint8_t> makeSequentialRom()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0x04u; // INC B
    rom[0x0101u] = 0x0Cu; // INC C
    rom[0x0102u] = 0x3Cu; // INC A
    rom[0x0103u] = 0x2Cu; // INC L
    return rom;
}

void assertEquivalent(const GB::GameBoyMachine& left, const GB::GameBoyMachine& right)
{
    for (const std::string_view reg : {
             GB::RegisterId::AF,
             GB::RegisterId::BC,
             GB::RegisterId::DE,
             GB::RegisterId::HL,
             GB::RegisterId::SP,
             GB::RegisterId::PC,
         }) {
        assert(left.runtimeContext().readRegister16(reg) ==
               right.runtimeContext().readRegister16(reg));
    }
    assert(left.recentAudioSamples() == right.recentAudioSamples());
    assert(left.audioFrameCounter() == right.audioFrameCounter());
    const auto leftVideo = left.videoStateSnapshot();
    const auto rightVideo = right.videoStateSnapshot();
    assert(leftVideo.has_value() && rightVideo.has_value());
    assert(leftVideo->ly == rightVideo->ly);
    assert(leftVideo->stat == rightVideo->stat);
}

void testSliceMatchesIndividualSteps()
{
    GB::GameBoyMachine stepped;
    GB::GameBoyMachine sliced;
    const auto rom = makeSequentialRom();
    stepped.loadRom(rom);
    sliced.loadRom(rom);

    for (std::size_t index = 0u; index < 4u; ++index) {
        stepped.step();
    }
    const auto result = sliced.runSlice({
        .maxInstructions = 4u,
        .maxCycles = 100u,
        .stopOnSegmentBoundary = false,
    });

    assert(result.progress.retiredInstructions == 4u);
    assert(result.progress.retiredCycles == 16u);
    assert(result.exitReason == BMMQ::ExecutionSliceExitReason::InstructionBudget);
    assert(result.lastFeedback.pcAfter == 0x0104u);
    assertEquivalent(stepped, sliced);
}

void testCycleBudgetIsAnAtomicSoftCeiling()
{
    GB::GameBoyMachine machine;
    machine.loadRom(makeSequentialRom());
    const auto result = machine.runSlice({
        .maxInstructions = 10u,
        .maxCycles = 5u,
        .stopOnSegmentBoundary = false,
    });

    assert(result.progress.retiredInstructions == 2u);
    assert(result.progress.retiredCycles == 8u);
    assert(result.exitReason == BMMQ::ExecutionSliceExitReason::CycleBudget);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == 0x0102u);
}

void testControlFlowEndsSegmentedSlice()
{
    auto rom = makeSequentialRom();
    rom[0x0100u] = 0x18u; // JR -2
    rom[0x0101u] = 0xFEu;
    GB::GameBoyMachine machine;
    machine.loadRom(rom);

    const auto result = machine.runSlice({
        .maxInstructions = 10u,
        .maxCycles = 100u,
        .stopOnSegmentBoundary = true,
    });
    assert(result.progress.retiredInstructions == 1u);
    assert(result.progress.retiredCycles == 12u);
    assert(result.exitReason == BMMQ::ExecutionSliceExitReason::SegmentBoundary);
    assert(result.lastFeedback.isControlFlow);
}

void testEmptyBudgetDoesNotExecute()
{
    GB::GameBoyMachine machine;
    machine.loadRom(makeSequentialRom());
    const auto pcBefore = machine.runtimeContext().readRegister16(GB::RegisterId::PC);
    const auto result = machine.runSlice({.maxInstructions = 0u});
    assert(result.progress.retiredInstructions == 0u);
    assert(result.exitReason == BMMQ::ExecutionSliceExitReason::InstructionBudget);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == pcBefore);
}

} // namespace

int main()
{
    testSliceMatchesIndividualSteps();
    testCycleBudgetIsAnAtomicSoftCeiling();
    testControlFlowEndsSegmentedSlice();
    testEmptyBudgetDoesNotExecute();
    return 0;
}
