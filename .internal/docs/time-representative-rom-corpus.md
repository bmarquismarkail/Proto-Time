# Standalone game acceleration corpus, version 1

This expands the approved whole-block research with four exact repository-owned
ROMs: the Game Boy-authored Collect and Exit game and its Game Gear port, and the
Game Gear-authored indexed/shadow-register revision and its Game Boy port.
The existing six synthetic cases remain in `whole-block-corpus`. This experiment
is `whole-block-roms`; baseline remains the default production policy.

The versioned contract is `tests/fixtures/acceleration/rom-corpus.json`. It binds
all four ROM SHA-256 values, the selected source fixtures, 18 input scenarios,
reviewed gameplay endpoints and required hardware/control regions. Each build
assembles the existing sources/assets twice with WLA-GB, WLA-Z80 and WLA-Link,
requires byte-identical ROMs matching that contract, and records tool/source,
symbol, asset and scenario identities. The build embeds this manifest and the
ROMs beside the generated native blocks. A caller cannot substitute another ROM
under the same case name. Missing tools produce a failing "not run" CTest check.

Build-time emission uses the existing built-in IR opcode envelope. WLA labels
and the fixtures' reviewed physical instruction inventories delimit entry
points, banks, code and data. Label entries and unconditional branches split
blocks; each block has at most eight instructions. There are 500 blocks across
the four ROMs, currently one to three instructions each. Indexed/shadow, stack,
absolute-memory, mapper, device and unsupported control instructions remain
explicit baseline work. There is no new CPU lowering or emitter opcode support.

All four backends use the same bank-aware static segment scheduler. PC alone
cannot distinguish the Tick and Attempt routines at logical address 4000.
Current physical backing chooses the entry; mapping generations invalidate
harness bindings. Binding and invalidation run in the paused host harness
between machine slices and are charged to timed gameplay. CPU execution-state
boundaries use explicitly counted baseline steps. A native guard failure or an
unexpected transfer fails the run; it never silently retries a guest instruction.
Guest state retains one writer. This harness creates no audio/presentation lane.

Startup, artwork upload and two committed ticks warm each fresh machine outside
timing. Each replay then follows all 18 scenarios for 273 committed ticks,
including idle, movement/animation, repeated pickups, walls, locked exit,
victory/audio notes, reset and input priority. Required executed regions cover
both bank trampolines, gameplay banks 1/2, input, rendering, audio and VBlank IRQs;
HALT polling, mapping changes and the reverse Game Gear prefix/shadow paths have
separate counters. The reviewed endpoint oracle and restored caller bank are
checked at every scenario boundary.
Instruction totals follow the existing machine retirement contract, including
HALT polling and interrupt-entry units. `haltPolls` is reported separately so
active guest execution is distinguishable from idle hardware advancement.

Correctness runs compare complete architectural registers, full machine
fingerprints and cycle/instruction totals at every committed tick against the
baseline replay. First and last active frames of each scenario also have
128-instruction windows starting at ReadInput: 35 windows, 4,480 retirements per
candidate backend per ROM. Every sampled retirement compares complete registers,
fingerprints, PC, cycles and control/segment feedback. This is sampled instruction
coverage, not an assertion that every instruction of every gameplay frame was
differentially fingerprinted. Existing exhaustive IR and research-boundary tests
remain required.

Timed runs contain no retirement observer. Nine repetitions rotate the four
backend orders. Each sample must reproduce all baseline tick states and records
emitted/non-emitted work, partial entries, CPU boundaries, bindings, mapping
changes, side exits, region/bank visits, prefixes and shadow instructions.
Timing includes the scheduler, input commits, mapping lifecycle, tick fingerprint
checks and timeline construction; it excludes startup and build-time compilation.
It is end-to-end headless gameplay-harness throughput, not isolated CPU throughput
or a frame-presentation latency measurement. The report chooses the fastest
validated existing backend separately for each ROM and preserves raw pairs.

Reproduce from the repository root:

```sh
cmake -S . -B build-working
cmake --build build-working -j4
python3 tools/measure_acceleration.py --build build-working \
  --experiment whole-block-roms \
  --output build-working/completion-validation/representative-roms/native.json
```

`--smoke` uses one timed repetition while retaining all scenarios and correctness
windows. `smoke-whole-block-roms` discovers the same short harness via CTest.
`smoke-acceleration-report-tool` rejects missing/stale contracts, ROM/input changes,
missing windows/ticks/backends, failed gameplay, divergent state, missing hardware
coverage and broken accounting. Reports bind the source, binary, CMake cache,
generated code and ROM manifest, retaining compiler/host configuration.

