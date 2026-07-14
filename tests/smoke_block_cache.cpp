#include <iostream>
#include <string_view>
#include <cassert>
#include <array>
#include <chrono>
#include <vector>
#include "cores/gameboy/GameBoyMachine.hpp"
using GameBoyMachine = GB::GameBoyMachine;
#include "inst_cycle/BlockCache.hpp"
#include "inst_cycle/BlockTranslator.hpp"

using namespace BMMQ;

void test_basic_cache() {
    std::cout << "Test: Basic Block Cache..." << std::endl;

    BlockCache<std::uint16_t, std::vector<std::uint8_t>> cache(10);

    std::vector<std::uint8_t> data(100, 0x00);
    cache.setBlock(0x0000, data, 0xCAFEBEEF);

    auto block = cache.getBlock(0x0000);
    assert(block.has_value());
    assert(block.value().size() == 100);
    assert(block.value()[0] == 0x00);

    assert(cache.guardValid(0x0000) == true);

    cache.invalidateGuard(0x0000);
    assert(cache.guardValid(0x0000) == false);

    std::cout << "  PASSED" << std::endl;
}

void test_guard_invalidator() {
    std::cout << "Test: Guard Invalidator..." << std::endl;

    GuardInvalidator<std::uint16_t> invalidator;

    invalidator.setGuard(0x0001, 1, 42);
    assert(invalidator.guardValid(0x0001) == true);

    auto guard = invalidator.getGuard(0x0001);
    assert(guard.generation == 42);

    invalidator.invalidateGuard(0x0001);
    assert(invalidator.guardValid(0x0001) == false);

    auto guard2 = invalidator.getGuard(0x0001);
    assert(guard2.validity == GuardValidity::Invalidated);

    invalidator.setGuard(0x0002, 1, 100);
    assert(invalidator.anyGuardValid(0x0000, 0x0010) == true);

    invalidator.invalidateAll();
    assert(invalidator.guardValid(0x0001) == false);
    assert(invalidator.guardValid(0x0002) == false);

    std::cout << "  PASSED" << std::endl;
}

void test_threaded_block_cache() {
    std::cout << "Test: Threaded Block Cache..." << std::endl;

    ThreadedBlockCache<std::uint16_t, std::uint8_t> cache;
    TranslatedBlockEntry<std::uint16_t, std::uint8_t> block;
    block.start = 0x1000u;
    block.end = 0x1002u;
    block.instructions.push_back({0x1000u, {0x3Eu, 0x12u, 0x00u}, 2u});
    block.instructions.push_back({0x1002u, {0x00u, 0x00u, 0x00u}, 1u});
    cache.insert(std::move(block));

    assert(cache.lookup(0x1000u));
    const auto continuation = cache.lookup(0x1002u);
    assert(continuation);
    assert(continuation.instructionIndex == 1u);
    cache.invalidateRange(0x1001u, 0x1001u);
    assert(!cache.lookup(0x1000u));
    const auto stats = cache.stats();
    assert(stats.translations == 1u);
    assert(stats.translatedInstructions == 2u);
    assert(stats.chainContinuations == 1u);
    assert(stats.invalidations == 1u);

    std::cout << "  PASSED" << std::endl;
}

void test_cache_eviction() {
    std::cout << "Test: Cache Eviction..." << std::endl;

    BlockCache<std::uint16_t, std::vector<std::uint8_t>> cache(5);

    for (int i = 0; i < 7; i++) {
        std::vector<std::uint8_t> data(100, static_cast<uint8_t>(i));
        cache.setBlock(static_cast<std::uint16_t>(0x1000 + i), data, 0xDEADBEEF);
    }

    // With eviction before insert at capacity:
    // 0x1000 evicted on insert 0x1005
    // 0x1001 evicted on insert 0x1006
    // 0x1002, 0x1003, 0x1004, 0x1005, 0x1006 should remain
    assert(cache.contains(0x1000) == false);
    assert(cache.contains(0x1001) == false);
    assert(cache.contains(0x1002) == true);
    assert(cache.contains(0x1003) == true);
    assert(cache.contains(0x1004) == true);
    assert(cache.contains(0x1005) == true);
    assert(cache.contains(0x1006) == true);

    std::cout << "  PASSED" << std::endl;
}

