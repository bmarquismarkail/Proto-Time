#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"
#include "cores/gamegear/GameGearIrExecution.hpp"
#include "inst_cycle/IrExecutionService.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::abort();
    }
}

// GameGearMachine currently exposes no public reset() API. The in-slice
// loadRom() path below is therefore the strongest available machine-reset
// boundary for continuation-invalidation coverage.
std::vector<std::uint8_t> makeBaseRom() {
    return std::vector<std::uint8_t>(0x8000u, 0x00u);
}

std::unique_ptr<BMMQ::GameGearMachine> makeIrMachine() {
    auto machine = std::make_unique<BMMQ::GameGearMachine>();
    BMMQ::Plugin::PortableIrStepPolicy policy;
    machine->attachExecutorPolicy(policy);
    machine->setDetailedIrTimingEnabled(false);
    machine->loadRom(makeBaseRom());
    return machine;
}

void writeRamCode(BMMQ::GameGearMachine& machine, std::uint16_t address,
                  const std::vector<std::uint8_t>& bytes) {
    for (std::size_t i = 0u; i < bytes.size(); ++i) {
        machine.runtimeContext().write8(
            static_cast<std::uint16_t>(address + static_cast<std::uint16_t>(i)), bytes[i]);
    }
}

void assertRegistersEqual(const BMMQ::GameGearMachine& left,
                          const BMMQ::GameGearMachine& right) {
    for (const std::string_view pair : {"AF", "BC", "DE", "HL", "IX", "IY", "SP", "PC"}) {
        require(left.readRegisterPair(pair) == right.readRegisterPair(pair),
                "register mismatch");
    }
}

void assertStateEqual(const BMMQ::GameGearMachine& baseline,
                      const BMMQ::GameGearMachine& ir) {
    assertRegistersEqual(baseline, ir);
    require(baseline.recentAudioSamples() == ir.recentAudioSamples(), "audio mismatch");
    require(baseline.audioFrameCounter() == ir.audioFrameCounter(), "audio frame mismatch");
    const auto baselineVideo = baseline.videoDebugFrameModel({16, 16});
    const auto irVideo = ir.videoDebugFrameModel({16, 16});
    require(baselineVideo.has_value() == irVideo.has_value(), "video availability mismatch");
    if (baselineVideo.has_value()) {
        require(baselineVideo->width == irVideo->width, "video width mismatch");
        require(baselineVideo->height == irVideo->height, "video height mismatch");
        require(baselineVideo->argbPixels == irVideo->argbPixels, "video pixel mismatch");
    }
}

void assertFeedbackEqual(const BMMQ::GameGearMachine& baseline,
                         const BMMQ::GameGearMachine& ir) {
    const auto& baseFeedback = baseline.runtimeContext().getLastFeedback();
    const auto& irFeedback = ir.runtimeContext().getLastFeedback();
    require(baseFeedback.pcBefore == irFeedback.pcBefore, "pcBefore mismatch");
    require(baseFeedback.pcAfter == irFeedback.pcAfter, "pcAfter mismatch");
    require(baseFeedback.retiredCycles == irFeedback.retiredCycles, "retiredCycles mismatch");
    require(baseFeedback.isControlFlow == irFeedback.isControlFlow, "controlFlow mismatch");
    require(baseFeedback.segmentBoundaryHint == irFeedback.segmentBoundaryHint,
            "segmentBoundaryHint mismatch");
}

void runSlice(BMMQ::GameGearMachine& machine,
              BMMQ::InstructionRetirementSink* observer,
              BMMQ::ExecutionBudget budget) {
    const auto result = machine.runSlice(budget, observer);
    require(result.exitReason == BMMQ::ExecutionSliceExitReason::InstructionBudget,
            "slice must stop on instruction budget");
    require(result.progress.retiredInstructions == budget.maxInstructions,
            "retired instruction count must match budget");
}

void runSlice(BMMQ::GameGearMachine& machine, BMMQ::ExecutionBudget budget) {
    runSlice(machine, nullptr, budget);
}

