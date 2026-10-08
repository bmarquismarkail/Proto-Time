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
on the paused control lane. Each instruction checks PC, code bytes, mapping,
execution state and helper ABI before mutation. Execution uses the real CPU
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
