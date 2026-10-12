# Whole-block acceleration feasibility evaluation — 2026-10-09

Evaluated checkout: `940024a` (initially clean). Runtime behavior, production
policy, threshold, and opcode coverage were not changed. Native ARM64 timing
remains deferred. This is an engineering evaluation, not a new admission result.

## Conclusion

The current whole-block path is real compiled IR work, but optimizing its emitted
instructions alone is not a credible route to 10% on the current gameplay corpus.
The decisive constraint is the tiny fraction of gameplay time reachable by that
path, compounded by short blocks and expensive rebinding. A broader execution
optimization may meet the threshold; it has not been demonstrated. Keep the
threshold and fastest-existing-backend comparison unchanged.

## Existing acceptance evidence, independently inspected

The retained nine-repetition report in
`build-working/completion-validation/x86-64-acceptance/final/` records:

| Core | Forward improvement | Reverse improvement | Per-core median |
| --- | ---: | ---: | ---: |
| Game Boy | -2.268% | -0.915% | -1.592% |
| Game Gear | -3.960% | -3.308% | -3.634% |

These are historical source/build-bound results, not fresh uninstrumented timings.
Improvement is **elapsed-time reduction**, `100 * (1 - emitted / comparator)`.
The target is emitted time <= 0.90 times the fastest validated existing time.
For the historical forward cases that requires removing about 383.8 ms from the
3,199.5 ms Game Boy candidate and 140.8 ms from the 1,048.6 ms Game Gear candidate.

Synthetic performance is a separate obstacle: Game Boy regresses 10.18–12.26%
against cached-block/portable IR despite beating its baseline by 19.10–24.89%.
Game Gear regresses 3.85–11.15% against baseline. All six synthetic cases fail
the acceptance evaluator's median-or-majority rule for repeatable >5% regressions.
Faster than baseline is therefore insufficient even for compute-heavy work.

## What is implemented

- `inst_cycle/research/WholeBlockEmitter.cpp` emits C++ operations and builds them
  into native executable text. Arithmetic is compiled directly; this removes the
  runtime IR operation switch. It is a build-time, ROM-inventory-bound experiment,
  not automatic runtime compilation of arbitrary games.
- `WholeBlock.hpp` calls a guard hook and retirement hook at every instruction.
  Register, memory and helper operations still pass through `InterpreterHost`.
  Game Boy's `WholeBlockAbiHost` adds an ABI function-pointer layer. There is no
  persistent host-register allocation across instructions or direct block chaining.
- `MachineBlock.hpp` preserves budgets, machine retirement, observers and fault
  handling. The cores retain single ownership of CPU/device state. Device
  advancement still occurs at every retirement; compiled arithmetic does not
  remove PPU/VDP, APU/PSG, interrupt or event work.
- Entry checks validate code bytes. Unobserved low-ROM continuations use immutable
  scalar guard summaries; writable or observed execution keeps full checks.
  Mapping, lifecycle, CPU state, exclusive execution and PC checks remain active.
  Removing them without an equivalent invalidation mechanism is not an optimization.
- The older `GameBoyNativeExecution.cpp` path is different: executable thunks jump
  to `executePlan`, which still loops over operations. Improving or selecting that
  path does not establish whole-block execution of the new design.

## Why blocks cannot currently pay for themselves

`tools/build_acceleration_roms.py` admits a narrow opcode envelope, splits at
labels/unsupported operations, ends at JR and caps segments at eight instructions.
The present game blocks are only one to three instructions. Conditional branches,
stack operations, absolute/device accesses and indexed/shadow paths remain baseline.

The retained unobserved gameplay accounting is:

| Core / case | HALT polling share of retirements | Emitted share | Instructions / emitted invocation | Bindings / invocation |
| --- | ---: | ---: | ---: | ---: |
| GB forward | 99.036% | 0.334% | 1.339 | 83.67% |
| GB reverse | 98.989% | 0.334% | 1.339 | 83.67% |
| GG forward | 98.032% | 0.513% | 1.243 | 49.99% |
| GG reverse | 97.999% | 0.515% | 1.243 | 49.99% |

HALT polling is a retirement accounting unit, not useful newly decoded guest
instructions. Counts alone are not a wall-time bound; fresh timing below tests
whether this imbalance also dominates elapsed time.