These are two authored game revisions and two ports of one gameplay design.
They expand real ROM coverage without establishing general game compatibility or
representativeness across genres, commercial games, BIOS/model/mapper variants,
RAM code or the full Z80 prefix space. Existing standalone independent-emulator
proofs cover these exact ROM identities; the new acceleration harness compares
execution paths within T.I.M.E. and does not replace those proofs or live video,
audio, GPU/controller/browser acceptance. Broader ROM selection remains an
explicit research scope decision.

Native ARM64 performance remains user-deferred. QEMU ARM64 checks establish guest
correctness only. The design go is approved; measured >=10% median improvement,
no repeatable regression above 5%, latency gates and native host acceptance remain
the unchanged acceleration requirements. The previously recurrent baseline
`perf_headless_scheduling_video` failure is separate from this ROM coverage work.
No admission evidence or third-family gate is changed by adding cases.

## Validation on 2026-10-08

The tested source digest is
`5a26b0a7cd0abda346cbcb3ae2a7735c005deab04a720c3ca173b98f4c2bb572`,
based on commit `b584451a85e50777da1ba552f6f6c880f041b75d` with this change
uncommitted. The preserved source archive contains all 610 Git-known source
files. Validation artifacts are under
`build-working/completion-validation/representative-roms/`; the final
`validation.json` records the documentation-only addition of these results after
testing. Compiled sources, contracts and ROMs are unchanged from the tested
snapshot. `build-bindings.json` binds both builds, flags, generated code, manifests
and executables; `artifacts-sha256.json` binds the retained logs and reports.

- Native configure and full build passed. The full discovered CTest suite passed
  **185/185**, including the six-case synthetic corpus, new ROM replay, report
  validator and existing performance gates. The report/ROM-inventory Python
  tests passed **8/8**.
- ARM64 QEMU focused validation passed **5/5**: four guest executables covering
  the ROM replay, Game Gear snapshot/capture, cartridge RAM and machine block
  boundaries, plus the host Python report validator. The full ROM replay took
  1,167.06 seconds under QEMU. This is correctness evidence; the full ARM64 suite
  and native ARM64 performance were not run in this change.
- All four ROMs completed 18 scenarios and 273 tick comparisons on each of four
  backends. The three candidate backends passed **53,760** sampled retirement
  comparisons against baseline across the corpus. Both reproducible builds
  matched the four contract ROM hashes.
- `perf_headless_scheduling_video` passed with scheduling-to-present p99
  **1.535 ms** and input-to-frame-response p99 **12.573 ms**, both below the
  existing 16 ms gates. One passing run does not establish a repair of the
  previously intermittent scheduling failure.
- No shared-state handoff or lane ownership changes were made; TSAN was not rerun.
  No new live GPU, controller, audible, browser or IDE acceptance was performed.

`native.json` contains nine rotated repetitions per case/backend, with raw
samples and complete source/build/host bindings. Measurement ran after the
compilers, QEMU and CTest had stopped, on Linux x86-64, Intel i7-8650U, GCC
16.2.1, RelWithDebInfo, with the host's existing `powersave` governor. Median
headless replay durations are seconds:

| Core / revision | Baseline | Cached block | Portable IR | Emitted | Fastest existing | Emitted improvement |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| Game Boy / forward | 2.800 | 2.907 | 2.984 | 2.876 | Baseline | -2.71% |
| Game Gear / forward | 0.984 | 1.063 | 1.052 | 1.029 | Baseline | -4.51% |
| Game Boy / reverse | 3.113 | 3.086 | 3.167 | 3.169 | Cached block | -2.67% |
| Game Gear / reverse | 0.967 | 1.033 | 1.028 | 1.003 | Baseline | -3.77% |

No case meets the required >=10% improvement. Game Gear forward also had
**6/9 paired samples** slower by more than 5%; the median comparison alone must
not be presented as a regression-gate pass. These are measurements of the
documented harness, including its checks, and do not establish a presentation
latency result or an isolated CPU speedup.

Native work accounted for **0.334%** of total Game Boy retirement units and
**0.513–0.515%** of Game Gear units. Excluding separately counted HALT polling,
the emitted share was **33.1–34.7%** on Game Boy and **25.7–26.1%** on Game Gear.
Each timed replay recorded 602 mapping changes, 9,844 Game Boy or 8,197 Game Gear
bindings, and zero unexpected/partial native side exits. The small blocks,
explicit unsupported work, idle hardware advancement and rebinding costs remain
visible in the report; extending the emitter's supported behavior requires a
separate implementation and correctness review. The approved design go and
deferred native ARM64 acceptance remain unchanged, and performance admission
remains pending.

The authored ROM inventory also exposed a shared Z80 instruction-length bug:
`2A` / `3A` absolute loads omitted their address bytes. Both the shared decoder
and reverse-fixture inventory now include those bytes; regression checks verify
plain, DD, FD and repeated-prefix identities through the real snapshot and
capture paths. The fixture ROM bytes are unchanged.
