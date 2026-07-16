# Phase 10: Threaded Basic-Block Cache

Status: complete, including Phase 10B hardening (2026-07-15)

## Contract

The Game Boy block executor is an opt-in, visible-state-preserving CPU mode. It
does not create a host thread and does not move guest state away from the
emulation lane. Every `RuntimeContext::step()` and `Machine::step()` still
retires exactly one guest instruction, so interrupt checks and CPU/APU/PPU/DMA
retirement boundaries remain unchanged.

Select it with either:

```text
--cpu-mode block
```

or:

```ini
[emulator]
cpu_mode = block
```

`baseline` remains the default. Block mode is rejected for cores that do not
advertise its implementation.

## Implementation

- Translation starts after a supported instruction executes through the
  visible-state-preserving fast path.
- Block formation checks the same opcode-eligibility contract as cached byte
  execution. A baseline-only opcode ends the block before it is cached instead
  of becoming a predictable guard failure on first use.
- A translated entry contains up to 16 instructions or 48 bytes and stops at
  control flow, interrupt-sensitive instructions, an address-page boundary, or
  an unsupported opcode.
- Immutable entries are reached through a 65,536-slot PC table. Slots for
  later instructions in the same entry provide constant-time safe successor
  chaining without a map lookup or mutex.
- The translated hit path reuses one fetch packet and executes the cached
  instruction through the existing fast executor.
- ROM loads, boot-ROM mapping changes, mapper writes, save-state import, direct
  program loads, and overlapping guest-memory writes invalidate affected
  entries. Mapping changes advance the cache generation. The memory-map write
  observer is the single invalidation authority for committed guest writes;
  intercepted MMIO and hardware-rejected writes do not invalidate code.
- A conservative 256-page executable index rejects writes to pages with no
  cached code before scanning cache entries. Exact byte overlap remains the
  invalidation decision on pages that may contain cached code.
- HALT/STOP, interrupt entry, EI/DI, DMA restrictions, and unsupported fast
  operations return to the baseline fetch/decode path.
- `RuntimeContext` advertises translation and invalidation capabilities.

Runtime diagnostics emit a `cpu_block_cache` object with hits, misses,
translations, translated instructions, invalidations, guard failures, chained
continuations, unsupported fallbacks, fast-eligibility stops, sparse opcode
histograms for both fallback classes, write-invalidation requests, page skips,
cache scans, and blocks examined. The external-ROM corpus aggregates hit,
translation, chaining, fallback, and invalidation-work rates.

## Verification

`smoke-block-cache` compares baseline and block execution instruction by
instruction across control flow and interrupt-sensitive instructions. Its
state signature covers CPU registers and feedback, work RAM, VRAM/OAM and LCD
state, audio frame state and PCM output. It also covers self-modifying RAM,
mapper-window invalidation, policy gating, and runtime cache disable/enable.

`perf-gameboy-block-cache` is a nine-run, alternating-order paired
CPU-throughput gate over 500,000 instructions of a stable multi-instruction
arithmetic loop. The retained gate requires a 2.05x paired median and a 2.0x
lower quartile. After Phase 10B hardening it reported:

```text
baseline_median_ns=188070814 block_median_ns=88294396
paired_speedup_median=2.13092 paired_speedup_p25=2.12555
```

The deterministic 20-ROM sample retired 100,000 instructions per backend with
20 passes, zero state/cycle mismatches, and zero backend errors. Block mode had
a 1.1125x geometric-mean and 1.0714x median speedup over baseline. Its aggregate
cache diagnostics were:

- 98.12% lookup hit rate and 88.09% chained continuations per hit;
- 3.998 translated instructions per block;
- zero unsupported cached fallbacks or guard failures, down from the prior
  46.6% unsupported-fallbacks-per-translation signal;
- 285 eligibility stops, all at the `0xCB` baseline-only prefix;
- 99.897% of 311,309 write-invalidation requests rejected by the page index.

Only 321 writes reached an exact-overlap scan, so a more elaborate per-page
block list is not justified by current measurements. The whole-machine corpus
speedup remains intentionally lower than the CPU-only gate because unchanged
PPU/APU and other per-instruction device retirement is included there.

The normal completion gate is a clean configure, full build, full CTest suite,
and focused sanitizer runs. On hosts where ThreadSanitizer aborts before test
startup with `unexpected memory mapping`, record that environment limitation;
the cache itself is emulation-thread-owned and contains no synchronization.