void prepareSequentialCode(BMMQ::GameGearMachine& machine) {
    writeRamCode(machine, 0xC000u, {0x06u, 0x7Fu}); // LD B,0x7F
    writeRamCode(machine, 0xC002u, {0x04u});        // INC B
    writeRamCode(machine, 0xC003u, {0x0Cu});        // INC C
    machine.runtimeContext().writeRegister16("PC", 0xC000u);
}

} // namespace

void testSequentialRetirementsReuseOneGuardCheck()
{
    auto machine = makeIrMachine();
    prepareSequentialCode(*machine);
    const auto before = machine->irStats();

    runSlice(*machine, {.maxInstructions = 3u, .maxCycles = 1000u, .stopOnSegmentBoundary = false});
    const auto after = machine->irStats();

    require(after.dispatchAttempts - before.dispatchAttempts == 3u,
            "dispatch attempts must equal instruction count");
    require(after.translations - before.translations == 1u,
            "WP5 must translate one bounded span once");
    require(after.executions - before.executions == 3u,
            "all three instructions must retire");
    require(after.guardChecks - before.guardChecks == 1u,
            "one full guard check must serve all retirements");
    require(after.cacheReuses - before.cacheReuses == 2u,
            "remaining retirements must reuse the cached span");
}

void testObserverEarlyExitStopsBeforeNextInstruction()
{
    auto machine = makeIrMachine();
    prepareSequentialCode(*machine);
    const auto before = machine->irStats();

    class StopAfterOne final : public BMMQ::InstructionRetirementSink {
    public:
        explicit StopAfterOne() = default;
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback& feedback,
            const BMMQ::ExecutionSliceProgress& progress) override {
            ++calls;
            lastPc = feedback.pcAfter;
            return progress.retiredInstructions == 1u
                       ? BMMQ::InstructionRetirementDecision::exitSlice(
                             BMMQ::ExecutionSliceExitReason::RetirementRequested)
                       : BMMQ::InstructionRetirementDecision::continueSlice();
        }
        std::uint64_t calls = 0u;
        std::uint32_t lastPc = 0u;
    } observer;

    const auto result = machine->runSlice(
        {.maxInstructions = 4u, .maxCycles = 1000u, .stopOnSegmentBoundary = false},
        &observer);
    const auto after = machine->irStats();

    require(observer.calls == 1u, "observer must see exactly one retirement");
    require(result.progress.retiredInstructions == 1u,
            "slice must retire exactly one instruction");
    require(result.exitReason == BMMQ::ExecutionSliceExitReason::RetirementRequested,
            "exit reason must be retirement requested");
    require(observer.lastPc == 0xC002u, "observer must see the first retirement");
    require(after.executions - before.executions == 1u,
            "IR must execute exactly one instruction");
}

void testInstructionBudgetExitStopsBeforeNextInstruction()
{
    auto machine = makeIrMachine();
    prepareSequentialCode(*machine);

    const auto result = machine->runSlice(
        {.maxInstructions = 2u, .maxCycles = 1000u, .stopOnSegmentBoundary = false});

    require(result.progress.retiredInstructions == 2u,
            "slice must retire exactly the budgeted instructions");
    require(result.exitReason == BMMQ::ExecutionSliceExitReason::InstructionBudget,
            "exit reason must be instruction budget");
    require(result.lastFeedback.pcAfter == 0xC003u,
            "PC must advance exactly two instructions");
    require(machine->runtimeContext().readRegister16("PC") == 0xC003u,
            "machine PC must match final retirement");
}

void testCycleBudgetExitStopsBeforeNextInstruction()
{
    auto machine = makeIrMachine();
    prepareSequentialCode(*machine);
    const auto before = machine->irStats();

    const auto result = machine->runSlice(
        {.maxInstructions = 4u, .maxCycles = 4u, .stopOnSegmentBoundary = false});
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 1u,
            "cycle budget must stop after the first atomic retirement");
    require(result.progress.retiredCycles == 7u,
            "cycle budget must retain the first instruction's complete cycle charge");
    require(result.exitReason == BMMQ::ExecutionSliceExitReason::CycleBudget,
            "exit reason must be cycle budget");
    require(machine->runtimeContext().readRegister16("PC") == 0xC002u,
            "cycle budget must stop before the next instruction");
    require(after.executions - before.executions == 1u,
            "cycle budget must permit exactly one IR execution");
}

