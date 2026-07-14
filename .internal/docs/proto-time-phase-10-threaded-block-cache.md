# Phase 10: Threaded Basic-Block Cache

Status: complete (2026-07-14)

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
  entries. Mapping changes advance the cache generation.
- HALT/STOP, interrupt entry, EI/DI, DMA restrictions, and unsupported fast
  operations return to the baseline fetch/decode path.
- `RuntimeContext` advertises translation and invalidation capabilities.

Runtime diagnostics emit a `cpu_block_cache` object with hits, misses,
translations, translated instructions, invalidations, guard failures, chained
continuations, and unsupported fallbacks.

## Verification

`smoke-block-cache` compares baseline and block execution instruction by
instruction across control flow and interrupt-sensitive instructions. Its
state signature covers CPU registers and feedback, work RAM, VRAM/OAM and LCD
state, audio frame state and PCM output. It also covers self-modifying RAM,
mapper-window invalidation, policy gating, and runtime cache disable/enable.

`perf-gameboy-block-cache` is a five-run median CPU-throughput gate over a
stable multi-instruction arithmetic loop. On the Phase 10 completion host it
reported:

```text
baseline_median_ns=92245205 block_median_ns=42608645 speedup=2.16494
```

The test requires at least 2.0x, cache hits, and chained continuations. The
whole-machine probe remains intentionally lower because unchanged PPU/APU and
other per-instruction device retirement is included there.

The normal completion gate is a clean configure, full build, full CTest suite,
and focused sanitizer runs. On hosts where ThreadSanitizer aborts before test
startup with `unexpected memory mapping`, record that environment limitation;
the cache itself is emulation-thread-owned and contains no synchronization.
