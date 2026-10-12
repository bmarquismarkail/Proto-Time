# Concrete Game Gear host experiment

2026-10-11. Implements experiment B's Game Gear continuation screen in the
[whole-block next-experiments plan](time-whole-block-next-experiments-plan.md).
Status: implemented, measured, and stopped at the compute gate. No Game Boy
expansion, C experiment, v2 qualification or admission change follows this result.
Independent event-aware HALT (D) remains open.

## Candidate and controls

`BasicInvocation<HookSet, Host>` defaults to the original `InterpreterHost`.
The generic entry and public IR interface retain their existing host contract.
The experimental Game Gear runtime also exposes its invocation-local
`GameGearIrHost&`; the generated body then sees the final concrete host's
register, memory and helper methods. `executeMachineBlock` deduces that host type
without retaining it in a binding. The same generated instruction body serves
all paths, with authenticated original entry-pointer dispatch.

Only the isolated typed research archive provides the new thread-local selector.
The ordinary emulator contains neither research selector. All timing rows keep
normal host register operations, cached guard metadata, live policy lookups,
full CPU/device retirement and observer ordering. No direct-register/view
combination, bookkeeping consolidation or HALT optimization is included.

Both progress/budget checks remain: the invocation copies its entry limits,
whereas the machine retirement callback reads the live budget after callbacks.
Eliminating one would require a separate semantics decision and regression
coverage; A did not establish that those checks were interchangeable.

The collector's `--concrete-gamegear` option selects a separate, non-qualifying
`proto-time-concrete-gamegear-v1` schema. It rotates six modes in the same
executable: baseline, cached block, portable IR, generic emitted, A's typed hooks
with erased host, and B's concrete host. It collects only Game Gear's three active
fixtures. Game Boy is gated on two established competitive compute wins.

## Validation and retained evidence

Evidence directory:
`build-working/completion-validation/invocation-specialization/b/`.
Measured and validated source SHA256:
`27dbec7188c761610313edc54ea65d42bb9b602d16cce730c05185e20b6e0805`.
Both sessions bind identical source, generated source, executable, CMake cache,
affinity and governors. Each has a preregistration, source archive, patch,
compiler/host details, frequency/thermal/load observations and raw stdout/stderr.
This report and discovery links were added afterward; they do not rebind the
archived measurement snapshot to the later checkout digest.

The full build passed. The final full sandboxed CTest run passed 199/205 in
652.82 seconds, including all five performance checks and all eight invocation /
concrete-host checks. Six socket/Python-worker tests failed under sandbox
restrictions. Their unrestricted targeted rerun passed 6/6 in 6.59 seconds.
Thus every discovered test has passing evidence across the full run and targeted
rerun; this is not a clean sandboxed full-suite pass or nine-round qualification.
`validation-binding.json`, `discovered-tests.log`, `full-ctest.log` and
`unrestricted-rerun.log` retain the exact validation boundary.

The concrete-host machine and lifecycle matrices cover generic/direct-register
correctness, RAM/code mutation, mapping, BIOS/boot visibility, owner/lifetime,
restore/fault recovery, interrupt state, observer mutation and live policy
replacement. The invocation contract matrix retains budget, ordering, conflicting
machine/observer exits, partial faults and malformed-dispatch coverage.
The collector tests reject incomplete rotations, missing controls, mismatched
instruction streams/endpoints and attempted v2 admission of either screen schema.

An initial test wrapper renamed an existing `main` whose success return was
implicit. Renaming removed that language rule and caused undefined behavior at
its end. The stalled test and its CTest parent were stopped; their log and source
binding remain as `interrupted-wrapper-*`. The repair retains a real `main`,
selects B through a test-only macro, and adds 180-second timeouts. Both corrected
wrapper tests passed in 43.67 seconds, then the entire full suite was rerun.
This interrupted validation is not counted as passing evidence or a performance
collection. No timing collection preceded the repair.

