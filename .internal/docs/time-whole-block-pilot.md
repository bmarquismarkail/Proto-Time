# Whole-block emission pilot, version 1

Status: emission pilot reviewed in `20b9329`, real-machine integration reviewed
in `7b8eb12`; boundary coverage and lifecycle fixes implemented, review pending. This pilot does
not close `acceleration.native` or admit another family.

The existing IR ABI executes one indexed instruction. This experiment adds a
separate internal C++ research contract in `inst_cycle/research/WholeBlock.hpp`.
A build-time emitter copies validated IR into numeric C++ expressions and emits
one function containing sequential instruction bodies. The selected compiler
produces useful arithmetic, loads, stores, and branches for the entire block;
execution performs no opcode dispatch and never returns to an interpreter
between instructions. Canonical helpers and host accesses remain calls through
`InterpreterHost`. The current external indexed ABI and production policies are
unchanged; baseline remains the default.

Compilation and metadata allocation happen during the build and artifact
construction, outside machine execution. Metadata is copied into owned immutable
storage. Code lives in the executable's RX ELF text; no runtime writable code
mapping is created. This is an ahead-of-time emission experiment, not a dynamic
compiler, and compile/invalidation cost measurements remain necessary for JIT.
Cross builds use the selected ARM64 compiler; QEMU only executes the generator
and tests. The generated source is retained in the build tree for inspection.

An invocation checks its bounded instruction/cycle budgets before entering each
instruction, then calls the owner's `before` hook. That hook must check PC,
code bytes, mapping generation, execution state, helper ABI, and lifecycle
binding. After executing an instruction, the emitted function calls `retire`
exactly once. The owner advances hardware and publishes ordered effects before
the next guard. Observer stops, control transfers, interrupts, and budgets end
execution at that boundary. Cycle budgets are soft per-instruction limits.
Exceptions propagate without retry. Both machine owners fault the research
session on an execution exception; baseline execution then rejects until ROM
reload or a validated checkpoint restore. There is no implicit interpreter fallback.

The first artifact set uses current Game Boy and Game Gear lowering for register
compute and RAM-access blocks, plus a shared conditional-transfer fixture.
Emission is limited to 16 instructions, 48 guest bytes, 16 bounded guards,
64 operations and 64 values per instruction, and 8 operands per operation.
Shifts and signed comparisons are explicitly rejected in this pilot. Expand the
emitted operation set only after the design review passes. Guest bytes never
become source text; caller identifiers are validated and operands are emitted
as numeric constants. Compiled entry/metadata pairing is trusted build output,
not an untrusted module-loading boundary or a stable external ABI.

`smoke-research-whole-block` differentially compares the generated functions with
portable IR after every instruction for 256 initial byte values across all five
artifacts. It checks registers, memory, ordered host calls, cycles, and results;
zero/soft budgets; code/mapping/lifecycle/state/helper/PC rejection; observer
stops; publication exceptions; and executable RX protections. The host model is
an IR-interface model. Its helper callbacks do **not** implement full LR35902
or Z80 semantics and its retirement hooks do **not** advance actual devices.
These tests are not the required real-core CPU/device differential evidence.

`time-smoke-research-whole-block --measure` reports nine interleaved raw samples
for 10,000 warmed whole-block invocations, using identical model hooks in both
portable and emitted paths. This isolates dispatch/emission costs; it excludes
real hardware, presentation, audio, and game corpus behavior. It cannot justify a
go or a reviewed no-go. Native ARM64 timing is still required independently of
QEMU throughput.

The separate internal `MachineBlock` binding validates the architecture and
canonical CPU lowering, then binds immutable artifact metadata to one machine
identity and its lifecycle generation. Mapping guards are rebound at construction
on the paused control lane. Each instruction checks PC, mapping, execution state
and helper ABI before mutation. Code bytes are checked at each invocation entry;
the ROM-only continuation optimization described below retains full per-instruction
byte checks for writable code and retirement observers. Execution uses the real CPU
helpers and the existing machine device-retirement path. No new production
policy is selected; this API requires an exclusive baseline machine without
capture, debugger or native trampolines.

`smoke-research-machine-block` compares both real cores against baseline after
every instruction in compute and RAM fixtures with boundary initial values.
It compares complete architectural registers, cycles, PC feedback and complete
machine fingerprints, and checks zero budgets, foreign owners, stale reload
bindings, callback failure recovery and rejected policies. These fixtures do not
establish complete opcode or device-scenario coverage. The separate
`smoke-research-machine-boundaries` check adds executable RAM with mirror writes
and guest self-modification, banked code and instructions spanning a bank
boundary, identical-byte remapping, BIOS mapping, zero-mutation guard rejection,
corrupt and successful checkpoint restores, fault recovery, mapping/restore
changes during retirement callbacks, and soft cycle budgets. It compares
architectural registers, feedback and full fingerprints at each tested native
retirement. Pending interrupts and HALT pause before native execution; baseline
then services the pending interrupt. Game Boy canonical retirement also handles
delayed EI; Game Gear pauses for its baseline deferred-EI path.

