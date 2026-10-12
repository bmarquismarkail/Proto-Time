# Invocation specialization experiment

2026-10-11. Implements Phase 0 and experiment A of the
[whole-block next-experiments plan](time-whole-block-next-experiments-plan.md).
Status: A implemented and measured; Game Gear improves over generic emitted
execution but has not established competitive compute benefit. [B](time-concrete-host-experiment.md) subsequently stopped at its competitive
compute gate; the independent HALT experiment remains open. This is not v2 qualification.

## Hypothesis and scope

Expose generated operations and concrete before/retire hooks together to the
compiler, retaining the existing abstract host/register strategy, cached guard
metadata and full per-instruction retirement. This isolates orchestration from
the prior direct-register and contiguous-view changes. It does not consolidate
budget accounting yet; that remains a gated B experiment.

`BasicInvocation<HookSet>` shares instruction-order, budget, retirement and
completion checks between generic `Hooks` and invocation-local `TypedHooks`.
The emitter creates one instruction-body template. The isolated core object
set instantiates that body with its concrete machine-owned retirement context.
The generated dispatcher authenticates the original generic entry pointer;
unknown entries fail before effects. Direct-mode pointer pairing is also checked.
Bindings retain no borrowed invocation, machine, CPU or policy pointers.

Only the experimental object set defines `TIME_RESEARCH_TYPED_INVOCATION`.
Its thread-local selector rotates generic and typed invocation rows inside the
same executable. The active screen compares baseline, cached-block, portable IR,
generic emitted and typed emitted. Register strategy is generic for every emitted
timing row; direct mode is exercised separately by boundary regressions. Shared
hardware retirement and live policy lookups remain unchanged.

## Source-bound starting control

The initial source archive, patch, registration, build/environment binding,
discovered test list and raw control are under
`build-working/completion-validation/invocation-specialization/phase0/`.
Control source SHA256:
`662fdc55d6d7bc1a0d7d8638d79764db82e7aaf59fa4976497c3d07f1b7f6682`.

The fresh full control build passed. The full sandboxed CTest run passed 191/197
in 331.01 seconds, including all registered performance checks. Six socket and
Python-worker tests failed under sandbox restrictions. The same six passed in
an unrestricted targeted rerun in 5.29 seconds. This establishes passing evidence
for all 197 registered checks across the full run and targeted rerun, rather
than a clean sandboxed full-suite pass.

The initial uninstrumented active control retained 270 samples and six passed
correctness records. It is the preimplementation control, not a v2 qualification
or a causal comparison with a different source snapshot. The within-executable
generic/typed rotations in experiment A supply the matched implementation control.

## Validation and collection

```sh
cmake -S . -B build-working
cmake --build build-working -j4
ctest --test-dir build-working -R 'invocation|research|acceleration|whole-block' --output-on-failure
ctest --test-dir build-working --output-on-failure
python3 tools/measure_invocation_specialization.py --build build-working --seed 20261011 --output build-working/completion-validation/invocation-specialization/a/session-0.json
python3 tools/measure_invocation_specialization.py --build build-working --seed 20261112 --output build-working/completion-validation/invocation-specialization/a/session-1.json
```

Run builds and tests before timing and do not overlap timed collections. The
collector registers its source/build/criteria/environment before executing and
retains source archives, patches, raw output and stderr. It rejects changed
bindings, incomplete rotations, warmed/final endpoint mismatch, different
generic/typed instruction streams, missing differential/branch coverage, and
diagnostic controls claiming qualification.

The bare-hook lower bound uses identical empty before/retire bodies with a
compiler memory barrier. A separate translation unit supplies erased pointers
to prevent folding those calls in the ordinary non-LTO build. These controls
omit CPU/device work and cannot enter admission. The policy lookup diagnostic
calls the actual virtual machine -> policy -> backend chain; its inclusive cost
is neither a bare virtual-call estimate nor subtracted from active timing.

Disassembly and permitted hardware-counter output are retained with
explicit scope and availability. No host governor/affinity setting is changed.
Native ARM64 performance remains deferred. This experiment supplies no live
audio/presentation acceptance or representative-game diversity evidence.

## Continuation gates

Evaluate A's equivalent-work gain and compiler output before consolidating
bookkeeping in B. B starts with Game Gear compute, and expands only after two
registered competitive compute wins and no clear mixed/RAM regression above 5%.
C remains conditional on B benefit. Event-aware HALT (D) is independent and
will have separate source-bound evidence and equal comparator treatment.
E requires a winning candidate and unchanged v2 qualification; this engineering
schema is intentionally rejected by the current v2 evaluator.

