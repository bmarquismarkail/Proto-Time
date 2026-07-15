#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
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
    std::uint64_t irGuardCheckNanos = 0;
    std::uint64_t irExecutionNanos = 0;
    std::uint64_t irBlockEntries = 0;
    std::uint64_t irBlockContinuations = 0;
    std::uint64_t irBlockContinuationRejects = 0;
};

enum class Mode { Baseline, Block, Ir };

class ContinueRetirementSink final : public BMMQ::InstructionRetirementSink {
public:
    BMMQ::InstructionRetirementDecision retireInstruction(
        const BMMQ::CpuFeedback&,
        const BMMQ::ExecutionSliceProgress&) override
    {
        return BMMQ::InstructionRetirementDecision::continueSlice();
    }
};

RunResult run(Mode mode, std::size_t steps)
{
    GB::GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy blockPolicy;
    machine.loadRom(makeBenchmarkRom());
    if (mode != Mode::Baseline) {
        machine.attachExecutorPolicy(blockPolicy);
        machine.setBlockCacheEnabled(true);
        machine.setPortableIrEnabled(mode == Mode::Ir);
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
    assert(warmup.progress.retiredInstructions == kWarmupSteps);

    const auto started = std::chrono::steady_clock::now();
    const auto measured = machine.runtimeContext().runSlice({
        .maxInstructions = steps,
        .stopOnSegmentBoundary = false,
    }, retirementSink);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (measured.progress.retiredInstructions != steps) {
        throw std::runtime_error("block-cache benchmark measurement ended early");
    }
    assert(measured.progress.retiredInstructions == steps);
    const auto stats = machine.blockCacheStats();
    return {
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
        stats.hits.load(),
        stats.chainContinuations.load(),
        stats.irExecutions.load(),
        stats.irFallbacks.load(),
        stats.irLoweredInstructions.load(),
        stats.irGuardChecks.load(),
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
    constexpr std::size_t kSteps = 250'000u;
    constexpr std::size_t kRuns = 5u;
    std::vector<std::int64_t> baseline;
    std::vector<std::int64_t> block;
    std::vector<std::int64_t> ir;
    std::uint64_t hits = 0;
    std::uint64_t continuations = 0;
    std::uint64_t irExecutions = 0;
    std::uint64_t irFallbacks = 0;
    std::uint64_t irLoweredInstructions = 0;
    std::uint64_t irGuardChecks = 0;
    std::uint64_t irGuardCheckNanos = 0;
    std::uint64_t irExecutionNanos = 0;
    std::uint64_t irBlockEntries = 0;
    std::uint64_t irBlockContinuations = 0;
    std::uint64_t irBlockContinuationRejects = 0;
    for (std::size_t runIndex = 0; runIndex < kRuns; ++runIndex) {
        baseline.push_back(run(Mode::Baseline, kSteps).nanoseconds);
        const auto result = run(Mode::Block, kSteps);
        block.push_back(result.nanoseconds);
        hits += result.hits;
        continuations += result.continuations;
        const auto irResult = run(Mode::Ir, kSteps);
        ir.push_back(irResult.nanoseconds);
        irExecutions += irResult.irExecutions;
        irFallbacks += irResult.irFallbacks;
        irLoweredInstructions += irResult.irLoweredInstructions;
        irGuardChecks += irResult.irGuardChecks;
        irGuardCheckNanos += irResult.irGuardCheckNanos;
        irExecutionNanos += irResult.irExecutionNanos;
        irBlockEntries += irResult.irBlockEntries;
        irBlockContinuations += irResult.irBlockContinuations;
        irBlockContinuationRejects += irResult.irBlockContinuationRejects;
    }
    std::sort(baseline.begin(), baseline.end());
    std::sort(block.begin(), block.end());
    std::sort(ir.begin(), ir.end());
    const auto baselineMedian = baseline[kRuns / 2u];
    const auto blockMedian = block[kRuns / 2u];
    const auto irMedian = ir[kRuns / 2u];
    const double speedup = static_cast<double>(baselineMedian) /
                           static_cast<double>(blockMedian);
    const double irSpeedup = static_cast<double>(baselineMedian) /
                             static_cast<double>(irMedian);
    const double irCoverage = irExecutions + irFallbacks == 0u
        ? 0.0
        : static_cast<double>(irExecutions) /
              static_cast<double>(irExecutions + irFallbacks);
    const double averageGuardNanos = irGuardChecks == 0u
        ? 0.0
        : static_cast<double>(irGuardCheckNanos) / static_cast<double>(irGuardChecks);
    const double averageExecutionNanos = irExecutions == 0u
        ? 0.0
        : static_cast<double>(irExecutionNanos) / static_cast<double>(irExecutions);

    std::cout << "gameboy_block_cache baseline_median_ns=" << baselineMedian
              << " block_median_ns=" << blockMedian
              << " speedup=" << speedup
              << " ir_median_ns=" << irMedian
              << " ir_speedup=" << irSpeedup
              << " ir_coverage=" << irCoverage
              << " ir_executions=" << irExecutions
              << " ir_fallbacks=" << irFallbacks
              << " ir_lowered_instructions=" << irLoweredInstructions
              << " ir_guard_avg_ns=" << averageGuardNanos
              << " ir_execution_avg_ns=" << averageExecutionNanos
              << " ir_block_entries=" << irBlockEntries
              << " ir_block_continuations=" << irBlockContinuations
              << " ir_block_continuation_rejects=" << irBlockContinuationRejects
              << " hits=" << hits
              << " chain_continuations=" << continuations << '\n';

    assert(hits > 0u);
    assert(continuations > 0u);
    assert(irExecutions > 0u);
    assert(irFallbacks > 0u);
    assert(irLoweredInstructions > 0u);
    assert(irGuardChecks > 0u);
    assert(irGuardCheckNanos > 0u);
    assert(irExecutionNanos > 0u);
    assert(irBlockEntries > 0u);
    assert(irBlockContinuations > 0u);
    assert(irBlockContinuations > irBlockEntries);
    assert(speedup >= 2.0);
    return 0;
}