The boundary checks exposed and repair three lifecycle problems: Game Boy
mapping generations must advance even with cached dispatch disabled (an empty
cache advances its generation without rewriting the dispatch index); Game Boy
reset must initialize backing DIV alongside its already-zero CPU cache; Game
Gear ROM reload must clear the reset VDP's pending IRQ flag. No emitted opcode
support is expanded: the six additional artifacts relocate the reviewed compute
and RAM sequences. Further representative game coverage and opcode/device
scenario expansion remain necessary before an acceleration decision. No go or
no-go is recorded by this increment.

Use `python3 tools/measure_acceleration.py --build build-working --experiment
whole-block-model --output build-working/completion-validation/whole-block-pilot/
native-model.json` to bind model samples to the source, executable, compiler,
effective cache, and host details. The compatibility default remains the real
core baseline corpus. ARM64 uses the cross-build directory and the same explicit
QEMU launcher as the baseline harness; model timings remain preliminary, and
QEMU timings remain emulated throughput.

The user reported live acceptance complete for the reviewed `7b8eb12`
integration. Native ARM64 performance is deferred by user instruction. This
deferral is not a passing measurement or a reviewed no-go; the acceleration
requirement and third-family admission remain open/closed respectively.

## Multi-block real-core corpus

`time-measure-whole-block-corpus` adds three deterministic synthetic patterns per
core: compute-heavy, RAM-heavy, and mixed device accesses. Each traversal executes
four emitted blocks, guest pointer setup/advancement, a data-dependent conditional
branch, and a loop transfer. The mixed programs also access Game Boy LCD/APU/input
or Game Gear VDP/PSG/input registers/ports while the actual machine devices advance
at canonical retirement. The ROM recipe and segment map live in
`tests/WholeBlockCorpus.hpp`; the build generator emits all 24 blocks from current
canonical CPU lowering. They reuse the pilot's reviewed compute/RAM opcode set
with relocated PCs and fall-through JR destinations. No emitted opcode is added.
These programs represent execution patterns, not commercial-game captures or
complete device/interrupt scenarios.

Baseline, cached-block, portable IR, and emitted paths use the same PC-to-segment
scheduler, including its lookup overhead. Emitted execution handles declared block
entries; setup, pointer changes, conditional branches and device segments run
explicitly through baseline. An entry in the middle of a block after an instruction
budget uses baseline until the next declared entry. Artifact binding/allocation,
ROM loading, fingerprint computation and correctness observers are outside timing.
A failed emitted guard or unexpected retirement count aborts the experiment;
there is no guard-triggered retry or implicit fallback. Coverage records emitted
and non-emitted instruction counts, emitted invocation counts, and the subset of
baseline work due to partial entries. These are whole-program segmented comparisons,
not the old IR-host-model dispatch measurement or ordinary unsegmented frontend
throughput.

Before measuring each case, all three candidate paths are compared to baseline
after every retirement for four initial states (`0`, `127`, `128`, `255`) and
1,024 instructions per state/path. Checks include complete architectural registers,
full deterministic machine fingerprints, retired cycles, PC, and control/segment
feedback. Both conditional paths must be observed for every candidate. Timed runs
start fresh, warm for 4,096 instructions, then measure 100,000 instructions with
no correctness observer. Warmup fingerprints/cycles and measured registers,
fingerprints/cycles must match across every path and repetition. Nine repetitions
rotate the four execution paths. The smoke mode uses 256 differential instructions
per initial state/path, 512 measured instructions, and one repetition.

Reproduce a source/build/host-bound report with:

```bash
cmake -S . -B build-working
cmake --build build-working -j4
python3 tools/measure_acceleration.py --build build-working \
  --experiment whole-block-corpus \
  --output build-working/completion-validation/whole-block-corpus/native.json
```

Add `--smoke` for a short report. The wrapper preserves raw order/repetition data,
ROM hashes, generated-source/executable/cache hashes, source digest, compiler,
effective CMake cache, host CPU/governors, medians and variability. It rejects
concurrent source/build changes and incomplete or divergent samples. Reports show
baseline-versus-emitted results and independently select the fastest validated
existing backend per case. Any launcher is explicitly labeled emulated throughput;
it cannot establish native ARM64 performance. The user's native ARM64 performance
deferral remains in force. Latency, compilation/invalidation costs, representative
game coverage and a reviewed go/no-go remain outstanding.

### Recorded x86-64 run, 2026-10-08

