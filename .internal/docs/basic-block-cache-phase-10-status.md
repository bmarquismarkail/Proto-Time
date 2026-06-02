# Phase 10: Basic Block Cache / Threaded Interpreter - Status Report

**Task ID:** t_aa7f3600  
**Status:** Complete with Phase 11 follow-up gates  
**Date:** June 2, 2026  
**Assignee:** hope

## Summary

Phase 10 has a guarded, deterministic LR3592 block-cache path over the existing fast interpreter. It does not generate native code and must not be treated as JIT/DBT infrastructure until the Phase 11 gates below are satisfied.

Baseline-faithful execution is now policy-clean: `BaselineFaithful` runs the canonical `fetch -> decode -> execute` path and does not probe or populate the block cache. Cache execution is only available when the attached executor policy advertises a non-baseline guarantee and the runtime cache switch is enabled.

## Current Implementation

Modified files:

- `inst_cycle/BlockCache.hpp`
- `cores/gameboy/gameboy.hpp`
- `cores/gameboy/gameboy.cpp`
- `cores/gameboy/GameBoyMachine.hpp`
- `tests/smoke_block_cache.cpp`

Implemented behavior:

- Guarded single-instruction cache entries keyed by PC.
- Byte-for-byte validation against the current fetched instruction bytes before fast execution.
- Runtime enable/disable through `GameBoyMachine::setBlockCacheEnabled()`.
- Full invalidation on ROM loads, ROM bank changes, boot-ROM disable, and writes into executable/code-sensitive regions.
- Range invalidation for overlapping cached blocks, using widened arithmetic to avoid 16-bit wrap in generated/larger block ranges.
- Cache hit statistics now count successful cached execution only, not lookup attempts.
- Baseline policy now disables both direct fast execution and cached fast execution.

## Smoke Coverage

`tests/smoke_block_cache.cpp` covers:

1. Basic set/get with guards.
2. Guard invalidator validity and generation.
3. Translator cache basics.
4. Capacity eviction.
5. Explicit cache statistics accounting.
6. Inclusive range invalidation.
7. Overlap invalidation, including high-address block arithmetic.
8. Cached fast-path execution under a non-baseline policy.
9. Baseline policy disabling cache execution.
10. Runtime cache disable/reenable behavior.
11. Control-flow equivalence with cache enabled.
12. Bank-switch invalidation of cached ROM window.
13. Synthetic throughput probe.
14. Generation coherency.

## Verification Status

Required normal lane:

```bash
cmake -S . -B build-working
cmake --build build-working -j4
ctest --test-dir build-working --output-on-failure
```

TSAN lane is not currently a proven gate. A focused TSAN run failed before test code with:

```text
FATAL: ThreadSanitizer: unexpected memory mapping ...
```

This is an environment/toolchain failure, not a reported data race. Before Phase 11 JIT/DBT work merges, restore a working TSAN lane or explicitly replace it with another concurrency verification path that exercises the cache, plugin policy switching, memory invalidation, and machine stepping under the same risk profile.

## Performance Evidence

Current performance evidence is synthetic only. The smoke throughput probe demonstrates that the cache path runs, but it is not a real-ROM throughput or determinism claim.

Recent synthetic probe example:

```text
disabled_ns=14824731 enabled_ns=12685104 enabled_hits=19996
```

Use this only as a path-execution check. Phase 11 must begin by establishing a deterministic trace/perf corpus with realistic ROM control flow before native code generation begins.

## Phase 11 Entry Gates

Do not start native JIT/DBT implementation until these are complete:

- Baseline-vs-cache trace comparison is clean with `BaselineFaithful` confirmed cache-free.
- Realistic ROM trace/perf corpus exists and records deterministic register, memory, feedback, and event signatures.
- TSAN is working again, or a documented replacement concurrency verification lane is accepted.
- Cache diagnostics are trusted: hits mean successful cached execution, misses mean failed/unavailable cached execution.
- Invalidation arithmetic remains safe for larger generated blocks and wrap-sensitive address ranges.
- Native translation design preserves machine-owned deterministic state; no guest CPU/APU/PPU state moves to background or callback threads.

## Phase 11 Scope Guidance

Start Phase 11 with corpus and measurement work, not codegen. Recommended order:

1. Build deterministic baseline trace fixtures for representative ROM control flow.
2. Add cache-vs-baseline trace comparison under non-baseline policy.
3. Measure real-ROM instruction throughput, cache hit rate, invalidation rate, and trace determinism.
4. Restore or replace TSAN verification.
5. Only then prototype guarded native translation behind an explicit policy/feature gate.

## Non-Goals For Phase 10

- No native code generation.
- No threaded interpreter execution model.
- No block chaining.
- No relaxation of baseline-faithful semantics.

## Conclusion

Phase 10 is a guarded fast-interpreter cache with policy-controlled execution. It is suitable as a measured stepping stone, but Phase 11 must first prove deterministic baseline-vs-optimized traces, realistic performance evidence, and a working concurrency verification lane.
