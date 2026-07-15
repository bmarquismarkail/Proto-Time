#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/RegisterId.hpp"

namespace {

std::vector<std::uint8_t> makeSequentialIrRom()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0x04u; // INC B
    rom[0x0101u] = 0x0Cu; // INC C
    rom[0x0102u] = 0x14u; // INC D
    rom[0x0103u] = 0x1Cu; // INC E
    return rom;
}

std::vector<std::uint8_t> makeIrLoopRom()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0x04u; // INC B
    rom[0x0101u] = 0x0Cu; // INC C
    rom[0x0102u] = 0x80u; // ADD A,B
    rom[0x0103u] = 0x81u; // ADD A,C
    rom[0x0104u] = 0x18u; // JR 0x0100
    rom[0x0105u] = 0xFAu;
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
    assert(baseline.recentAudioSamples() == portableIr.recentAudioSamples());
    assert(baseline.audioFrameCounter() == portableIr.audioFrameCounter());
    const auto baselineVideo = baseline.videoStateSnapshot();
    const auto irVideo = portableIr.videoStateSnapshot();
    assert(baselineVideo.has_value() && irVideo.has_value());
    assert(baselineVideo->ly == irVideo->ly);
    assert(baselineVideo->stat == irVideo->stat);
}

void enablePortableIr(GB::GameBoyMachine& machine,
                      BMMQ::Plugin::VisibleStatePreservingStepPolicy& policy)
{
    machine.attachExecutorPolicy(policy);
    machine.setBlockCacheEnabled(true);
    machine.setPortableIrEnabled(true);
}

void testBlockEntryAmortizesGuardsAcrossSynchronousRetirements()
{
    class RetirementRecorder final : public BMMQ::InstructionRetirementSink {
    public:
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback& feedback,
            const BMMQ::ExecutionSliceProgress&) override
        {
            pcs.push_back(feedback.pcAfter);
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }

        std::vector<std::uint32_t> pcs;
    } observer;

    GB::GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy policy;
    enablePortableIr(machine, policy);
    machine.loadRom(makeSequentialIrRom());

    // The cold step populates the Phase 10 block and its portable lowering.
    machine.runtimeContext().step();
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0100u);
    const auto before = machine.blockCacheStats();

    const auto result = machine.runSlice({
        .maxInstructions = 4u,
        .maxCycles = 100u,
        .stopOnSegmentBoundary = false,
    }, &observer);
    const auto after = machine.blockCacheStats();

    assert(result.progress.retiredInstructions == 4u);
    assert(observer.pcs == std::vector<std::uint32_t>({0x0101u, 0x0102u, 0x0103u, 0x0104u}));
    assert(after.irExecutions.load() - before.irExecutions.load() == 4u);
    assert(after.irGuardChecks.load() - before.irGuardChecks.load() == 1u);
    assert(after.irBlockEntries.load() - before.irBlockEntries.load() == 1u);
    assert(after.irBlockContinuations.load() - before.irBlockContinuations.load() == 3u);
}

void runRetirementWriteTest()
{
    GB::GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy policy;
    enablePortableIr(machine, policy);
    machine.loadRom(makeSequentialIrRom());
    machine.runtimeContext().write8(0xC000u, 0x00u);
    machine.runtimeContext().write8(0xC001u, 0x00u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);

    machine.runtimeContext().step();
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
    const auto before = machine.blockCacheStats();

    class RewriteNextInstruction final : public BMMQ::InstructionRetirementSink {
    public:
        explicit RewriteNextInstruction(BMMQ::RuntimeContext& context) : context_(context) {}
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback&,
            const BMMQ::ExecutionSliceProgress& progress) override
        {
            if (progress.retiredInstructions == 1u) context_.write8(0xC001u, 0x2Fu);
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }
    private:
        BMMQ::RuntimeContext& context_;
    } observer(machine.runtimeContext());

    const auto result = machine.runSlice({
        .maxInstructions = 2u,
        .maxCycles = 100u,
        .stopOnSegmentBoundary = false,
    }, &observer);
    const auto after = machine.blockCacheStats();

    assert(result.progress.retiredInstructions == 2u);
    assert(machine.runtimeContext().readRegister16(GB::RegisterId::PC) == 0xC002u);
    assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) & 0x0060u) == 0x0060u);
    assert(result.lastFeedback.executionPath != BMMQ::ExecutionPathHint::PortableIr);
    assert(after.irExecutions.load() - before.irExecutions.load() == 1u);
    assert(after.irBlockContinuationRejects.load() -
               before.irBlockContinuationRejects.load() == 1u);
}

void testBlockSliceMatchesCanonicalRetirementSequence()
{
    class FeedbackRecorder final : public BMMQ::InstructionRetirementSink {
    public:
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback& feedback,
            const BMMQ::ExecutionSliceProgress&) override
        {
            feedbacks.push_back(feedback);
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }
        std::vector<BMMQ::CpuFeedback> feedbacks;
    } baselineObserver, irObserver;

    GB::GameBoyMachine baseline;
    GB::GameBoyMachine portableIr;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy policy;
    enablePortableIr(portableIr, policy);
    const auto rom = makeIrLoopRom();
    baseline.loadRom(rom);
    baseline.setBlockCacheEnabled(false);
    portableIr.loadRom(rom);

    constexpr std::uint64_t kInstructions = 512u;
    const BMMQ::ExecutionBudget budget{
        .maxInstructions = kInstructions,
        .maxCycles = 10'000u,
        .stopOnSegmentBoundary = false,
    };
    const auto baselineResult = baseline.runSlice(budget, &baselineObserver);
    const auto irResult = portableIr.runSlice(budget, &irObserver);

    assert(baselineResult.progress.retiredInstructions == kInstructions);
    assert(irResult.progress.retiredInstructions == kInstructions);
    assert(baselineObserver.feedbacks.size() == irObserver.feedbacks.size());
    for (std::size_t index = 0u; index < baselineObserver.feedbacks.size(); ++index) {
        const auto& expected = baselineObserver.feedbacks[index];
        const auto& actual = irObserver.feedbacks[index];
        assert(expected.pcBefore == actual.pcBefore);
        assert(expected.pcAfter == actual.pcAfter);
        assert(expected.retiredCycles == actual.retiredCycles);
        assert(expected.isControlFlow == actual.isControlFlow);
        assert(expected.segmentBoundaryHint == actual.segmentBoundaryHint);
    }
    assertEquivalent(baseline, portableIr);
    const auto stats = portableIr.blockCacheStats();
    assert(stats.irBlockEntries.load() > 0u);
    assert(stats.irBlockContinuations.load() > stats.irBlockEntries.load());
}

} // namespace

int main()
{
    testBlockEntryAmortizesGuardsAcrossSynchronousRetirements();
    runRetirementWriteTest();
    testBlockSliceMatchesCanonicalRetirementSequence();
    return 0;
}