void testRomLoadDuringSliceClearsContinuation()
{
    auto machine = makeIrMachine();
    prepareSequentialCode(*machine);

    class LoadRomAfterOne final : public BMMQ::InstructionRetirementSink {
    public:
        explicit LoadRomAfterOne(BMMQ::GameGearMachine* target) : machine(target) {}
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback&,
            const BMMQ::ExecutionSliceProgress& progress) override {
            if (progress.retiredInstructions == 1u) {
                auto replacement = std::vector<std::uint8_t>(0x8000u, 0x00u);
                replacement[0x0000u] = 0x0Cu; // INC C
                machine->loadRom(replacement);
                machine->runtimeContext().writeRegister16("PC", 0x0000u);
            }
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }
        BMMQ::GameGearMachine* machine;
    } observer(machine.get());

    const auto before = machine->irStats();
    const auto result = machine->runSlice(
        {.maxInstructions = 2u, .maxCycles = 1000u, .stopOnSegmentBoundary = false},
        &observer);
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 2u,
            "slice must continue after ROM load");
    require(after.translations - before.translations == 2u,
            "new ROM must force a fresh translation");
    require(after.cacheReuses == before.cacheReuses,
            "old cache must not be reused after ROM load");
}

void testStateLoadDuringSliceClearsContinuation()
{
    auto machine = makeIrMachine();
    prepareSequentialCode(*machine);

    const auto pattern = (std::filesystem::temp_directory_path() /
                          "proto-time-gg-ir-continuation-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const auto* created = mkdtemp(writable.data());
    require(created != nullptr, "unable to create continuation temp directory");
    const std::filesystem::path temporaryDirectory(created);
    const auto statePath = temporaryDirectory / "before-slice.ptss";
    machine->save_state(statePath);

    class LoadStateAfterOne final : public BMMQ::InstructionRetirementSink {
    public:
        LoadStateAfterOne(BMMQ::GameGearMachine* target,
                          std::filesystem::path statePath)
            : machine(target), path(std::move(statePath)) {}
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback&,
            const BMMQ::ExecutionSliceProgress& progress) override {
            if (progress.retiredInstructions == 1u) machine->load_state(path);
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }
        BMMQ::GameGearMachine* machine;
        std::filesystem::path path;
    } observer(machine.get(), statePath);

    const auto before = machine->irStats();
    const auto result = machine->runSlice(
        {.maxInstructions = 2u, .maxCycles = 1000u, .stopOnSegmentBoundary = false},
        &observer);
    const auto after = machine->irStats();
    std::error_code cleanupError;
    std::filesystem::remove_all(temporaryDirectory, cleanupError);

    require(result.progress.retiredInstructions == 2u,
            "slice must continue after state load");
    require(machine->runtimeContext().readRegister16("PC") == 0xC002u,
            "loaded PC must retire from the restored state");
    require(after.translations - before.translations == 2u,
            "state load must force a fresh translation");
    require(after.cacheReuses == before.cacheReuses,
            "state load must not reuse the previous artifact");
}

void testIrModeToggleDuringSliceClearsContinuation()
{
    auto machine = makeIrMachine();
    prepareSequentialCode(*machine);

    class TogglePolicyAfterOne final : public BMMQ::InstructionRetirementSink {
    public:
        explicit TogglePolicyAfterOne(BMMQ::GameGearMachine* target) : machine(target) {}
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback&,
            const BMMQ::ExecutionSliceProgress& progress) override {
            if (progress.retiredInstructions == 1u) {
                BMMQ::Plugin::DefaultStepPolicy canonical;
                machine->attachExecutorPolicy(canonical);
            }
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }
        BMMQ::GameGearMachine* machine;
    } observer(machine.get());

    const auto before = machine->irStats();
    const auto result = machine->runSlice(
        {.maxInstructions = 2u, .maxCycles = 1000u, .stopOnSegmentBoundary = false},
        &observer);
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 2u,
            "slice must continue after mode toggle");
    require(after.executions - before.executions == 1u,
            "only the first instruction should execute via IR");
    require(after.translations - before.translations == 1u,
            "only the first instruction should be IR-translated");
}