Each gameplay replay changes mapping generation 602 times. The harness clears
*all* bindings on each change, giving 9,844 Game Boy or 8,197 Game Gear timed
bindings. `bindMachineBlock` reconstructs source instructions, re-lowers and
compares canonical IR, copies metadata and patches mapping guards. This is
sensible validation but costly recurring work for one-instruction blocks.
A cache keyed only by guest PC is unsafe; any replacement must bind physical
backing, code identity, mapping and lifecycle correctly.

## Measurement scope and harness cost

The existing benchmark fairly gives all four modes the same static schedule,
input sequence and validation. However it measures a heavily instrumented replay,
not ordinary unrestricted `timeEmulator` execution. Every HALT poll performs PC
lookup, backing resolution, dispatch lookup, execution-state inspection, a
one-retirement slice and map-based accounting. The outer wait loop reads the
logical tick repeatedly. Tick fingerprints, JSON timeline construction and
scenario checks are also inside elapsed time. Startup and artwork upload are
outside it.

In Game Gear, `debugBacking` calls `analysisLocation`, including a mapper-interface
`dynamic_cast`. The historical pre-scalar-guard profile shows 11.76% self samples
in `Runner::advance`, plus significant RTTI, backing inspection and slice dispatch.
This supports investigation, but is not a current isolated speedup measurement.
Optimizing shared harness work must apply equally to the existing comparators.

The four ROMs are two revisions and two ports of one authored gameplay design.
They provide useful correctness scenarios, but weak workload diversity for a
claim about arbitrary Game Boy/Game Gear software. Retain these cases; add
independently chosen active-compute, rendering, banking and device-heavy games as
supplementary evidence rather than replacing unfavorable acceptance cases.

## Fresh timing diagnostic

Artifacts live in `build-working/completion-validation/whole-block-evaluation/`.
The diagnostic used Intel i7-8650U, Linux x86-64, GCC 16.2.1 and the current
RelWithDebInfo `-O2 -g -DNDEBUG` libraries, with the existing powersave governor.
No agent-started compilation or test job overlapped the timing runs; unrelated
host load was not controlled. CPU/compiler/governor and build hashes are retained.
`diagnostic.cpp` is a generated copy of the current ROM harness with a scoped timer
around each `Runner::advance`. It classifies time by entry state: HALT, emitted,
or other active execution. HALT includes an invocation that wakes to an interrupt;
it is not a claim that every cycle in that invocation was idle. The timer includes
lookup, execution, binding and accounting within advance. It excludes outer tick
polling, state fingerprints and timeline construction. There are two clock reads
per advance, so overhead and host noise prevent treating these results as a new
acceptance run or a precise uninstrumented bound.

`run-diagnostic.py` runs three repetitions of baseline and emitted modes for all
four cases, alternating mode order on the middle repetition. All runs execute
full 273-tick gameplay and scenario checks; the script compares complete tick
hashes and final state across both modes and all repetitions. It does not add
per-retirement observers. The existing smoke suite supplies that separate check.

| Case | Baseline HALT % | Baseline active % | Candidate active % | Candidate emitted-only % |
| --- | ---: | ---: | ---: | ---: |
| GB forward | 79.99 | 1.53 | 2.68 | 1.36 |
| GG forward | 75.98 | 2.52 | 5.19 | 2.85 |
| GB reverse | 80.57 | 1.62 | 2.71 | 1.33 |
| GG reverse | 75.63 | 2.61 | 5.39 | 2.89 |

Values are medians of per-run fractions, not ratios of independently selected
medians. Across all repetitions baseline active fractions ranged 1.53–1.64%
(Game Boy) and 2.43–2.61% (Game Gear). Candidate emitted-only work consumes only
about 1.33–1.36% and 2.85–2.89%, respectively, including its binding/guard costs.
Absolute timings varied substantially; these runs do not establish a candidate
speedup. In particular, differences in HALT duration can dwarf all native work.

Under the explicit assumption that other costs stay fixed, even making *all*
baseline active advances free saves only the measured 1.5–2.6%, far below 10%.
This is a diagnostic Amdahl-style estimate, not a rigorous uninstrumented upper
bound: timers perturb execution, classification includes interrupt wakeup, and
outer-loop costs are not attributed per active instruction. The several-fold gap
is strong evidence against another emitted-only micro-optimization meeting this
particular gameplay gate. Expanding active opcode coverage alone faces the same
constraint. A broader HALT/dispatch optimization is the plausible avenue.

