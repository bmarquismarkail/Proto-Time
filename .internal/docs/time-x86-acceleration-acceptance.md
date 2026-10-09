# Native x86-64 acceleration acceptance

This evaluates the approved whole-block design on Linux x86-64 for both current
cores. Native ARM64 performance remains user-deferred. Baseline execution and
third-family admission are unchanged; a passing local measurement still requires
review and cannot supply missing ARM64 evidence.

The acceptance procedure is:

```sh
cmake -S . -B build-working
cmake --build build-working -j4
python3 tools/measure_x86_acceptance.py --build build-working \
  --output build-working/completion-validation/x86-64-acceptance/final
```

Run without other compilation, QEMU, sanitizer or performance jobs. The collector
records nine rotated repetitions of the fresh baseline, synthetic whole-block
and four-ROM gameplay experiments, followed by nine runs of every discovered
CTest `performance` test and the full discovered CTest suite. Raw reports and
logs remain bound to source contents, actual executables, generated code, ROM
manifest, effective cache and host configuration. Missing tools or incompatible
execution fail explicitly. No test is skipped or retried into a passing record.

The evaluator requires full native Linux x86-64 measurements. Smoke results,
launchers, sanitizer timings, changed source/build/artifacts, disabled latency
limits, incomplete baselines, missing scenarios and differential evidence cannot
establish acceptance. It independently recalculates the fastest validated
existing comparator and checks each core's median improvement across its two
authored ROM cases against the unchanged 10% requirement. An individual case is
a repeatable regression when its ratio of medians exceeds 5% slowdown or a
majority of its nine raw paired repetitions exceed 5%; synthetic cases must also
avoid these regressions. No outlier or failing latency repetition is discarded.
The existing four scheduling limits remain 16 ms and every other discovered
performance test must pass. This is the documented operational interpretation
for this measured review, not a change to the completion matrix or an expanded
claim of ROM representativeness.

Successful evaluation reports `thresholds-met-awaiting-review`, never automatic
admission. A failed threshold reports `failed` with individual reasons; missing
or incompatible evidence reports `not-run`. All three outcomes preserve the
closed admission gate. Recheck retained evidence against its exact checkout and
build with:

```sh
python3 tools/measure_x86_acceptance.py --check \
  --output build-working/completion-validation/x86-64-acceptance/final
```

For a retained measurement of an earlier source snapshot, also pass
`--source-root /absolute/path/to/exact-tested-checkout`. The source digest must
match the original report. This checks historical evidence; it does not rebind
the report to the current tree. The evaluator requires one completed result for
every currently discovered test and rejects skipped or disabled tests even when
CTest exits successfully.

## Guard optimization and diagnosis

The entry-byte validation and ROM-only continuation rules from the reviewed
backend remain intact. Each binding now derives an immutable scalar guard
summary once, preserving mapping generation, helper ABI, execution-state mask
and expected bits. Unobserved low-ROM continuations compare this summary instead
of redispatching the guard vector. Every instruction still checks PC,
owner/lifecycle, execution exclusivity and state. Entries, writable code and
retirement observers retain full byte validation; Game Gear fetch observers
still reject native execution. No opcode lowering or external ABI changes.

Tests compare summarized and ordinary guard behavior across 256 execution states,
two mapping generations and three helper versions for both architectures,
including nonzero masked expectations and malformed duplicate guards. Existing
real-core per-retirement, unobserved budget-cutoff, RAM rewrite, mapper, interrupt,
BIOS, checkpoint and lifecycle checks remain required.

The original committed executable and diagnostic harness are retained under
`build-working/completion-validation/x86-64-acceptance/`. Three timed Game Gear
forward probes measured approximately 21 ms of binding construction against
881–915 ms gameplay replays; the binding counter includes startup, so this is an
upper estimate near 2.4% of timed duration, not an isolated acceptance result.
Ten additional replays supplied a CPU sampling profile. Harness lookup/accounting,
RTTI, memory/backing inspection, ordinary slice dispatch and device retirement
remain visible alongside the native work. These diagnostics motivate guard work
but do not establish a 10% representative speedup.

## Measured result