void testComponentReplacementDuringSliceClearsContinuation()
{
    auto machine = makeIrMachine();
    prepareSequentialCode(*machine);

    class ReplaceComponentsAfterOne final : public BMMQ::InstructionRetirementSink {
    public:
        explicit ReplaceComponentsAfterOne(BMMQ::GameGearMachine* target) : machine(target) {}
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback&,
            const BMMQ::ExecutionSliceProgress& progress) override {
            if (progress.retiredInstructions == 1u) {
                machine->setIrComponents(nullptr, nullptr);
            }
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }
        BMMQ::GameGearMachine* machine;
    } observer(machine.get());

    const auto before = machine->irStats();
    const auto result = machine->runSlice(
        {.maxInstructions = 2u, .maxCycles = 1000u, .stopOnSegmentBoundary = false},
        &observer);
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 2u,
            "slice must continue after component replacement");
    require(after.executions - before.executions == 2u,
            "both instructions must execute via IR after component reset");
    require(after.translations - before.translations == 2u,
            "both instructions must be freshly translated after component reset");
}

void testMappingChangeDuringSliceRejectsCachedContinuation()
{
    auto machine = makeIrMachine();
    writeRamCode(*machine, 0xC000u, {0x06u, 0x7Fu}); // LD B,0x7F
    writeRamCode(*machine, 0xC002u, {0x04u});        // INC B
    writeRamCode(*machine, 0xC003u, {0x0Cu});        // INC C
    machine->runtimeContext().writeRegister16("PC", 0xC000u);

    class ChangeMappingAfterOne final : public BMMQ::InstructionRetirementSink {
    public:
        explicit ChangeMappingAfterOne(BMMQ::GameGearMachine* target) : machine(target) {}
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback&,
            const BMMQ::ExecutionSliceProgress& progress) override {
            if (progress.retiredInstructions == 1u) {
                machine->runtimeContext().write8(0xFFFCu, 0x01u);
            }
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }
        BMMQ::GameGearMachine* machine;
    } observer(machine.get());

    const auto before = machine->irStats();
    const auto result = machine->runSlice(
        {.maxInstructions = 3u, .maxCycles = 1000u, .stopOnSegmentBoundary = false},
        &observer);
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 3u,
            "slice must continue after mapping change");
    require(after.translations - before.translations >= 2u,
            "mapping change must trigger fresh translation");
    require(machine->runtimeContext().readRegister16("BC") == 0x8001u,
            "final state must match canonical execution after mapping change");
}

void testMidSliceCodeWriteRejectsContinuation()
{
    auto machine = makeIrMachine();
    writeRamCode(*machine, 0xC000u, {0x06u, 0x7Fu}); // LD B,0x7F
    writeRamCode(*machine, 0xC002u, {0x04u});        // INC B
    writeRamCode(*machine, 0xC003u, {0x0Cu});        // INC C
    machine->runtimeContext().writeRegister16("PC", 0xC000u);

    class RewriteSecondInstruction final : public BMMQ::InstructionRetirementSink {
    public:
        explicit RewriteSecondInstruction(BMMQ::GameGearMachine* target) : machine(target) {}
        BMMQ::InstructionRetirementDecision retireInstruction(
            const BMMQ::CpuFeedback&,
            const BMMQ::ExecutionSliceProgress& progress) override {
            if (progress.retiredInstructions == 1u) {
                machine->runtimeContext().write8(0xC002u, 0x03u); // INC BC (unsupported)
            }
            return BMMQ::InstructionRetirementDecision::continueSlice();
        }
        BMMQ::GameGearMachine* machine;
    } observer(machine.get());

    const auto before = machine->irStats();
    const auto result = machine->runSlice(
        {.maxInstructions = 3u, .maxCycles = 1000u, .stopOnSegmentBoundary = false},
        &observer);
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 3u,
            "all instructions must retire even after code write");
    require(after.translations - before.translations >= 2u,
            "code write must trigger fresh translation");
    require(machine->runtimeContext().readRegister16("BC") == 0x7F02u,
            "final state must match canonical execution after code write");
}

