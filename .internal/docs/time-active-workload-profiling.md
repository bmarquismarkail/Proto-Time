# Active-workload profiling

Scope: native x86-64 diagnostic attribution of the six existing synthetic active
workloads, after acceleration v2 implementation commit `60afc9f`. Prior v2
qualification remains failed and its source archives are retained. This work
introduces no admission, execution-policy, guard, or retirement changes.

## Reproduction

```sh
cmake -S . -B build-working
cmake --build build-working -j4
ctest --test-dir build-working -R 'acceleration|research|whole-block' --output-on-failure
ctest --test-dir build-working --output-on-failure
python3 tools/measure_acceleration_experiments.py --build build-working --synthetic --output build-working/completion-validation/active-profiling/control.json
python3 tools/profile_acceleration_active.py --build build-working --repetitions 3 --output build-working/completion-validation/active-profiling/phases.json
```

Do not overlap measurements with builds, tests, or each other. The normal control
contains nine rotated repetitions. The phase binary contains three rotated
repetitions across baseline, cached-block, portable IR, generic emitted code, and
direct-register emitted code. Each sample warms 4,096 instructions and measures
100,000. All declared instructions are checked to exclude HALT. Per-retirement
correctness compares registers, machine fingerprints, cycles, PC and control-flow
feedback for four seeds and all nonbaseline backends. Both conditional paths and
identical steady-state endpoints are required.

Bindings and dispatch-table construction are measured separately from steady
execution. Phase calls are counted continuously; clocks sample approximately one
in 64 calls using independent phase RNGs, with distinct repetition seeds. An
empty scope gives a clock-cost floor; it is not subtracted from measured scopes.

Phase times are inclusive and nested. Slice includes execution and retirement;
operations include register access and helpers. CPU-step covers ordinary
interpreter, cached and portable-IR execution; CPU-retirement covers the separate
emitted retirement hook. Register phases cover the IR host and direct-register
view, not all register accesses inside ordinary CPU execution. Device retirement
covers the machine hook, including device advancement and event bookkeeping.
Zero samples means unmeasured, and zero calls can mean outside the scope rather
than zero work. Binding has few calls and may have no sampled clocks; runner setup
wall time includes dispatch allocation and is not pure binding cost.

The binary, generated source, CMake cache, source digest, command, host, CPU
frequency governors, affinity and collection times bind the diagnostic results.
The collector verifies source/build/environment stability and preserves raw JSON.
Instrumented totals are not qualification throughput evidence. Synthetic active
patterns do not establish representative-ROM performance or native ARM64 timing.

## Results

Completed native x86-64 profiling on 2026-10-10, source snapshot
`f84e06ce71ee409385c004b00630e6e42200b51281072140eeb1fb562b05bf99`.
The normal and diagnostic collectors have identical source bindings. Evidence is
under `build-working/completion-validation/active-profiling/`: `control.json`
(270 samples), `phases.json` (90), `phases.raw.json`,
`control-paired-statistics.json`, `symbols.json`, and the build/test logs. The
`source-binding.json`, `profiling.patch`, and `new-source-files.tar.gz` reconstruct
the collection source from commit `60afc9f`.

Full build succeeded; 15/15 focused CTests and 193/193 full CTests passed. Full
suite duration was 354.91 seconds, including all five registered performance
checks. No new nine-round latency qualification was run: this is diagnostic
research, with no changed execution candidate. Normal measurement binaries have
no phase-ledger/TLS timing symbols; the diagnostic binary has them.

The nine-repeat uninstrumented control still regresses. This table uses paired
median improvement against the fastest existing backend selected by median time;
negative numbers mean slower. Confidence intervals use the unchanged v2 paired
median log-ratio bootstrap. This is one supplemental collection, not a new
qualification or a replacement for the two prior failed confirmation sessions.

| Core/workload | Comparator | Generic emitted gain, 95% CI | Direct gain, 95% CI |
|---|---|---|---|

| gameboy/compute-heavy | cached-block | -13.39% [-15.93, -10.90] | -10.83% [-14.84, -7.48] |
| gameboy/mixed-devices | cached-block | -10.73% [-12.51, -8.62] | -9.25% [-11.03, -7.13] |
| gameboy/ram-heavy | cached-block | -9.84% [-19.19, -7.49] | -8.03% [-9.75, -5.92] |
| gamegear/compute-heavy | baseline | -12.25% [-17.69, -9.69] | -12.35% [-45.10, -5.86] |
| gamegear/mixed-devices | baseline | -3.10% [-3.97, 0.06] | -2.83% [-5.10, -0.36] |
| gamegear/ram-heavy | baseline | -4.53% [-7.78, -2.72] | -5.15% [-7.77, -2.62] |

