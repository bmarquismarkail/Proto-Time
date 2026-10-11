# Acceleration experiments — 2026-10-09

**Outcome:** the tested designs do not meet the 10% threshold. Ten configurations
were screened and three confirmed with nine native repetitions. The fully combined
configuration achieves +0.65% on Game Boy but regresses 14.27% on Game Gear versus
the fastest existing paths. Correctness passes; default headless latency remains
variable (one full-suite failure followed by nine clean dedicated rounds).
No acceleration admission or default-policy promotion is justified.

The user authorized all experiments recommended by the feasibility evaluation,
including combinations where feasible. Baseline/default execution, the >=10%
requirement and the >5% repeatable-regression rule remain unchanged. Native ARM64
performance is still deferred. Changes and generated evidence remain uncommitted.

## Implemented research switches

- `lean`: fixed-size region/bank counters replace map lookups in the replay.
- `halt64`, `halt512`: the same batching applies to all four backends. Every CPU
  and hardware retirement remains; a synchronous observer stops immediately when
  CPU execution state or mapping changes. No timer/DMA/PPU/APU event is skipped.
- `reuse`: keep canonically validated bindings across mapping changes, copy/rebind
  mapping metadata and verify live code bytes before accepting the new binding.
  Lifecycle, owner, fault and writable-code checks still apply. This is validation
  reuse, not a cache keyed only by PC or a claim that arbitrary banks are immutable.
- `direct`: generated specialized entries read/write invocation-local AF/BC/DE/HL/
  SP/PC word pointers. Byte extraction is endian-independent and Game Boy flags are
  masked. Every register write is immediately visible to helpers and retirement.
  Memory/helper callbacks and all guards remain. No view is retained by a binding.
- `expanded`: an opt-in research adapter adds LD16, INC/DEC16, conditional JR, JP,
  absolute byte loads/stores, Game Boy high-I/O accesses, PUSH/POP and CALL.
  Ordinary adapters retain their original envelope. RET and prefixes remain
  baseline work; no new external IR opcode/helper ABI was introduced.
- `reuse-direct`, `lean-halt-reuse-direct`, and `all`: combinations of the above.

HALT batching is the conservative experiment from the recommendation. It does
not coalesce hardware cycles to a future deadline. No complete cross-device
next-deadline contract exists in the current stepping path; skipping polls without
that contract would bypass timer/DMA/interrupt/audio/video semantics. This series
therefore measures dispatch amortization while preserving every retirement.

## Correctness and measurement procedure

`time-measure-acceleration-experiments` shares the authored ROM replay. Each case
retains the complete 18-scenario, 273-tick timeline and exact original warmup/tick
oracle. Within each variant all existing backends use identical accounting,
HALT budget and segment inventory. Observed correctness and timings are separate.
A full run rotates nine repetitions; `--screen` uses three and `--smoke` one.
`--no-windows` is an explicitly labeled screen that retains complete tick and
endpoint comparisons while omitting the costly instruction windows. `--profile`
samples one advance in 64 and reports HALT/emitted/other category durations; those
instrumented samples are diagnostic only.

The first `all` probe failed its startup oracle because expanded blocks continued
past the software COMMIT store. Expanded inventories now end blocks at both
COMMIT bytes, preserving the original exact observation and input boundaries.
The failed log is retained; it is not performance evidence.

The collector `tools/measure_acceleration_experiments.py` binds source, actual
binaries, generated sources, manifests, cache, host/compiler/governor and commands.
It rejects missing/duplicate samples, unequal comparator configurations, missing
correctness and divergent state. It selects the fastest validated existing path
per case independently. Supplemental reports cannot grant admission or silently
replace the original acceptance corpus.

Examples:

```sh
cmake -S . -B build-working
cmake --build build-working -j4
python3 tools/measure_acceleration_experiments.py --build build-working --screen \
  --no-windows --output build-working/completion-validation/acceleration-experiments/screen.json
python3 tools/measure_acceleration_experiments.py --build build-working --variant all \
  --output build-working/completion-validation/acceleration-experiments/all-full.json
python3 tools/measure_acceleration_experiments.py --build build-working --synthetic \
  --output build-working/completion-validation/acceleration-experiments/synthetic-full.json
```

## Structural evidence