The command above completed on an Intel Core i7-8650U with GCC
`16.2.1 20260810`, `RelWithDebInfo` (`-O2 -g -DNDEBUG`), and recorded `powersave`
CPU governors. The report is retained at
`build-working/completion-validation/whole-block-corpus/native.json`; it contains
216 raw timing samples and six passed differential records (73,728 candidate
retirement comparisons, with both conditional paths observed). Every warmed and
measured endpoint matched across paths and repetitions. The recorded source
digest, before this results paragraph was added, is
`f8efed64681332dbfc6d56828b2d26d67b4316a6334661622948b24e9f59c71c`.

Median elapsed times below are milliseconds for 100,000 instructions. The final
column reports emitted guest instructions as a percentage of measured work;
remaining instructions use explicit baseline segments or partial-entry handling.

| Core | Pattern | Baseline ms | Emitted ms | Fastest existing path (ms) | Emitted slowdown vs fastest | Emitted coverage |
| --- | --- | ---: | ---: | --- | ---: | ---: |
| Game Boy | compute-heavy | 107.866 | 86.242 | cached-block (65.930) | 30.81% | 75.665% |
| Game Boy | ram-heavy | 127.018 | 106.774 | portable-ir (90.128) | 18.47% | 72.714% |
| Game Boy | mixed-devices | 114.436 | 99.517 | cached-block (81.264) | 22.46% | 68.412% |
| Game Gear | compute-heavy | 14.354 | 31.168 | baseline (14.354) | 117.13% | 75.665% |
| Game Gear | ram-heavy | 16.541 | 30.066 | baseline (16.541) | 81.77% | 72.714% |
| Game Gear | mixed-devices | 19.485 | 33.099 | baseline (19.485) | 69.87% | 66.661% |

On these patterns the emitted Game Boy path reduces elapsed time versus baseline
by 13.04–20.05%, but is slower than the selected existing comparator in every
case. The Game Gear emitted path is slower than baseline in every case. Paired
elapsed-time regressions above 5% occur in eight of nine Game Boy compute samples
and nine of nine samples in the other five cases. This establishes the observed
cost of this guarded, segmented AOT path on this host; it does not isolate the
cause of the regression or establish commercial-game performance. Raw spread,
standard deviations, paired ratios and build/host bindings remain in the report.

Validation built the full tree and ran all 184 registered CTest checks: 183 passed,
including the new corpus and report-validator checks. The existing
`perf_headless_scheduling_video` check failed, then failed again in isolation:
scheduling-to-present p99 was 33.40 ms and 89.49 ms respectively, and input-response
p99 was 17.60 ms and 24.52 ms, against 16 ms limits. Logs are retained alongside
the report as `full-ctest.log` and `latency-rerun.log`. That check's source was not
changed by this increment; these observations do not assign a cause. No live
frontend or physical presentation measurement was performed. Native ARM64
performance remains deferred, and no reviewed go/no-go or admission change is
recorded.

Full-suite validation also exposed a netplay completion race: a peer could accept
the remote final packet while its own final packet was still queued, then stop
the worker and discard that outgoing packet. The CLI now waits for an atomic
worker publication barrier as well as the existing final-state acknowledgment.
The barrier records successful kernel transmission, not peer receipt or a new
wire-level acknowledgment. It is outside guest execution; packet format and
frame determinism are unchanged. Transport tests cover monotonic publication
and reset on reconnect; the two-process CLI verifies final publication.

### Approved design: ROM continuation optimization, 2026-10-08

The user approved continued acceleration development as go and deferred native
ARM64 performance. Performance acceptance remains pending. The earlier scheduling
failure above was repaired separately in `32e3b796`; see
[scheduling latency repair](scheduling-latency-repair.md).

Sampling the original emitted compute path for 20 million instructions showed
that Game Gear cartridge reads (40.67% self samples), code peeks (13.45%) and
scalar/byte guard handling (5.97%) dominated execution. Game Boy's larger costs
included hardware retirement and rendering, alongside code reads and guard checks.
The raw profiles, original executable, harness and bindings are retained under
`build-working/completion-validation/acceleration-optimization/`.

Bindings now record whether the entire validated code-byte guard lies below
`$8000`. Both current cores map ROM/BIOS in that range. With no retirement
observer, an invocation checks all bytes before its first instruction, then uses
scalar continuation guards. Each instruction still checks PC, mapping, CPU state,
helper ABI, owner/lifecycle generation and execution exclusivity; hardware advances
and retirement publishes exactly once. No validation is reused across invocations.
RAM, mirrors, Game Gear cartridge-RAM windows and blocks spanning `$8000` retain
full byte checks. Retirement observers also retain full checks because they can
perform arbitrary control-lane actions. Fetch inspection remains free of device
side effects. The external IR ABI, existing CPU interfaces, emitted opcode set and
default baseline policy are unchanged.

