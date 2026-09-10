#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"

namespace {

constexpr std::uint64_t kMeasuredInstructions = 100'000u;
constexpr std::uint64_t kWarmupInstructions = 10'000u;
constexpr std::size_t kPairCount = 9u;

enum class Mode { Canonical, PortableIr };

struct RunResult {
    std::uint64_t elapsedNanos = 0u;
    std::uint64_t requestedInstructions = 0u;
    std::uint64_t retiredInstructions = 0u;
    std::uint64_t retiredCycles = 0u;
    std::uint16_t finalPc = 0u;
    BMMQ::GameGearIrStats ir{};
    std::vector<std::uint8_t> serializedState;
};

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto pattern = (base / "proto-time-gg-ir-benchmark-XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        const auto* created = mkdtemp(writable.data());
        if (created == nullptr) throw std::runtime_error("unable to create benchmark temp directory");
        path_ = created;
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        if (!path_.empty()) std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

std::vector<std::uint8_t> makeWorkload()
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[0] = 0x06u; rom[1] = 0x7Fu; // LD B,7F
    rom[2] = 0x04u;                 // INC B
    rom[3] = 0x48u;                 // LD C,B
    rom[4] = 0x81u;                 // ADD A,C
    rom[5] = 0x0Du;                 // DEC C
    rom[6] = 0xAFu;                 // XOR A
    rom[7] = 0x03u;                 // INC BC (unsupported by IR)
    rom[8] = 0x00u;                 // NOP
    rom[9] = 0x18u; rom[10] = 0xF7u; // JR 0002
    return rom;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("unable to read benchmark state");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

RunResult run(Mode mode,
              std::uint64_t instructions,
              bool detailedTiming,
              const TemporaryDirectory& temporary,
              std::string_view label)
{
    BMMQ::GameGearMachine machine;
    machine.loadRom(makeWorkload());
    if (mode == Mode::PortableIr) {
        BMMQ::Plugin::PortableIrStepPolicy policy;
        machine.attachExecutorPolicy(policy);
        machine.setDetailedIrTimingEnabled(detailedTiming);
    }

    RunResult result;
    result.requestedInstructions = instructions;
    const auto started = std::chrono::steady_clock::now();
    while (result.retiredInstructions < instructions) {
        const auto slice = machine.runSlice({.maxInstructions = std::min<std::uint64_t>(
                                                 256u, instructions - result.retiredInstructions),
                                             .stopOnSegmentBoundary = false});
        if (slice.progress.retiredInstructions == 0u)
            throw std::runtime_error("benchmark made no retirement progress");
        result.retiredInstructions += slice.progress.retiredInstructions;
        result.retiredCycles += slice.progress.retiredCycles;
    }
    result.elapsedNanos = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count());
    result.finalPc = machine.readRegisterPair("PC");
    result.ir = machine.irStats();

    const auto statePath = temporary.path() / (std::string(label) + ".ptss");
    machine.save_state(statePath);
    result.serializedState = readFile(statePath);
    if (result.retiredInstructions != result.requestedInstructions) {
        throw std::runtime_error(std::string(label) + ": requested/retired count mismatch");
    }
    return result;
}

void requireParity(const RunResult& canonical,
                   const RunResult& ir,
                   std::size_t pair)
{
    const auto prefix = "pair " + std::to_string(pair) + ": ";
    if (canonical.requestedInstructions != ir.requestedInstructions)
        throw std::runtime_error(prefix + "requested instructions differ");
    if (canonical.retiredInstructions != ir.retiredInstructions)
        throw std::runtime_error(prefix + "retired instructions differ");
    if (canonical.retiredCycles != ir.retiredCycles)
        throw std::runtime_error(prefix + "retired cycles differ");
    if (canonical.finalPc != ir.finalPc)
        throw std::runtime_error(prefix + "final PC differs");
    if (canonical.serializedState != ir.serializedState)
        throw std::runtime_error(prefix + "serialized CPU/memory/mapper/VDP/PSG state differs");
}

std::uint64_t median(std::array<std::uint64_t, kPairCount> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2u];
}