The generated RAM fixture has 28→11 static indirect call sites for Game Boy and
34→17 for Game Gear when selecting direct entries. `assembly-summary.json` and
`emitted-assembly.txt` retain the actual disassembly. Static call-site counts are
not runtime sampling proportions or speedup measurements.

Expanded block lengths reach eight instructions for the forward cases, versus
one to three in the ordinary inventory. Actual dynamic lengths/coverage and
binding/rebinding counts are recorded in each timing report. Larger blocks alone
are not assumed faster. Measured dynamic lengths rise from 1.339 to 2.715–2.870
on Game Boy and from 1.243 to 1.477–1.732 on Game Gear. Emitted retirement share
rises from 0.334% to 0.813–0.860% on Game Boy and from 0.513–0.515% to
1.081–1.247% on Game Gear; HALT retirements still dominate the denominator.

On the original inventory, reuse reduces canonical binds from 9,844 to 58 on Game
Boy and from 8,197 to 56 on Game Gear, replacing the remainder with 9,786 and
8,141 live-validated remaps respectively. The expanded combined case requires
12,694 GB and 15,970–16,345 GG remaps. Each remap still copies metadata; this
prototype removes re-lowering, not all mapping-specific construction. Expanded
coverage also increases binding traffic, so more emitted work can regress.

## Results and validation

### Synthetic direct-register experiment

Nine rotated repetitions (100,000 timed retirements after 4,096 warmup retirements),
with four-seed per-retirement differential checks, completed. Both generic and
direct generated entries were measured against all three existing paths. Results
below are elapsed-time reduction against the fastest validated existing path;
negative values mean slower.

| Core / workload | Generic emitted | Direct entry | Fastest existing |
| --- | ---: | ---: | --- |
| gameboy / compute-heavy | -13.75% | -11.72% | cached-block |
| gameboy / mixed-devices | -10.60% | -11.01% | cached-block |
| gameboy / ram-heavy | -6.56% | -9.82% | portable-ir |
| gamegear / compute-heavy | -9.99% | -9.06% | baseline |
| gamegear / mixed-devices | -3.77% | -3.44% | baseline |
| gamegear / ram-heavy | -5.47% | -6.37% | baseline |

Direct entries do not meet 10% on any synthetic case. Five of six cases also fail
the >5% repeatable-regression rule. Removing register indirection alone is
insufficient; generated assembly improvement is not throughput acceptance.

### ROM experiments

All ten configurations completed the three-repeat screen: 480 timed samples,
40 separate correctness replays, all 273 committed ticks and complete endpoints
matched in every sample. The screen omits retirement windows and is explicitly
supplemental. Combined `all` separately passed 35 windows of 128 retirements and
every committed tick on each of four cases (17,920 retirement comparisons).
Its one-repetition smoke timings are not acceptance.

| Configuration | GB forward | GB reverse | GG forward | GG reverse |
| --- | ---: | ---: | ---: | ---: |
| original | -2.21% | +1.63% | -4.27% | -5.95% |
| lean | -1.83% | -1.25% | -4.83% | -3.98% |
| halt64 | -2.24% | -1.83% | -12.17% | -8.19% |
| halt512 | +5.37% | -2.04% | -9.60% | -11.10% |
| reuse | -0.60% | -1.54% | +0.44% | -2.96% |
| direct | -1.67% | -1.54% | -5.87% | -2.87% |
| expanded | -2.22% | -4.54% | -10.53% | -9.71% |
| reuse-direct | -2.51% | -1.45% | +0.18% | -3.34% |
| lean-halt-reuse-direct | -0.08% | -1.70% | -5.36% | -9.91% |
| all | -1.76% | -1.22% | -12.81% | -15.14% |

No screened configuration reaches 10% on both cases of either core. `reuse`,
`halt512` (the largest isolated positive screen sample), and `all` were selected
for nine-repeat confirmation. Other configurations remain screening evidence,
not nine-repeat acceptance evidence. The fully combined changes make common replay
work much faster, but do not make emitted execution win over the comparators.
Variants run sequentially; cross-variant absolute timing comparisons are
exploratory and can contain host drift. Within a variant, backend order rotates.

Raw artifacts and source archive:
`build-working/completion-validation/acceleration-experiments/`.

### Nine-repeat ROM confirmation

Each confirmation includes all four cases, all four backend comparisons, 144
timed samples and tick/endpoint correctness in every sample. Retirement windows
are disabled in these confirmations and labeled in the reports; combined
retirement-window correctness is retained separately.