void testUnsupportedOpcodeFallsBackSafely()
{
    auto machine = makeIrMachine();
    writeRamCode(*machine, 0xC000u, {0x03u}); // INC BC - unsupported by IR
    machine->runtimeContext().writeRegister16("PC", 0xC000u);
    const auto before = machine->irStats();

    const auto result = machine->runSlice(
        {.maxInstructions = 1u, .maxCycles = 1000u, .stopOnSegmentBoundary = false});
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 1u,
            "unsupported opcode must still retire canonically");
    require(after.unsupportedFallbacks - before.unsupportedFallbacks == 1u,
            "unsupported opcode must record an unsupported fallback");
    require(after.executions - before.executions == 0u,
            "unsupported opcode must not execute via IR");
}

void testInterruptSensitiveStateFallsBackSafely()
{
    auto machine = makeIrMachine();
    writeRamCode(*machine, 0xC000u, {0xFBu, 0x04u}); // EI, INC B
    machine->runtimeContext().writeRegister16("PC", 0xC000u);
    const auto before = machine->irStats();

    const auto result = machine->runSlice(
        {.maxInstructions = 2u, .maxCycles = 1000u, .stopOnSegmentBoundary = false});
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 2u,
            "both instructions must retire");
    require(after.executions - before.executions == 0u,
            "interrupt-sensitive EI sequence must fall back for both dispatches");
    require(after.translations == before.translations,
            "interrupt-sensitive fallback must not prepare an IR artifact");
    require(after.fallbacks - before.fallbacks == 2u,
            "interrupt-sensitive sequence must classify both canonical fallbacks");
    require(machine->cpuInterruptsEnabled(),
            "deferred EI must resolve after both retirements");
}

void testPageBoundarySpanStartsFreshEntry()
{
    auto machine = makeIrMachine();
    writeRamCode(*machine, 0xC0FEu, {0x04u, 0x0Cu}); // INC B, INC C
    writeRamCode(*machine, 0xC100u, {0x48u});        // LD C,B
    machine->runtimeContext().writeRegister16("PC", 0xC0FEu);
    const auto before = machine->irStats();

    const auto result = machine->runSlice(
        {.maxInstructions = 3u, .maxCycles = 1000u, .stopOnSegmentBoundary = false});
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 3u,
            "slice must retire all three instructions");
    require(result.lastFeedback.pcAfter == 0xC101u,
            "PC must advance through the page boundary");
    require(after.translations - before.translations == 2u,
            "page boundary must start a fresh IR entry");
    require(after.guardChecks - before.guardChecks == 2u,
            "each entry must perform its own guard check");
    require(after.executions - before.executions == 3u,
            "all three instructions must execute via IR");
    require(after.cacheReuses - before.cacheReuses == 1u,
            "second instruction must reuse the first entry");
}

void testControlFlowEndsPreparedSpan()
{
    auto machine = makeIrMachine();
    writeRamCode(*machine, 0xC000u, {0x00u, 0x18u, 0xFDu}); // NOP; JR C000
    machine->runtimeContext().writeRegister16("PC", 0xC000u);
    const auto before = machine->irStats();

    const auto result = machine->runSlice(
        {.maxInstructions = 2u, .maxCycles = 1000u, .stopOnSegmentBoundary = false});
    const auto after = machine->irStats();

    require(result.lastFeedback.pcAfter == 0xC000u,
            "control flow must retire at its canonical target");
    require(after.translations - before.translations == 1u,
            "control-flow-final span must translate once");
    require(after.guardChecks - before.guardChecks == 1u,
            "control-flow-final span must use one full guard check");
    require(after.executions - before.executions == 2u,
            "both instructions must execute through IR");
    require(after.cacheReuses - before.cacheReuses == 1u,
            "control-flow instruction must reuse the prepared span");
}

