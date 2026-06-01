#include <iostream>
#include <cassert>
#include <vector>
#include "cores/gameboy/GameBoyMachine.hpp"
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

void test_block_translator() {
    std::cout << "Test: Block Translator..." << std::endl;

    BlockTranslatorImpl<std::uint16_t, std::vector<std::uint8_t>> trans;

    trans.setNativeHandler(0x00, []() { std::cout << "Handler called\n"; });
    assert(trans.canTranslate(0x00) == true);

    trans.clear();
    assert(trans.canTranslate(0x00) == false);

    std::vector<std::uint8_t> blockData;
    blockData.push_back(0x01);
    blockData.push_back(0x02);
    blockData.push_back(0x03);
    blockData.push_back(0x04);
    trans.cacheBlock(0x0010, blockData, 4);

    auto cached = trans.getCachedBlock(0x0010);
    assert(cached.has_value());
    assert(cached.value().size() == 4);

    auto stats = trans.getStats();
    assert(stats.cacheHits >= 1);

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
        cache.getBlock(static_cast<std::uint16_t>(0x1000 + i));
    }

    // Invalidate one entry
    cache.invalidateGuard(0x1000);

    auto stats = cache.getStats();
    assert(stats.hits.load() >= 5);
    assert(stats.invalidations.load() >= 1);

    double hitRate = stats.hitRate();
    assert(hitRate > 0.0);

    std::cout << "  PASSED (hit rate: " << hitRate << ")" << std::endl;
}

void test_range_invalidation() {
    std::cout << "Test: Range Invalidation..." << std::endl;

    BlockTranslatorImpl<std::uint16_t, std::vector<std::uint8_t>> trans;

    for (int i = 0; i < 10; i++) {
        std::vector<std::uint8_t> data;
        data.push_back(static_cast<uint8_t>(i));
        trans.cacheBlock(static_cast<std::uint16_t>(0x1000 + i), data, 4);
    }

    // Invalidate 0x1000 through 0x1004 (inclusive range)
    trans.invalidateRange(0x1000, 0x1004);

    // 0x1000-0x1004 should be invalidated, 0x1005 should remain
    assert(trans.getCachedBlock(0x1000) == std::nullopt);
    assert(trans.getCachedBlock(0x1005) != std::nullopt);

    std::cout << "  PASSED" << std::endl;
}

void test_range_overlap_invalidation() {
    std::cout << "Test: Range Overlap Invalidation..." << std::endl;

    BlockCache<std::uint16_t, std::vector<std::uint8_t>> cache(10);
    cache.setBlock(0xC000, std::vector<std::uint8_t>{0x3E, 0x12}, 0xDEADBEEF);
    assert(cache.guardValid(0xC000));
    cache.invalidateRange(0xC001, 0xC001);
    assert(!cache.guardValid(0xC000));

    std::cout << "  PASSED" << std::endl;
}

void test_gameboy_cached_fast_path_execution() {
    std::cout << "Test: Game Boy Cached Fast Path Execution..." << std::endl;

    GameBoyMachine machine;
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
    test_block_translator();
    test_cache_eviction();
    test_cache_stats();
    test_range_invalidation();
    test_range_overlap_invalidation();
    test_gameboy_cached_fast_path_execution();
    test_generation_coherency();

    std::cout << std::endl << "=== All Tests Passed ===" << std::endl;
    return 0;
}
