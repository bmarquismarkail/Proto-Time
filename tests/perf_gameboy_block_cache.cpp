#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/GameBoyNativeExecution.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

namespace {

std::vector<std::uint8_t> makeBenchmarkRom()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0x04u;                         // INC B
    rom[0x0101u] = 0x0Cu;                         // INC C
    rom[0x0102u] = 0x80u;                         // ADD A,B
    rom[0x0103u] = 0x81u;                         // ADD A,C
    rom[0x0104u] = 0x2Fu;                         // CPL
    rom[0x0105u] = 0x18u; rom[0x0106u] = 0xF9u;  // JR 0x0100
    return rom;
}

struct RunResult {
    std::int64_t nanoseconds = 0;
    std::uint64_t hits = 0;
    std::uint64_t continuations = 0;
    std::uint64_t irExecutions = 0;
    std::uint64_t irFallbacks = 0;
    std::uint64_t irLoweredInstructions = 0;
    std::uint64_t irGuardChecks = 0;
    std::uint64_t irLoweringNanos = 0;
    std::uint64_t irGuardCheckNanos = 0;
    std::uint64_t irExecutionNanos = 0;
    std::uint64_t irBlockEntries = 0;
    std::uint64_t irBlockContinuations = 0;
    std::uint64_t irBlockContinuationRejects = 0;
};

enum class Mode { Baseline, Block, Ir, Native };

class ContinueRetirementSink final : public BMMQ::InstructionRetirementSink {
public:
    BMMQ::InstructionRetirementDecision retireInstruction(
        const BMMQ::CpuFeedback&,
        const BMMQ::ExecutionSliceProgress&) override
    {
        return BMMQ::InstructionRetirementDecision::continueSlice();
    }
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

RunResult run(Mode mode, std::size_t steps, bool detailedTiming = false)
{
    GB::GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy blockPolicy;
    machine.loadRom(makeBenchmarkRom());
    if (mode != Mode::Baseline) {
        machine.attachExecutorPolicy(blockPolicy);
        machine.setBlockCacheEnabled(true);
        machine.setPortableIrEnabled(mode == Mode::Ir);
        machine.setNativeIrEnabled(mode == Mode::Native);
        machine.setDetailedIrTimingEnabled(
            detailedTiming && (mode == Mode::Ir || mode == Mode::Native));
    } else {
        machine.setBlockCacheEnabled(false);
    }

    ContinueRetirementSink retirementSink;
    constexpr std::size_t kWarmupSteps = 4'096u;
    const auto warmup = machine.runtimeContext().runSlice({
        .maxInstructions = kWarmupSteps,
        .stopOnSegmentBoundary = false,
    }, retirementSink);
    if (warmup.progress.retiredInstructions != kWarmupSteps) {
        throw std::runtime_error("block-cache benchmark warmup ended early");
    }

    const auto started = std::chrono::steady_clock::now();
    const auto measured = machine.runtimeContext().runSlice({
        .maxInstructions = steps,
        .stopOnSegmentBoundary = false,
    }, retirementSink);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (measured.progress.retiredInstructions != steps) {
        throw std::runtime_error("block-cache benchmark measurement ended early");
    }
    const auto stats = machine.blockCacheStats();
    return {
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
        stats.hits.load(),
        stats.chainContinuations.load(),
        stats.irExecutions.load(),
        stats.irFallbacks.load(),
        stats.irLoweredInstructions.load(),
        stats.irGuardChecks.load(),
        stats.irLoweringNanos.load(),
        stats.irGuardCheckNanos.load(),
        stats.irExecutionNanos.load(),
        stats.irBlockEntries.load(),
        stats.irBlockContinuations.load(),
        stats.irBlockContinuationRejects.load(),
    };
}

} // namespace