std::int64_t median(std::array<std::int64_t, kPairCount> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2u];
}

bool isIrSupported(std::uint8_t opcode) noexcept
{
    return opcode == 0x00u || opcode == 0x18u ||
           (opcode >= 0x40u && opcode <= 0xBFu && opcode != 0x76u) ||
           (opcode & 0xC7u) == 0x04u ||
           (opcode & 0xC7u) == 0x05u ||
           (opcode & 0xC7u) == 0x06u;
}

std::uint8_t workloadLength(std::uint8_t opcode) noexcept
{
    return opcode == 0x18u || (opcode & 0xC7u) == 0x06u ? 2u : 1u;
}

double eligibleRunMean()
{
    const auto rom = makeWorkload();
    std::vector<std::uint64_t> runs;
    std::uint64_t current = 0u;
    for (std::size_t pc = 2u; pc < 11u;) {
        const auto opcode = rom[pc];
        if (isIrSupported(opcode)) ++current;
        else if (current != 0u) {
            runs.push_back(current);
            current = 0u;
        }
        pc += workloadLength(opcode);
        if (opcode == 0x18u && current != 0u) {
            runs.push_back(current);
            current = 0u;
        }
    }
    if (current != 0u) runs.push_back(current);
    if (runs.empty()) return 0.0;
    return static_cast<double>(std::accumulate(runs.begin(), runs.end(), std::uint64_t{0})) /
           static_cast<double>(runs.size());
}

} // namespace