Whole-block remains research; default execution and family admission are
unchanged. Later reports identify their original tested source archives even if
adding documentation changes the checkout digest.

## Final A validation and results

The full build passed. The full sandboxed run passed 196/202 tests in 542.49
seconds, including all five performance checks and all five invocation tests.
The six socket/Python-worker failures passed in an unrestricted targeted rerun
in 6.35 seconds. All 202 registered tests therefore have passing evidence across
the full run and rerun; the sandboxed full suite itself did not pass cleanly.
The earlier focused invocation run also passed 5/5 in 83.58 seconds. No new
ownership/handoff was introduced and no TSAN run was needed for A.

Two registered nine-repeat collections retain 540 active samples, twelve
correctness records, 36 empty-hook rows and 108 inclusive policy-lookup rows.
Each session checks four seeds and both conditional outcomes for cached,
portable, generic emitted and typed execution at every retirement. Timing rows
share warmed and final machine states, cycles and ROM hashes; generic/typed
rows have identical coverage and invocation counts.

Tested source SHA256:
`7f1f8f306d0148b6a8c3de20cf2b2cdc95d4622e03c51f5f72c8afde24941df8`.
Evidence is under `build-working/completion-validation/invocation-specialization/a/`.
`session-0.json` and `session-1.json` contain complete paired intervals and raw
samples; adjacent registrations, patches and source archives reconstruct the
tested snapshot. Both sessions have identical source/build/environment bindings.
`summary.json` records the bounded next step. This results text was added after
collection and does not rebind those measurements to the updated document digest.

Positive percentages mean faster. These are paired median elapsed-time gains;
the fastest validated existing backend is selected independently per case/session
from baseline, cached-block and portable IR.

| Core / workload | Gain vs generic emitted, sessions 0 / 1 | Gain vs fastest existing, sessions 0 / 1 |
| --- | --- | --- |
| Game Boy compute | -0.84% / +0.44% | -13.64% / -13.51% |
| Game Boy mixed | +1.94% / -0.18% | -13.60% / -9.94% |
| Game Boy RAM | +0.88% / -8.03% | -7.67% / -12.06% |
| Game Gear compute | +14.16% / +14.74% | -2.80% / -3.20% |
| Game Gear mixed | +12.82% / +12.97% | +1.46% / +0.87% |
| Game Gear RAM | +16.41% / +17.12% | -0.59% / +0.32% |

All three Game Gear generic-emitted comparisons have positive 95% confidence
lower bounds in both sessions. Competitive compute intervals are
[-3.79%, -2.70%] and [-3.48%, +4.43%]; mixed intervals are [-0.54%, +2.83%]
and [-0.29%, +3.88%]. Thus compute/mixed competitive benefit is not established.
All Game Gear competitive non-regression cases pass the paired screen. Game Boy
has no repeatable generic improvement and clear competitive losses; its second
RAM comparison also regresses against generic emitted execution.

Empty hooks have median erased/typed costs of 1.989/1.550 ns per call in session
0 and 1.810/1.420 ns in session 1. Inclusive machine/policy/backend lookup medians
are 3.952/3.842 ns for Game Boy/Game Gear in session 0 and 3.617/3.614 ns in
session 1. These measurements omit architectural work or include lookup bodies;
they cannot be subtracted from active elapsed time. In particular, the Game Gear
gain cannot be assigned solely to the bare thunk cost: concrete compiler context,
inlining and the generated entry selection are part of the measured candidate.

`selected-disassembly.txt` and `assembly-symbols.json` retain concrete generated
body instantiations and the lookup chain. The standalone policy lookup retains
two indirect calls. The experimental selector is present in the measurement
binary and absent from the production emulator. `perf stat` completed with
cycles/instructions/branches/branch-misses; its complete smoke-process counts
include setup, parity checks and all backends, so they do not isolate a backend's
active instruction cost or certify throughput.

Decision: A identifies meaningful removable orchestration on Game Gear and
justifies the bounded B experiment. Start B on Game Gear, preserving all guards
and observable retirement work; inspect the concrete host/helper path and
redundant accounting before choosing its single implementation change. Do not
extend B to Game Boy until its two-session competitive compute and mixed/RAM
screen gates pass. C remains conditional, D remains independent, and E requires
a winning candidate. No repeat-until-pass collection, broad combination sweep,
criteria relaxation, default promotion or family admission occurred.