int main()
{
    constexpr std::size_t kSteps = 500'000u;
    constexpr std::size_t kRuns = 9u;
    constexpr double kRequiredSpeedup = 2.0;
    constexpr double kMeasurementMargin = 0.05;
    std::vector<std::int64_t> baseline;
    std::vector<std::int64_t> block;
    std::vector<std::int64_t> ir;
    std::vector<std::int64_t> native;
    std::vector<double> pairedBlockSpeedups;
    std::uint64_t hits = 0;
    std::uint64_t continuations = 0;
    std::uint64_t irExecutions = 0;
    std::uint64_t irFallbacks = 0;
    std::uint64_t irLoweredInstructions = 0;
    std::uint64_t irGuardChecks = 0;
    std::uint64_t irBlockEntries = 0;
    std::uint64_t irBlockContinuations = 0;
    std::uint64_t irBlockContinuationRejects = 0;
    std::uint64_t nativeExecutions = 0;
    const bool nativeSupported = GB::NativeExecution::supported();
    for (std::size_t runIndex = 0; runIndex < kRuns; ++runIndex) {
        RunResult baselineResult;
        RunResult result;
        // Alternate pair order to cancel first-run, thermal, and frequency
        // drift instead of comparing unrelated backend medians.
        if ((runIndex & 1u) == 0u) {
            baselineResult = run(Mode::Baseline, kSteps);
            result = run(Mode::Block, kSteps);
        } else {
            result = run(Mode::Block, kSteps);
            baselineResult = run(Mode::Baseline, kSteps);
        }
        baseline.push_back(baselineResult.nanoseconds);
        block.push_back(result.nanoseconds);
        pairedBlockSpeedups.push_back(
            static_cast<double>(baselineResult.nanoseconds) /
            static_cast<double>(result.nanoseconds));
        hits += result.hits;
        continuations += result.continuations;
        const auto irResult = run(Mode::Ir, kSteps);
        ir.push_back(irResult.nanoseconds);
        require(irResult.irLoweringNanos == 0u &&
                    irResult.irGuardCheckNanos == 0u &&
                    irResult.irExecutionNanos == 0u,
                "default IR benchmark unexpectedly enabled detailed timers");
        irExecutions += irResult.irExecutions;
        irFallbacks += irResult.irFallbacks;
        irLoweredInstructions += irResult.irLoweredInstructions;
        irGuardChecks += irResult.irGuardChecks;
        irBlockEntries += irResult.irBlockEntries;
        irBlockContinuations += irResult.irBlockContinuations;
        irBlockContinuationRejects += irResult.irBlockContinuationRejects;
        if (nativeSupported) {
            const auto nativeResult = run(Mode::Native, kSteps);
            native.push_back(nativeResult.nanoseconds);
            nativeExecutions += nativeResult.irExecutions;
        }
    }
    std::sort(baseline.begin(), baseline.end());
    std::sort(block.begin(), block.end());
    std::sort(ir.begin(), ir.end());
    std::sort(native.begin(), native.end());
    std::sort(pairedBlockSpeedups.begin(), pairedBlockSpeedups.end());
    const auto baselineMedian = baseline[kRuns / 2u];
    const auto blockMedian = block[kRuns / 2u];
    const auto irMedian = ir[kRuns / 2u];
    const auto nativeMedian = nativeSupported ? native[kRuns / 2u] : 0;
    const auto pairedSpeedupMedian = pairedBlockSpeedups[kRuns / 2u];
    const auto pairedSpeedupLowerQuartile = pairedBlockSpeedups[kRuns / 4u];
    const double speedup = static_cast<double>(baselineMedian) /
                           static_cast<double>(blockMedian);
    const double irSpeedup = static_cast<double>(baselineMedian) /
                             static_cast<double>(irMedian);
    const double nativeSpeedup = nativeSupported
        ? static_cast<double>(baselineMedian) / static_cast<double>(nativeMedian) : 0.0;
    const double nativeVsBlock = nativeSupported
        ? static_cast<double>(blockMedian) / static_cast<double>(nativeMedian) : 0.0;
    const double irCoverage = irExecutions + irFallbacks == 0u
        ? 0.0
        : static_cast<double>(irExecutions) /
              static_cast<double>(irExecutions + irFallbacks);
    // Detailed timers are an explicitly intrusive second pass and do not bias
    // the primary baseline/block/IR medians above.
    const auto detailed = run(Mode::Ir, kSteps, true);
    const double averageGuardNanos = detailed.irGuardChecks == 0u
        ? 0.0
        : static_cast<double>(detailed.irGuardCheckNanos) /
              static_cast<double>(detailed.irGuardChecks);
    const double averageExecutionNanos = detailed.irExecutions == 0u
        ? 0.0
        : static_cast<double>(detailed.irExecutionNanos) /
              static_cast<double>(detailed.irExecutions);

    std::cout << "gameboy_block_cache baseline_median_ns=" << baselineMedian
              << " block_median_ns=" << blockMedian
              << " speedup=" << speedup
              << " paired_speedup_median=" << pairedSpeedupMedian
              << " paired_speedup_p25=" << pairedSpeedupLowerQuartile
              << " required_paired_speedup="
              << (kRequiredSpeedup + kMeasurementMargin)
              << " ir_median_ns=" << irMedian
              << " ir_speedup=" << irSpeedup
              << " native_median_ns=" << nativeMedian
              << " native_speedup=" << nativeSpeedup
              << " native_vs_block=" << nativeVsBlock
              << " native_executions=" << nativeExecutions
              << " ir_coverage=" << irCoverage
              << " ir_executions=" << irExecutions
              << " ir_fallbacks=" << irFallbacks
              << " ir_lowered_instructions=" << irLoweredInstructions
              << " ir_guard_avg_ns=" << averageGuardNanos
              << " ir_execution_avg_ns=" << averageExecutionNanos
              << " ir_detailed_lowering_ns=" << detailed.irLoweringNanos
              << " ir_block_entries=" << irBlockEntries
              << " ir_block_continuations=" << irBlockContinuations
              << " ir_block_continuation_rejects=" << irBlockContinuationRejects
              << " hits=" << hits
              << " chain_continuations=" << continuations << '\n';

    require(hits > 0u, "block cache recorded no hits");
    require(continuations > 0u, "block cache recorded no continuations");
    require(irExecutions > 0u, "portable IR recorded no executions");
    require(irFallbacks > 0u, "portable IR recorded no fallbacks");
    require(irLoweredInstructions > 0u, "portable IR lowered no instructions");
    require(irGuardChecks > 0u, "portable IR recorded no guard checks");
    require(irBlockEntries > 0u, "portable IR recorded no block entries");
    require(irBlockContinuations > irBlockEntries,
            "portable IR did not amortize block entries");
    require(!nativeSupported || nativeExecutions > 0u, "native IR recorded no executions");
    require(detailed.irLoweringNanos > 0u, "detailed lowering timer recorded no time");
    require(detailed.irGuardCheckNanos > 0u, "detailed guard timer recorded no time");
    require(detailed.irExecutionNanos > 0u, "detailed execution timer recorded no time");
    require(pairedSpeedupMedian >= kRequiredSpeedup + kMeasurementMargin,
            "Phase 10 paired median did not clear the 2.0x gate with measurement margin");
    require(pairedSpeedupLowerQuartile >= kRequiredSpeedup,
            "Phase 10 paired lower quartile fell below 2.0x");
    return 0;
}