Generic/direct emitted code is not competitive on compute in either core. Direct
registers improve some Game Boy point estimates but do not close the gap; they do
not consistently improve Game Gear. Game Gear mixed work has no established
positive benefit, and its RAM results remain marginal or regressing. No policy
or admission changed; native ARM64 timing remains deferred.

The active-only emitted coverage is 75.7% for compute, 72.7% for RAM, and
68.4%/66.7% for Game Boy/Game Gear mixed work. Every measured sample retires
100,000 active instructions with zero HALT, zero steady binding/remapping calls,
and zero recorded metadata copies. These workload shapes remove the earlier
HALT domination. They still include explicit setup/control-flow/device fallback.

Selected generic-emitted phase estimates follow, in sampled nanoseconds per
*active instruction*. These are inclusive diagnostic estimates, not percentages
of uninstrumented time and not an additive cost breakdown.

| Core/workload | Entry guard | Continuation guard | Operations | CPU retirement | Machine/device retirement | Helper 0 |
|---|---:|---:|---:|---:|---:|---:|
| gameboy/compute-heavy | 18.0 | 28.9 | 67.8 | 137.2 | 280.8 | 5.6 |
| gameboy/mixed-devices | 14.2 | 23.3 | 68.0 | 144.0 | 304.8 | 4.9 |
| gameboy/ram-heavy | 14.9 | 24.9 | 86.7 | 175.4 | 328.7 | 6.2 |
| gamegear/compute-heavy | 25.1 | 19.9 | 49.9 | 17.0 | 42.1 | 18.3 |
| gamegear/mixed-devices | 23.2 | 18.1 | 55.0 | 15.5 | 51.9 | 16.7 |
| gamegear/ram-heavy | 25.6 | 21.8 | 80.1 | 18.3 | 49.1 | 21.1 |

The empty-scope sampled mean is 18.74 ns (15,679 clock samples). Thus tiny register
and refresh-helper timings are near the measurement floor. Clock cost remains in
each sampled scope, and nested counter/clock work perturbs parent scopes; the
estimates cannot close a wall-time budget or establish causal speedups. In
particular, summing guard, operation, register, helper, CPU-step, and retirement
rows would be invalid. Raw counts, samples, per-call estimates, and instrumented
elapsed time are retained for all five backends, including portable IR.

For Game Gear compute, entry guards have about 232 ns per invocation and
continuation guards about 31 ns per call. Together they estimate 45.1 ns per
active instruction. Machine retirement estimates 42.1 ns versus baseline 39.1 ns;
refresh-helper calls estimate about 24.2 ns each, near the 18.7 ns floor. This
makes entry-guard work and whole-block orchestration a stronger optimization lead
than refresh dispatch or another binding-copy experiment. It is a lead, not proof
that removing that estimated cost would recover the regression.

For Game Boy compute, emitted CPU retirement estimates 137.2 ns per active
instruction and machine/device retirement 280.8 ns. The cached comparator's
machine/device retirement is similar at 284.1 ns. The ABI's CPU retirement calls
`LR3592_DMG::retireInstruction`, which is also used by ordinary CPU execution:
its shared per-cycle timer, LCD, DMA, and serial work is not evidence of an
emitted-only regression. Baseline/cached CPU-step scopes include this work and
cannot be directly compared with the emitted retirement hook alone. Improving
shared hardware retirement may help every backend without closing the relative
acceleration gap. Generic emitted guards additionally estimate 46.9 ns per active
instruction; operations estimate 67.8 ns including helper/register work.

## Recommended next experiment

Test one implementation change to entry-guard byte validation and invocation
orchestration on both cores. Preserve every owner/lifecycle/mapping/helper/CPU
state check and exact code-byte comparison, observer behavior, writable-code
revalidation, deterministic retirement, and fault handling. Prefer reducing
redundant lookup/dispatch within the same validation semantics; do not waive or
batch away correctness boundaries. Use the existing fault, remapping, code-write,
observer, and per-retirement parity regressions, then the same uninstrumented
active corpus. Proceed to unchanged v2 two-session qualification only if that
candidate shows credible competitive active benefit.

Do not combine it immediately with direct-register changes, shared device/timer
optimizations, or another factorial sweep. If guard work proves insufficient,
instrument the shared Game Boy hardware retirement path at instruction scope
before changing it; keep its common cost separate from emitted-specific wrapper
cost. Independently chosen sustained-active ROMs are still required before any
default-promotion claim.

Profiling code and this report remain uncommitted after the requested initial
commit. Historical v2 evidence and decisions are unchanged.

## Commit checkpoint, 2026-10-11

The implementation, tests, collectors and these experiment reports are included
in the requested commit checkpoint. Earlier statements about uncommitted work
describe collection-time status. Generated build and raw timing artifacts remain
local and excluded from Git. Recorded hashes continue to identify the original
tested snapshots; adding these previously ignored reports changes the checkout
source digest and does not rebind historical measurements to the commit.