For scale, if active work were 1.6% or 2.6% of baseline and could be made entirely
free, shared non-active cost would first have to shrink by approximately 85% or
76%, respectively, to make active work 10% of the remaining total. This is a
conditional model, not a measured optimization forecast. Real active execution
cannot be free, so that scenario is deliberately optimistic.


## Recommended development order and decision points

1. **Fix the measurement bottleneck before tuning another guard branch.** Add
   active/HALT time and block-length histograms to diagnostic reports; preserve
   raw counters. Build a lean replay measurement with identical input commits,
   budgets and tick oracles, applying scheduling/accounting changes to every mode.
   Keep the present contract as the acceptance gate unless explicitly revised.
   A faster harness alone is not an accelerated-backend win.
2. **Prototype bounded HALT execution as a separate execution optimization.**
   First amortize host dispatch while retaining every hardware retirement and
   stop at interrupt/wake, input, budget, observer and lifecycle boundaries.
   Event-driven advancement to the next device deadline is a later, riskier step:
   it must preserve timer edges, DMA, interrupts, audio/video emission, logical tick
   boundaries and observer semantics. Do not just skip cycles or suppress devices.
   Since this improves common work, rerun all competitors; do not credit a common
   optimization only to the candidate.
3. **Remove recurring binding construction.** Separate canonical validation of
   immutable emitted metadata from mapping-specific binding. Reuse validated
   physical-ROM identities across bank switches with explicit generation/lifecycle
   checks. Preserve RAM rewriting, BIOS disable, checkpoint and observer behavior.
   This targets the emitted overhead directly, but the timing budget below means
   it cannot provide the whole gameplay gain on its own.
4. **Make the emitter competitive on synthetic active work.** Prototype a
   core-specific internal host adapter/direct register access to remove virtual
   and ABI dispatch. Keep canonical memory access and helper semantics. Start
   without retaining registers across retirements; observers and interrupts
   require fully published state. Measure generated assembly and paired samples
   before considering register caching or more aggressive fusion.
5. **Expand useful block coverage only after the preceding measurements.** The
   strongest opportunities are common control flow, memory and stack sequences
   that currently fragment blocks; simply increasing the eight-instruction cap
   does not lengthen the current one-to-three-instruction runs. Preserve instruction
   retirement and interrupt side exits. Block chaining is contingent on correct
   entry guards and budgets, not an unconditional jump past the machine owner.

For each runtime prototype require targeted differential boundary coverage and
full CTest, then nine rotated native samples against the newly rebuilt fastest
existing comparator. Keep the >=10% per-core median, <=5% repeatable-regression
rule and all latency gates. Do not infer native ARM64 performance from QEMU.
The prior baseline scheduling test has both passes and failures, so a throughput
success alone cannot establish stable latency acceptance.

## Validation and delivery

- Current CMake configure and full `-j4` build succeeded.
- All 24 diagnostic replays completed; tick hashes, final states and cycle totals
  agreed across baseline/emitted modes and all repetitions for each case.
- Full CTest: 180/186 passed in 392.41 seconds. The six failures involved socket
  creation/binding or Python-worker startup under the execution sandbox.
- The exact six failures were rerun outside that sandbox, without source changes:
  6/6 passed in 6.97 seconds. Thus every discovered test passed across these two
  runs; this is not a claim that the original full-suite run was clean.
- All whole-block, boundary, corpus and report-validator checks passed in the
  original full run, including the 183.19-second ROM replay. All five discovered
  performance tests passed once; nine-repeat stable latency acceptance was not
  repeated, and the historical intermittent latency failure remains relevant.
- No live presentation/audio test, TSAN rerun, or native ARM64 timing was performed.
- No runtime source changes or commits. This report is under the repository's
  ignored `.internal/` tree; raw diagnostic sources, executable, measurements and
  validation logs remain under ignored `build-working/`.

The recommended next implementation experiment is bounded HALT batching with an
identical comparator measurement, followed by immutable-artifact binding reuse.
Neither is claimed implemented or proven to meet 10% by this evaluation.
