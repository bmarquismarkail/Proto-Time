# Proto-Time Phase 11D: Measurement and Corpus Closure

## Scope

Phase 11D closes the evidence gaps left after the block-level portable backend.
It does not add opcodes, generate native code, allocate executable memory, or
change the emulation-lane ownership and synchronous retirement contracts from
11A-11C.

The completion scope is:

1. remove measurement probes that bias ordinary backend throughput;
2. make the retained Phase 10 performance gate active in release-style builds;
3. validate baseline, block, and IR against external production ROMs using a
   deterministic guest-state contract; and
4. use synthetic and realistic measurements to decide whether opcode expansion
   is justified.

## Measurement modes

IR guard checks, executions, lowerings, fallbacks, entries, and continuations
remain counted in normal execution. Per-operation `steady_clock` calls are off
by default, and their nanosecond totals remain zero. This is the unbiased mode
used by the synthetic benchmark and external-ROM corpus.

`--cpu-detailed-timing` with `--cpu-mode ir` enables the intrusive guard,
lowering, and execution timers. Diagnostics expose
`cpu_block_cache.detailed_timing_enabled` so results cannot silently mix the two
modes. The synthetic benchmark performs a separate timed pass for attribution;
that pass does not contribute to its baseline, block, or IR medians.

## Active gates

Performance and smoke targets compile with assertions enabled, but the Game Boy
block-cache benchmark no longer relies on `assert` for acceptance. It throws a
test failure when cache/IR activity is absent, detailed timing is broken, block
entry amortization is lost, or the retained Phase 10 synthetic speedup falls
below 2.0x. Consequently the gate remains active under `RelWithDebInfo` and
other configurations that define `NDEBUG`.

External-ROM host timing is informational because process scheduling and host
load make per-ROM thresholds noisy. Its hard gate is exact agreement on:

- requested retired instruction count;
- emulated cycle count;
- deterministic-state schema; and
- the final Game Boy machine-state fingerprint.

The fingerprint covers ROM identity, machine state, CPU state, memory, PPU,
APU, input, mapper, and cartridge state. Backend execution-path metadata and
host cache/profiling state are normalized or excluded.

## Corpus workflow

`tools/gameboy_corpus.py` reads loose ROMs and all supported ZIP members without
writing ROM content to results. Every backend invocation receives a separate
temporary ROM copy, which prevents adjacent battery-save writes and cross-mode
save contamination. Results include stable manifests, repeated mode medians,
per-ROM speedups, geometric-mean corpus speedups, IR dispatch coverage, and
continuations per guarded entry.

`--sample N --sample-seed <text>` selects a reproducible hash-ranked
cross-section from the entire discovered collection. `--limit N` remains useful
for fast lexically ordered smoke checks. Malformed/encrypted/oversized inputs,
duplicate ZIP member names, timeouts, nonzero exits, incomplete retirement,
missing diagnostics, and backend mismatches are explicitly classified.

## Completion measurements

On the development host, a representative unbiased synthetic run reported:

| Mode | Median | Relative to baseline |
|---|---:|---:|
| Baseline | 99.01 ms | 1.00x |
| Phase 10 block | 45.12 ms | 2.19x |
| Portable block IR | 68.11 ms | 1.45x |

The separate detailed pass measured approximately 77.8 ns per full guard check
and 244.9 ns per portable instruction. These attribution values are host- and
build-dependent and are not acceptance thresholds.

A stable-hash sample of 20 external DMG ROMs from 1,725 discovered cases ran
100,000 instructions twice in each mode (120 isolated emulator invocations):

- 20/20 matched retired instructions, cycles, and full deterministic state;
- Phase 10 block geometric-mean speedup was 1.066x (median 1.050x);
- portable IR geometric-mean speedup was 1.042x (median 1.048x);
- weighted portable dispatch coverage was 17.8%; and
- guarded entries averaged 0.42 direct continuations.

This sample is reproducible evidence, not a claim that every external ROM has
been exhaustively tested. The tool supports the complete collection and repeat
runs without placing copyrighted ROM data in the repository.

## Decision

The architecture-neutral ABI, guards, retirement boundary, and block cursor are
correctness-ready for another backend. The portable interpreter is useful as a
reference backend and now has an unbiased synthetic advantage, but the realistic
sample does not show a durable advantage large enough to justify broadening its
opcode subset. Coverage expansion remains frozen.

Any further Phase 11 work should first define or spike a W^X-safe native backend
behind the existing ABI, including executable-memory bounds, write/execute
transitions, instruction-cache synchronization, untrusted-ROM handling, and
x86-64/ARM64 support policy. That work must use the same active synthetic gate
and external-ROM state contract before opcode coverage is expanded.