void test_cache_stats() {
    std::cout << "Test: Cache Statistics..." << std::endl;

    BlockCache<std::uint16_t, std::vector<std::uint8_t>> cache(10);

    for (int i = 0; i < 5; i++) {
        std::vector<std::uint8_t> data(100, static_cast<uint8_t>(i));
        cache.setBlock(static_cast<std::uint16_t>(0x1000 + i), data, 0xDEADBEEF);
    }

    for (int i = 0; i < 5; i++) {
        assert(cache.getBlock(static_cast<std::uint16_t>(0x1000 + i)).has_value());
        cache.recordHit();
    }
    assert(!cache.getBlock(0x2000).has_value());
    cache.recordMiss();

    // Invalidate one entry
    cache.invalidateGuard(0x1000);

    auto stats = cache.getStats();
    assert(stats.hits.load() == 5u);
    assert(stats.misses.load() == 1u);
    assert(stats.invalidations.load() >= 1);

    double hitRate = stats.hitRate();
    assert(hitRate > 0.0);

    std::cout << "  PASSED (hit rate: " << hitRate << ")" << std::endl;
}

void test_range_overlap_invalidation() {
    std::cout << "Test: Range Overlap Invalidation..." << std::endl;

    BlockCache<std::uint16_t, std::vector<std::uint8_t>> cache(10);
    cache.setBlock(0xC000, std::vector<std::uint8_t>{0x3E, 0x12}, 0xDEADBEEF);
    assert(cache.guardValid(0xC000));
    cache.invalidateRange(0xC001, 0xC001);
    assert(!cache.guardValid(0xC000));

    cache.setBlock(0xFFFE, std::vector<std::uint8_t>{0x00, 0x00, 0x00, 0x00}, 0xDEADBEEF);
    assert(cache.guardValid(0xFFFE));
    cache.invalidateRange(0xFFFF, 0xFFFF);
    assert(!cache.guardValid(0xFFFE));

    std::cout << "  PASSED" << std::endl;
}

void test_gameboy_cached_fast_path_execution() {
    std::cout << "Test: Game Boy Cached Fast Path Execution..." << std::endl;

    GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy optimizedPolicy;
    machine.attachExecutorPolicy(optimizedPolicy);
    std::vector<uint8_t> rom(0x8000u, 0x00u);
    machine.loadRom(rom);

    machine.runtimeContext().write8(0xC000u, 0x3Eu);  // LD A,d8
    machine.runtimeContext().write8(0xC001u, 0x12u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
    machine.step();
    assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x12u);

    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
    machine.step();
    assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x12u);
    assert(machine.blockCacheStats().hits.load() >= 1u);

    machine.runtimeContext().write8(0xC001u, 0x34u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
    machine.step();
    assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x34u);
    assert(machine.blockCacheStats().invalidations.load() >= 1u);

    std::cout << "  PASSED" << std::endl;
}


std::vector<uint8_t> makeControlFlowRom() {
    std::vector<uint8_t> rom(0x8000u, 0x00u);
    rom[0x0100u] = 0xCDu; rom[0x0101u] = 0x10u; rom[0x0102u] = 0x01u; // CALL 0x0110
    rom[0x0103u] = 0x18u; rom[0x0104u] = 0xFBu;                         // JR 0x0100
    rom[0x0110u] = 0x87u;                                                 // ADD A,A
    rom[0x0111u] = 0xC9u;                                                 // RET
    rom[0x0120u] = 0xF3u;                                                 // DI
    rom[0x0121u] = 0xFBu;                                                 // EI
    rom[0x0122u] = 0x76u;                                                 // HALT
    return rom;
}

std::vector<uint8_t> makeMbcRom() {
    std::vector<uint8_t> rom(0x4000u * 3u, 0x00u);
    rom[0x0147u] = 0x01u; // MBC1
    rom[0x0148u] = 0x01u; // 4 ROM banks in header terms; actual fixture has 3 banks.
    rom[0x4000u] = 0x3Eu; rom[0x4001u] = 0x11u; // Bank 1: LD A,0x11
    rom[0x8000u] = 0x3Eu; rom[0x8001u] = 0x22u; // Bank 2 visible at 0x4000 after switch.
    return rom;
}