**x86-64 performance acceptance failed.** The user-approved design go remains in
effect; this result is neither performance acceptance nor a reviewed research
no-go. Third-family admission remains closed, and native ARM64 performance is
still deferred.

The retained full measurement used commit `a428afa` plus the changes archived in
`build-working/completion-validation/x86-64-acceptance/tested-source.tar.gz`.
Its source digest is
`c5bbf02b29dbd0c81570237d1f32ffd94b90826f9cd5fec42e14f33663d00611`.
`tested-source-binding.json` identifies all 613 files; `native-build-bindings.json`
identifies the unchanged executables, generated code, ROM manifest, effective
compiler flags and cache. The runner was an Intel i7-8650U, Linux x86-64,
GCC 16.2.1, RelWithDebInfo (`-O2 -g -DNDEBUG`), with the existing `powersave`
governor. No native throughput experiment overlapped compilation or QEMU.

All nine rotated repetitions were retained: 162 fresh baseline samples, 216
synthetic samples and 144 ROM samples. Baseline was the fastest validated existing
backend for each ROM case. Positive improvement means faster emitted execution.

| Core / authored ROM case | Existing median (ms) | Emitted median (ms) | Improvement |
| --- | ---: | ---: | ---: |
| Game Boy / forward | 3128.515 | 3199.474 | -2.27% |
| Game Boy / reverse | 3079.798 | 3107.978 | -0.91% |
| Game Gear / forward | 1008.630 | 1048.576 | -3.96% |
| Game Gear / reverse | 954.605 | 986.185 | -3.31% |

The representative per-core median improvement is **-1.59% for Game Boy** and
**-3.63% for Game Gear**, below the required +10%. None of the four ROM cases
meets the repeatable-regression rule, but all six synthetic cases do: Game Boy
compute/mixed/RAM medians regress 12.26%, 11.83%, and 10.18%; Game Gear
compute/mixed/RAM medians regress 8.19%, 3.85%, and 11.15%. The Game Gear mixed
case fails because five of nine paired runs regress more than 5%.

Differential checks passed for all six synthetic cases and four ROM cases. The
ROM checks include complete 18-scenario, 273-tick timelines per case plus the
required sampled per-retirement comparisons. These are correctness and coverage
results, not evidence of a performance gain. The scalar guard optimization does
not have an isolated before/after speedup claim.

The measurement collection's full CTest run passed **186/186**, and its nine
repetitions of five discovered performance tests passed **45/45**. The largest
scheduling-to-present p99 across those repetitions was 5.603 ms; the largest
input-to-frame p99 was 15.577 ms, under the unchanged 16 ms limit.

A subsequent full suite validating the corrected reporting tools passed
**185/186** and failed `perf_headless_scheduling_video`: scheduling-to-present
p99 was **17.450 ms**, input-to-frame p99 15.585 ms. It recorded ten catch-up
clamps, 13.350 ms discarded catch-up time, a 4.965 ms maximum update gap and a
4.957 ms maximum execution batch (4.844 ms thread CPU time). This retained
failure prevents a claim of stable latency acceptance. The scheduling test
requires baseline execution; the scalar native continuation path is not active
in that test. These diagnostics establish cumulative pacing loss in this run,
not its external cause.

The original collector incorrectly counted extra concurrency gate lines as
scheduling gates. Its original log is preserved. The corrected evaluator selects
the four named scheduling gates and checks every discovered test result. It
re-evaluated the unchanged measurements against a verified extraction of the
exact tested source snapshot and reports `failed` in `final/acceptance.json`.
Later edits affect only the Python evaluator, its tests, shared validation-root
support and this results document; they do not retag the original source digest.
`reanalysis.log` and `validation.json` record that distinction and all subsequent
validation, including the failing second suite.

No live browser, GPU, controller or audible acceptance was repeated for this
change. No shared-state ownership or handoff contract changed, so TSAN was not
rerun. The final focused Python validation passed both CTest entries (eight
measurement-report tests and four acceptance tests, including failure-path
subcases). ARM64 QEMU correctness passed **2/2** focused machine-block and boundary
tests; `arm64-smoke/qemu-bindings.json` retains the actual binaries, wrappers,
runner and sysroot bindings. These checks cannot establish native ARM64 performance.