Additional differential checks cover all 104 unobserved instruction-budget
cutoffs across compute/RAM fixtures and four initial states on both cores. They
compare registers, full machine fingerprints, cycles and retirement feedback.
Unobserved self-modifying RAM must exit after the modifying instruction. A guest
mapper write from a ROM block must also exit before the following instruction.
Existing observed per-retirement and lifecycle/interrupt tests remain in place.

Two separate measurements are retained:

- `paired-diagnostic.json`: 36 alternating before/after process runs, three pairs
  per case, each with 4,096 warmup and 20 million measured instructions. ROM load,
  binding, process startup and shutdown are included. Instruction, cycle and
  coverage accounting agree across each pair. This diagnostic isolates the change
  against the retained original executable; it does not establish acceptance or
  independently compare complete state at its 20-million-instruction endpoint.
- `after.json`: the standard nine-repetition, 216-sample real-core corpus report,
  with all six per-retirement correctness records passing (73,728 candidate
  retirement comparisons), both conditional paths covered, and all warmup/timed
  register, fingerprint and cycle endpoints matching. It independently selects
  the fastest validated existing path in each case.

| Core | Pattern | Paired diagnostic median gain vs previous emitted | Optimized emitted ms / 100k instructions | Fastest existing path (ms) | Optimized slowdown vs fastest |
| --- | --- | ---: | ---: | --- | ---: |
| Game Boy | compute-heavy | 10.49% | 69.419 | cached-block (60.657) | 14.44% |
| Game Boy | ram-heavy | 5.97% | 89.017 | cached-block (83.942) | 6.05% |
| Game Boy | mixed-devices | 8.87% | 83.824 | portable-ir (78.064) | 7.38% |
| Game Gear | compute-heavy | 48.69% | 16.315 | baseline (14.526) | 12.31% |
| Game Gear | ram-heavy | 38.25% | 17.987 | baseline (16.392) | 9.73% |
| Game Gear | mixed-devices | 33.00% | 18.385 | baseline (17.314) | 6.18% |

All 18 before/after diagnostic pairs improved. Separate-run absolute Game Boy
medians shifted substantially alongside comparator medians, so the first
`before.json` and intermediate `after-guards.json` are retained without treating
their absolute differences as an isolated optimization result. The paired
process diagnostic and the final within-run comparator results above have
separate scopes. The intermediate run used an extra internal CPU parameter;
the final code instead reuses the existing continuation validator and preserves
that CPU interface.

The final corpus run used the Intel Core i7-8650U, GCC 16.2.1,
`RelWithDebInfo` (`-O2 -g -DNDEBUG`), and recorded powersave governors. Its tested
source digest is
`f63ccad5ef2c98497123c07a6a832847126094a1b31fc3173d7a5ed68bd9ec54`,
before this documentation update. `tested-source.tar.gz` and
`tested-source-binding.json` preserve that exact source; the reports bind executable,
generated source, effective CMake cache and host configuration. These remain
synthetic segmented fixtures. The optimized path is still slower than the fastest
existing backend in every case, so the required performance acceptance threshold
has not been met. Native ARM64 performance, representative-game measurements and
final acceptance remain outstanding; no admission change is made.

Reproduce the standard report with the command in the corpus section, changing
`--output` to the optimization artifact directory. The retained
`profile-harness.cpp` and `compare-profile-backends.py` document the separate
whole-process diagnostic. `profile-before-binding.json` identifies the original
commit/executable and sampling commands. Rebuilding that executable requires the
original checkout, not the optimized sources.

Final validation built the full native tree and ran all 184 discovered CTest
checks: **183 passed; the baseline `perf_headless_scheduling_video` gate failed**.
Scheduling-to-present p99 was 17.538 ms and input-response p99 was 16.075 ms,
against unchanged 16 ms limits. Ten isolated runs of the same unchanged fixture
passed five times and failed five times; these diagnostic reruns do not replace
the failed full-suite result. The fixture explicitly selects baseline execution,
so it does not exercise the new ROM continuation path. No cause is assigned to
this recurrence and no scheduling limit or test behavior is changed here.
`full-ctest.log` and `latency-isolated.json` preserve the results and diagnostics.

The four focused whole-block/model, real-machine, boundary and corpus smoke
checks passed under native ThreadSanitizer (194.45 s; both cores and tests were
instrumented) and ARM64 QEMU user mode (178.52 s). Their build logs, effective
flags/cache bindings and test logs are retained in the same artifact directory.
QEMU supplies correctness evidence only. Native ARM64 performance and live
presentation/audio were not rerun by this optimization increment. The failing
baseline latency gate and the remaining performance requirements keep acceptance
pending. Changes remain available for review without an admission update.