void testUnsafeExecutionRegionStopsPreparedSpan()
{
    auto machine = makeIrMachine();
    writeRamCode(*machine, 0xFDFEu, {0x00u, 0x00u});
    machine->runtimeContext().writeRegister16("PC", 0xFDFEu);
    const auto before = machine->irStats();

    const auto result = machine->runSlice(
        {.maxInstructions = 3u, .maxCycles = 1000u, .stopOnSegmentBoundary = false});
    const auto after = machine->irStats();

    require(result.progress.retiredInstructions == 3u,
            "unsafe-region boundary must preserve canonical retirement");
    require(after.translations - before.translations == 1u,
            "safe prefix before unsafe execution must translate once");
    require(after.executions - before.executions == 2u,
            "unsafe execution address must not enter IR");
    require(after.fallbacks - before.fallbacks == 1u,
            "unsafe execution address must fall back once");
}

void testGuestStoreCannotContinueIntoModifiedCode()
{
    auto machine = makeIrMachine();
    writeRamCode(*machine, 0xC000u, {0x70u, 0x00u}); // LD (HL),B; NOP
    machine->runtimeContext().writeRegister16("BC", 0x0400u);
    machine->runtimeContext().writeRegister16("HL", 0xC001u);
    machine->runtimeContext().writeRegister16("PC", 0xC000u);
    const auto before = machine->irStats();

    const auto result = machine->runSlice(
        {.maxInstructions = 2u, .maxCycles = 1000u, .stopOnSegmentBoundary = false});
    const auto after = machine->irStats();

    require(result.lastFeedback.pcAfter == 0xC002u,
            "guest store sequence must retire exactly two instructions");
    require(machine->runtimeContext().readRegister16("BC") == 0x0500u,
            "modified opcode must execute instead of stale continuation");
    require(after.translations - before.translations == 2u,
            "guest store must terminate the first prepared span");
    require(after.guardChecks - before.guardChecks == 2u,
            "modified code must receive a fresh full guard check");
}

void testExactStateRemainsPreservedAgainstCanonical()
{
    BMMQ::GameGearMachine baseline;
    BMMQ::Plugin::DefaultStepPolicy canonicalPolicy;
    baseline.attachExecutorPolicy(canonicalPolicy);
    baseline.loadRom(makeBaseRom());
    prepareSequentialCode(baseline);

    auto ir = makeIrMachine();
    prepareSequentialCode(*ir);

    const BMMQ::ExecutionBudget budget{
        .maxInstructions = 3u,
        .maxCycles = 1000u,
        .stopOnSegmentBoundary = false,
    };
    runSlice(baseline, budget);
    runSlice(*ir, budget);

    assertStateEqual(baseline, *ir);
    assertFeedbackEqual(baseline, *ir);
}

int main()
{
    testSequentialRetirementsReuseOneGuardCheck();
    testObserverEarlyExitStopsBeforeNextInstruction();
    testInstructionBudgetExitStopsBeforeNextInstruction();
    testCycleBudgetExitStopsBeforeNextInstruction();
    testRomLoadDuringSliceClearsContinuation();
    testStateLoadDuringSliceClearsContinuation();
    testIrModeToggleDuringSliceClearsContinuation();
    testComponentReplacementDuringSliceClearsContinuation();
    testMappingChangeDuringSliceRejectsCachedContinuation();
    testMidSliceCodeWriteRejectsContinuation();
    testUnsupportedOpcodeFallsBackSafely();
    testInterruptSensitiveStateFallsBackSafely();
    testPageBoundarySpanStartsFreshEntry();
    testControlFlowEndsPreparedSpan();
    testUnsafeExecutionRegionStopsPreparedSpan();
    testGuestStoreCannotContinueIntoModifiedCode();
    testExactStateRemainsPreservedAgainstCanonical();
    return 0;
}