void assertMachineCoreStateEqual(const GameBoyMachine& lhs, const GameBoyMachine& rhs) {
    for (const std::string_view reg : {
             GB::RegisterId::AF,
             GB::RegisterId::BC,
             GB::RegisterId::DE,
             GB::RegisterId::HL,
             GB::RegisterId::SP,
             GB::RegisterId::PC,
         }) {
        assert(lhs.runtimeContext().readRegister16(reg) == rhs.runtimeContext().readRegister16(reg));
    }

    const auto& leftFeedback = lhs.runtimeContext().getLastFeedback();
    const auto& rightFeedback = rhs.runtimeContext().getLastFeedback();
    assert(leftFeedback.pcBefore == rightFeedback.pcBefore);
    assert(leftFeedback.pcAfter == rightFeedback.pcAfter);
    assert(leftFeedback.retiredCycles == rightFeedback.retiredCycles);

    for (uint16_t address = 0xC000u; address < 0xE000u; ++address) {
        assert(lhs.runtimeContext().peek8(address) == rhs.runtimeContext().peek8(address));
    }
    assert(lhs.audioFrameCounter() == rhs.audioFrameCounter());
    assert(lhs.recentAudioSamples() == rhs.recentAudioSamples());
    const auto leftVideo = lhs.videoStateSnapshot();
    const auto rightVideo = rhs.videoStateSnapshot();
    assert(leftVideo.has_value() == rightVideo.has_value());
    if (leftVideo.has_value()) {
        assert(leftVideo->vram == rightVideo->vram);
        assert(leftVideo->oam == rightVideo->oam);
        assert(leftVideo->lcdc == rightVideo->lcdc);
        assert(leftVideo->stat == rightVideo->stat);
        assert(leftVideo->ly == rightVideo->ly);
    }
}

void test_baseline_policy_disables_cache_execution() {
    std::cout << "Test: Baseline Policy Disables Cache Execution..." << std::endl;

    GameBoyMachine machine;
    std::vector<uint8_t> rom(0x8000u, 0x00u);
    machine.loadRom(rom);
    assert(machine.guarantee() == BMMQ::ExecutionGuarantee::BaselineFaithful);
    assert(machine.blockCacheEnabled());

    machine.runtimeContext().write8(0xC000u, 0x3Eu);
    machine.runtimeContext().write8(0xC001u, 0x12u);
    for (int i = 0; i < 3; ++i) {
        machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
        machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
        machine.step();
        assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x12u);
    }
    assert(machine.blockCacheStats().hits.load() == 0u);
    assert(machine.blockCacheStats().misses.load() == 0u);

    BMMQ::Plugin::VisibleStatePreservingStepPolicy optimizedPolicy;
    machine.attachExecutorPolicy(optimizedPolicy);
    for (int i = 0; i < 3; ++i) {
        machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
        machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
        machine.step();
        assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x12u);
    }
    assert(machine.blockCacheStats().hits.load() >= 1u);

    std::cout << "  PASSED" << std::endl;
}

void test_runtime_cache_disable() {
    std::cout << "Test: Runtime Cache Disable..." << std::endl;

    GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy optimizedPolicy;
    machine.attachExecutorPolicy(optimizedPolicy);
    std::vector<uint8_t> rom(0x8000u, 0x00u);
    machine.loadRom(rom);
    machine.setBlockCacheEnabled(false);
    assert(!machine.blockCacheEnabled());

    machine.runtimeContext().write8(0xC000u, 0x3Eu);
    machine.runtimeContext().write8(0xC001u, 0x12u);
    for (int i = 0; i < 2; ++i) {
        machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
        machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
        machine.step();
        assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x12u);
    }
    assert(machine.blockCacheStats().hits.load() == 0u);

    machine.setBlockCacheEnabled(true);
    assert(machine.blockCacheEnabled());
    for (int i = 0; i < 2; ++i) {
        machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0xC000u);
        machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
        machine.step();
        assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x12u);
    }
    assert(machine.blockCacheStats().hits.load() >= 1u);

    std::cout << "  PASSED" << std::endl;
}