| Configuration | GB forward | GB reverse | GB per-core median | GG forward | GG reverse | GG per-core median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| reuse | -1.99% | -4.41% | -3.20% | -4.61% | -2.34% | -3.47% |
| halt512 | -3.81% | -1.55% | -2.68% | -10.52% | -8.40% | -9.46% |
| all | +0.20% | +1.09% | +0.65% | -14.79% | -13.75% | -14.27% |

No confirmed configuration meets 10%. `reuse` avoids the >5% ROM regression flag
but remains slower. `halt512` and `all` fail that regression rule on both Game Gear
cases. Synthetic direct entries fail it on five of six cases.


## Decision and limits

Do not promote any experimental entry or change the threshold. The strongest
combined Game Boy result is only +0.65%; Game Gear regresses 14.27% per core.
All recommended conservative prototypes were exercised, individually and in
feasible combinations. The study is a staged experiment: ten configurations were
screened with three repeats, three configurations confirmed with nine, and direct
synthetic work independently measured with nine. It is not a full nine-repeat
factorial admission run. Unfavorable screening configurations were not promoted.

Common HALT dispatch/accounting optimizations substantially reduce replay time,
but every competitor receives them. That common improvement cannot satisfy the
whole-block admission gate. Dynamic emitted blocks remain short and the active
fraction remains small. Direct registers alone yield -3.06% to +1.79% against the
generic emitted entry, far below the improvement needed against the strongest
existing backend. More emitted coverage also increases mapping-specific work.

These results reject the tested designs on this corpus; they do not prove that
10% is impossible with a different architecture. A later allocation-free binding
representation or reduced internal hook dispatch could be tested separately.
The current remap prototype still copies immutable metadata. Persistent register
caching, block chaining and event-driven device coalescing were not introduced;
they require stronger invalidation/retirement/deadline contracts. Native ARM64
performance remains deferred, and no emulated throughput is substituted for it.
The authored ROMs cover two revisions of one gameplay design; the six active
synthetic cases supplement them but do not establish arbitrary-game performance.

## Final validation

CMake configure and full `-j4` build completed; the final build has no warnings.
`git diff --check` passed. Full CTest ran outside the restricted socket sandbox:
**188/189 passed in 298.13 seconds**. All original and new acceleration,
canonical-boundary, extended-opcode, synthetic/ROM and report tests passed.
The only failure was `perf_headless_scheduling_video`: scheduling-to-present p99
31.092981 ms versus the enforced 16 ms limit. Other latency metrics in that run
passed. This is the default scheduling path, not an experimental replay setting;
no causal attribution to these experiments is established.

Nine predetermined dedicated rounds of all five discovered `performance` tests
then completed **45/45 passes**. All attempts and the full-suite failure are
retained in `performance-1.log` through `performance-9.log`, `ctest-full.log` and
`validation-summary.json`. The later passes do not erase the earlier failure or
establish clean full-suite/overall latency acceptance. No limit was relaxed.

All seven source-bound experiment reports match the final source digest
`51f8dc11a51e57b97342d3301cc1a26d1a0930ce69f12009a42fe355e6aea841`.
`tested-source.tar.gz` and `tested-source-binding.json` retain the measured source.
Builds, throughput jobs and test suites did not overlap during accepted timings.
The early combined correctness run began during the last incremental link, but
its timed samples began after the build finished. The failed COMMIT-boundary
probe is preserved separately and excluded from timing evidence.

Changes remain uncommitted. The report and raw build evidence are in ignored
`.internal/` and `build-working/` locations. Baseline policy, external IR/helper
ABI and admission thresholds were not changed. No live
presentation/audio acceptance or TSAN rerun has been performed in this series.
Machine ownership and thread handoff were not changed.

The separate `all-profile.json` samples one advance in 64. HALT represents
97.33–97.64% of sampled baseline advance time on GB and 86.09–89.08% on GG.
Candidate emitted calls represent 4.01–4.48% of sampled GB advance time and
17.92–22.66% on GG (including binding/guard/hook costs). These are one-pass
instrumented category estimates, with as few as 92 HALT samples, and exclude
outer tick/fingerprint work; they are not uninstrumented wall-time bounds or
admission evidence. The uninstrumented nine-repeat conclusions stand separately.