Each active fixture has four-seed per-retirement parity, both conditional paths,
zero HALT, equal warmed/final machine states, and identical emitted/fallback
coverage and invocation counts across the three emitted modes. The two nine-repeat
sessions retain 324 active samples, six correctness records, 36 empty-hook controls
and 54 policy-lookup controls. Empty controls remain ineligible and their timings
are not subtracted from active costs.

## Results

Paired median improvements below compare B with the named comparator. Positive
means B is faster. Confidence intervals are 95% bootstrap intervals of paired
median log ratios using the unchanged v2 statistical helper; these engineering
screens cannot enter v2 admission.

| Active case | Comparator | Session 0 gain, CI | Session 1 gain, CI |
| --- | --- | --- | --- |
| Compute | Generic emitted | +11.52%, [6.62, 14.11] | +12.67%, [12.21, 13.23] |
| Compute | A typed / erased host | +2.33%, [0.73, 2.62] | +1.74%, [1.24, 2.41] |
| Compute | Fastest existing: baseline | **-0.46%, [-2.75, 1.32]** | **+1.88%, [1.70, 2.08]** |
| Mixed devices | Generic emitted | +13.81%, [5.91, 16.23] | +13.73%, [9.88, 14.08] |
| Mixed devices | A typed / erased host | +1.10%, [-2.94, 1.74] | +1.92%, [1.61, 3.00] |
| Mixed devices | Fastest existing: baseline | +3.07%, [2.79, 4.29] | +1.57%, [-3.21, 1.89] |
| RAM | Generic emitted | +19.03%, [15.66, 24.04] | +21.35%, [18.07, 32.68] |
| RAM | A typed / erased host | +1.53%, [-2.10, 2.24] | +4.92%, [1.41, 10.12] |
| RAM | Fastest existing: baseline | +1.06%, [0.04, 1.78] | +2.61%, [-0.28, 6.18] |

All competitive non-regression checks passed in both sessions. Compute has only
2/9 positive pairs in session 0, then 9/9 in session 1. Session 0 does not establish
a competitive compute win. The planned two-session compute continuation gate is
therefore unmet. Mixed-device competitive benefit is also not established in both
sessions. Improvements over generic emitted execution do not meet these gates.

The selected hot Game Gear compute entry bodies contain 40 indirect call sites
for generic emitted, 31 for A's typed/erased host, and zero for B's concrete host.
`selected-disassembly.txt` and `assembly-symbols.json` retain exact symbols and
binary/source hashes. B still calls other functions; live virtual behavior can
remain inside callees. Inlining changes each selected body's scope, so these
counts do not establish removal of all runtime indirect calls, additive costs,
or a predicted speedup. No new per-backend hardware-counter attribution is claimed.

## Decision and continuation

Stop this B candidate at the registered gate. Retain its isolated implementation
and negative/inconclusive competitive evidence; do not broaden it to Game Boy,
combine another micro-optimization, or repeat until it passes. C and winner-only
E are not entered for this candidate. No eligibility/default policy changes.

Proceed independently with D's deadline inventory and conservative Game Gear
HALT prototype. A shared runtime benefit will be measured separately and cannot
substitute for emitted active competitiveness. Native ARM64 timing remains
user-deferred. No live presentation/audio acceptance or new TSAN run is claimed;
B changes compiler visibility without introducing a thread ownership/handoff.

Reproduction commands, using fresh output paths:

```sh
cmake -S . -B build-working
cmake --build build-working -j4
ctest --test-dir build-working --output-on-failure
python3 tools/measure_invocation_specialization.py --build build-working --concrete-gamegear --seed 20261011 --output build-working/completion-validation/invocation-specialization/b/session-0.json
python3 tools/measure_invocation_specialization.py --build build-working --concrete-gamegear --seed 20261112 --output build-working/completion-validation/invocation-specialization/b/session-1.json
```

The collector refuses existing output paths; the retained collections are immutable.
The comparison summaries were independently reproduced from each raw report.
Implementation and reports are included in the user-requested 2026-10-12 commit
checkpoint; the failed continuation gate remains unchanged.
