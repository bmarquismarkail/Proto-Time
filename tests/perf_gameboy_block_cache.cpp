#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
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
};

RunResult run(bool blockMode, std::size_t steps)
{
    GB::GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy blockPolicy;
    machine.loadRom(makeBenchmarkRom());
    if (blockMode) {
        machine.attachExecutorPolicy(blockPolicy);
        machine.setBlockCacheEnabled(true);
    } else {
        machine.setBlockCacheEnabled(false);
    }

    constexpr std::size_t kWarmupSteps = 4'096u;
    for (std::size_t index = 0; index < kWarmupSteps; ++index) {
        machine.runtimeContext().step();
    }

    const auto started = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < steps; ++index) {
        machine.runtimeContext().step();
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto stats = machine.blockCacheStats();
    return {
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
        stats.hits.load(),
        stats.chainContinuations.load(),
    };
}

} // namespace

int main()
{
    constexpr std::size_t kSteps = 250'000u;
    constexpr std::size_t kRuns = 5u;
    std::vector<std::int64_t> baseline;
    std::vector<std::int64_t> block;
    std::uint64_t hits = 0;
    std::uint64_t continuations = 0;
    for (std::size_t runIndex = 0; runIndex < kRuns; ++runIndex) {
        baseline.push_back(run(false, kSteps).nanoseconds);
        const auto result = run(true, kSteps);
        block.push_back(result.nanoseconds);
        hits += result.hits;
        continuations += result.continuations;
    }
    std::sort(baseline.begin(), baseline.end());
    std::sort(block.begin(), block.end());
    const auto baselineMedian = baseline[kRuns / 2u];
    const auto blockMedian = block[kRuns / 2u];
    const double speedup = static_cast<double>(baselineMedian) /
                           static_cast<double>(blockMedian);

    std::cout << "gameboy_block_cache baseline_median_ns=" << baselineMedian
              << " block_median_ns=" << blockMedian
              << " speedup=" << speedup
              << " hits=" << hits
              << " chain_continuations=" << continuations << '\n';

    assert(hits > 0u);
    assert(continuations > 0u);
    assert(speedup >= 2.0);
    return 0;
}