int main()
try {
    TemporaryDirectory temporary;
    (void)run(Mode::Canonical, kWarmupInstructions, false, temporary, "warmup-canonical");
    (void)run(Mode::PortableIr, kWarmupInstructions, false, temporary, "warmup-ir");

    std::array<std::uint64_t, kPairCount> canonicalTimes{};
    std::array<std::uint64_t, kPairCount> irTimes{};
    std::array<std::int64_t, kPairCount> pairedDeltas{};
    RunResult referenceCanonical;
    RunResult referenceIr;
    for (std::size_t pair = 0u; pair < kPairCount; ++pair) {
        RunResult canonical;
        RunResult ir;
        if ((pair & 1u) == 0u) {
            canonical = run(Mode::Canonical, kMeasuredInstructions, false, temporary,
                            "pair-" + std::to_string(pair) + "-canonical");
            ir = run(Mode::PortableIr, kMeasuredInstructions, false, temporary,
                     "pair-" + std::to_string(pair) + "-ir");
        } else {
            ir = run(Mode::PortableIr, kMeasuredInstructions, false, temporary,
                     "pair-" + std::to_string(pair) + "-ir");
            canonical = run(Mode::Canonical, kMeasuredInstructions, false, temporary,
                            "pair-" + std::to_string(pair) + "-canonical");
        }
        requireParity(canonical, ir, pair);
        canonicalTimes[pair] = canonical.elapsedNanos;
        irTimes[pair] = ir.elapsedNanos;
        pairedDeltas[pair] = static_cast<std::int64_t>(ir.elapsedNanos) -
                             static_cast<std::int64_t>(canonical.elapsedNanos);
        if (pair == 0u) {
            referenceCanonical = canonical;
            referenceIr = ir;
        }
        std::printf("gamegear-ir pair=%zu order=%s canonical_ns=%llu ir_ns=%llu\n",
                    pair, (pair & 1u) == 0u ? "canonical-first" : "ir-first",
                    static_cast<unsigned long long>(canonical.elapsedNanos),
                    static_cast<unsigned long long>(ir.elapsedNanos));
    }

    const auto detailed = run(Mode::PortableIr, kMeasuredInstructions, true,
                              temporary, "detailed-ir");
    requireParity(referenceCanonical, detailed, kPairCount);
    for (const auto* measured : std::array<const RunResult*, 2>{&referenceIr, &detailed}) {
        if (measured->ir.guardChecks == 0u ||
            measured->ir.executions <= measured->ir.guardChecks)
            throw std::runtime_error("benchmark did not exercise guarded continuation");
    }
    if (referenceIr.ir.dispatchAttempts != kMeasuredInstructions ||
        referenceIr.ir.dispatchAttempts !=
            referenceIr.ir.executions + referenceIr.ir.fallbacks) {
        throw std::runtime_error("counter-only dispatch classification is inconsistent");
    }
    if (referenceIr.ir.loweringNanos != 0u ||
        referenceIr.ir.guardCheckNanos != 0u ||
        referenceIr.ir.executionNanos != 0u) {
        throw std::runtime_error("counter-only benchmark recorded intrusive timing");
    }
    if (detailed.ir.loweringNanos == 0u || detailed.ir.guardCheckNanos == 0u ||
        detailed.ir.executionNanos == 0u) {
        throw std::runtime_error("detailed attribution did not record every timing domain");
    }
    const auto canonicalMedian = median(canonicalTimes);
    const auto irMedian = median(irTimes);
    const auto deltaMedian = median(pairedDeltas);
    // Only preparation and full guards are isolated here. Backend execution
    // includes guest semantics, so this is not a dispatch-overhead start gate.
    const auto attributedNanos = detailed.ir.loweringNanos + detailed.ir.guardCheckNanos;
    const auto coverage = detailed.ir.dispatchAttempts == 0u ? 0.0 :
        100.0 * static_cast<double>(detailed.ir.executions) /
        static_cast<double>(detailed.ir.dispatchAttempts);
    const auto attributedPercent = detailed.elapsedNanos == 0u ? 0.0 :
        100.0 * static_cast<double>(attributedNanos) /
        static_cast<double>(detailed.elapsedNanos);
    const auto runMean = eligibleRunMean();

    std::printf(
        "gamegear-ir summary instructions=%llu pairs=9 canonical_median_ns=%llu "
        "ir_median_ns=%llu paired_delta_median_ns=%lld dispatch_attempts=%llu "
        "coverage_percent=%.3f guard_rejects=%llu unsupported_fallbacks=%llu "
        "fallbacks=%llu translations=%llu executions=%llu cache_reuses=%llu "
        "lowering_ns=%llu guard_ns=%llu execution_ns=%llu "
        "preparation_guard_percent_of_detailed_wall=%.3f eligible_run_mean=%.3f\n",
        static_cast<unsigned long long>(kMeasuredInstructions),
        static_cast<unsigned long long>(canonicalMedian),
        static_cast<unsigned long long>(irMedian),
        static_cast<long long>(deltaMedian),
        static_cast<unsigned long long>(detailed.ir.dispatchAttempts), coverage,
        static_cast<unsigned long long>(detailed.ir.guardFailures),
        static_cast<unsigned long long>(detailed.ir.unsupportedFallbacks),
        static_cast<unsigned long long>(detailed.ir.fallbacks),
        static_cast<unsigned long long>(detailed.ir.translations),
        static_cast<unsigned long long>(detailed.ir.executions),
        static_cast<unsigned long long>(detailed.ir.cacheReuses),
        static_cast<unsigned long long>(detailed.ir.loweringNanos),
        static_cast<unsigned long long>(detailed.ir.guardCheckNanos),
        static_cast<unsigned long long>(detailed.ir.executionNanos),
        attributedPercent, runMean);
    std::printf("gamegear-ir continuation guard_checks=%llu executions=%llu "
                "detailed_wall_ns=%llu dispatch_overhead_gate=unmeasured\n",
                static_cast<unsigned long long>(detailed.ir.guardChecks),
                static_cast<unsigned long long>(detailed.ir.executions),
                static_cast<unsigned long long>(detailed.elapsedNanos));
    return EXIT_SUCCESS;
} catch (const std::exception& exception) {
    std::fprintf(stderr, "Game Gear IR benchmark failed: %s\n", exception.what());
    return EXIT_FAILURE;
}