void test_control_flow_equivalence_with_cache() {
    std::cout << "Test: Control Flow Equivalence With Cache..." << std::endl;

    GameBoyMachine baseline;
    GameBoyMachine cached;
    const auto rom = makeControlFlowRom();
    baseline.loadRom(rom);
    cached.loadRom(rom);
    BMMQ::Plugin::VisibleStatePreservingStepPolicy optimizedPolicy;
    cached.attachExecutorPolicy(optimizedPolicy);
    baseline.setBlockCacheEnabled(false);
    const auto capabilities = cached.runtimeContext().capabilityProfile();
    assert(capabilities.translation);
    assert(capabilities.invalidation);

    for (int stepIndex = 0; stepIndex < 12; ++stepIndex) {
        baseline.step();
        cached.step();
        assertMachineCoreStateEqual(baseline, cached);
    }
    assert(cached.blockCacheStats().hits.load() >= 4u);
    assert(cached.blockCacheStats().translations.load() > 0u);
    assert(cached.blockCacheStats().translatedInstructions.load() >
           cached.blockCacheStats().translations.load());
    assert(cached.blockCacheStats().chainContinuations.load() > 0u);

    baseline.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0120u);
    cached.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x0120u);
    for (int stepIndex = 0; stepIndex < 3; ++stepIndex) {
        baseline.step();
        cached.step();
        assertMachineCoreStateEqual(baseline, cached);
    }

    std::cout << "  PASSED" << std::endl;
}

void test_bank_switch_invalidates_cached_rom_window() {
    std::cout << "Test: Bank Switch Invalidates Cached ROM Window..." << std::endl;

    GameBoyMachine machine;
    BMMQ::Plugin::VisibleStatePreservingStepPolicy optimizedPolicy;
    machine.attachExecutorPolicy(optimizedPolicy);
    machine.loadRom(makeMbcRom());

    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x4000u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
    machine.step();
    assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x11u);

    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x4000u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
    machine.step();
    assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x11u);
    assert(machine.blockCacheStats().hits.load() >= 1u);

    const auto invalidationsBefore = machine.blockCacheStats().invalidations.load();
    machine.runtimeContext().write8(0x2000u, 0x02u);
    assert(machine.blockCacheStats().invalidations.load() > invalidationsBefore);

    machine.runtimeContext().writeRegister16(GB::RegisterId::PC, 0x4000u);
    machine.runtimeContext().writeRegister16(GB::RegisterId::AF, 0x0000u);
    machine.step();
    assert((machine.runtimeContext().readRegister16(GB::RegisterId::AF) >> 8) == 0x22u);

    std::cout << "  PASSED" << std::endl;
}

void test_cache_throughput_probe() {
    std::cout << "Test: Cache Throughput Probe..." << std::endl;

    constexpr int kSteps = 20000;
    const auto rom = makeControlFlowRom();

    auto run = [&](bool cacheEnabled) {
        GameBoyMachine machine;
        BMMQ::Plugin::VisibleStatePreservingStepPolicy optimizedPolicy;
        machine.attachExecutorPolicy(optimizedPolicy);
        machine.loadRom(rom);
        machine.setBlockCacheEnabled(cacheEnabled);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kSteps; ++i) {
            machine.step();
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::pair{
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
            machine.blockCacheStats().hits.load()
        };
    };

    const auto [disabledNs, disabledHits] = run(false);
    const auto [enabledNs, enabledHits] = run(true);
    assert(disabledNs > 0);
    assert(enabledNs > 0);
    assert(disabledHits == 0u);
    assert(enabledHits > 0u);

    std::cout << "  disabled_ns=" << disabledNs
              << " enabled_ns=" << enabledNs
              << " enabled_hits=" << enabledHits << std::endl;
    std::cout << "  PASSED" << std::endl;
}

void test_generation_coherency() {
    std::cout << "Test: Generation Coherency..." << std::endl;

    BlockCache<std::uint16_t, std::vector<std::uint8_t>> cache(10);

    std::vector<std::uint8_t> data(100, 0x00);
    cache.setBlock(0x0000, data, 0xCAFEBEEF);
    auto initialGen = cache.generation();

    cache.incrementGeneration();
    auto newGen = cache.generation();

    assert(newGen == initialGen + 1);

    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "=== Block Cache Smoke Tests ===" << std::endl << std::endl;

    test_basic_cache();
    test_guard_invalidator();
    test_threaded_block_cache();
    test_cache_eviction();
    test_cache_stats();
    test_range_overlap_invalidation();
    test_gameboy_cached_fast_path_execution();
    test_baseline_policy_disables_cache_execution();
    test_runtime_cache_disable();
    test_control_flow_equivalence_with_cache();
    test_bank_switch_invalidates_cached_rom_window();
    test_cache_throughput_probe();
    test_generation_coherency();

    std::cout << std::endl << "=== All Tests Passed ===" << std::endl;
    return 0;
}
