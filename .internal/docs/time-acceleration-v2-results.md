# Acceleration criteria v2: implementation and native evaluation

Status: implemented; measured native x86-64 no-go, 2026-10-09. Changes remain uncommitted.
Default execution and family admission are unchanged. Native ARM64 performance
remains deferred.

## Implementation

Criteria v2 replaces the mandatory 10% gameplay floor with paired active benefit,
per-case 5% non-regression, two independent native confirmation sessions and
unchanged latency gates. Ten percent remains a target. Default eligibility also
requires positive representative benefit and independent active-ROM diversity;
this evaluation does not supply that diversity gate.

Both core bindings now share immutable canonically validated artifacts. Live
mapping belongs to the binding; entry/remap validates live bytes, writable and
observed continuations retain full checks. Ownership, lifecycle, helper ABI,
CPU-state and mapping checks remain authoritative. Steady remap does not re-lower,
revalidate or copy metadata. The previous double-copy path remains an explicit
benchmark control.

The dedicated diagnostic executable samples binding, remap, guards, generated
operations, helpers, CPU retirement and device retirement. Instrumentation is
absent from throughput binaries. Operations include helper cost; these inclusive
phases must not be added together. Zero-sample durations remain unknown.

The initial native source archive and all raw evidence are under
`build-working/completion-validation/acceleration-v2/`. Historical v1 evidence is
unchanged under `acceleration-experiments/`. The pre-change patch and new-file
archive preserve the worktree before implementation; no useful edits were reset.

## Diagnostic attribution

In the Game Gear forward replay, original coverage makes 8,141 steady remaps.
Copy control makes 16,282 metadata copies (approximately 14.98 MB); scalar reuse
makes zero copies. Expanded coverage makes 15,970 remaps: copy control makes
31,940 copies (approximately 46.09 MB); scalar reuse makes zero copies.
The allocation regression also completes 10,000 scalar remaps per core with zero
allocations and unchanged immutable artifact identity.

The replay has 78,151 useful active retirements out of 3,971,593 total retirements,
approximately 98% HALT polls. Original coverage emits 20,383 useful instructions
(26% of active work); expanded coverage emits 49,521 (63%). Device retirement is
called for every retirement. Sampled refresh helper costs are tens of nanoseconds
and less than one percent of this diagnostic replay; hardware retirement and HALT
work dominate. This is diagnostic attribution, not qualification throughput.

## Validation and decisions

Fresh confirmation and matched scheduling are complete. The full build passed,
all 10 focused tests passed, and the final discovered CTest suite passed 191/191.
All nine final verbose performance rounds passed (45/45), with exact enforced
16 ms gate values retained. The evaluator has 15 passing tests, including exact
failed-output replay and rejection of conflicting metrics. Earlier latency
failures remain in the report. Both native x86-64 core decisions are failed;
competitive opt-in and default eligibility remain false.

## Balanced screens and fresh paired measurements

The balanced screen contains 384 timing rows: eight combinations, four cases,
four backends and three repeats. Scalar expanded remapping improves Game Gear
emitted time versus its copy control by 10.77% forward and 8.11% reverse. These
are engineering screens, not qualification. Standalone original-HALT screens
contain another 96 rows. Against the fastest existing backend, scalar reuse
averages -3.57% Game Boy and -0.85% Game Gear; copy control averages -7.32% and
-3.17%. The standalone runs were sequential, so this contrast is supporting
evidence rather than a causal decomposition. Two separate diagnostic runs retain
64 rows. Expanded reverse Game Gear records 602 validated MachineBoundary side
exits for both copy/scalar controls; original coverage records zero truncated
side exits. Every side exit remains in the emitted accounting.

The fixed candidate is `halt512-expanded-reuse`: generic generated entries,
expanded coverage, scalar binding reuse, lean accounting and HALT-512. Every
comparator shares accounting and HALT options. Two fresh nine-repeat sessions
retain 288 gameplay and 540 active timing rows. The gameplay oracle compares
35,840 native retirements across the two sessions, plus every committed tick and
complete endpoints. Active cases retain seed-dependent per-retirement correctness
and equal machine endpoints for all five backends. Initial synthetic bindings are
prepared during warmup; runtime binding/remapping is included in gameplay timing.

The table reports paired median elapsed-time reduction and 95% intervals, using
10,000 deterministic bootstrap resamples of paired log ratios. Negative means
slower. The comparator is the fastest validated existing backend by median in
each case/session. Historical ratios of separate medians remain in raw reports;
they differ from paired medians and cannot substitute for these intervals.

| Core | Domain / workload | Session 1: gain %, 95% interval | Session 2: gain %, 95% interval | Non-regression, sessions 1 / 2 |
| --- | --- | --- | --- | --- |
| gameboy | active / compute-heavy | -15.61 [-19.67, -14.61] | -18.23 [-41.43, -14.68] | failed / failed |
| gameboy | active / mixed-devices | -12.89 [-15.03, -5.23] | -12.73 [-17.26, -7.31] | failed / failed |
| gameboy | active / ram-heavy | -9.78 [-11.81, -6.70] | -8.98 [-13.60, -7.02] | failed / failed |
| gameboy | rom / collect-forward | +0.07 [-2.53, +3.66] | -1.61 [-6.30, -0.18] | passed / inconclusive |
| gameboy | rom / collect-reverse | +0.08 [-5.82, +12.83] | +0.27 [-6.78, +0.49] | inconclusive / inconclusive |
| gamegear | active / compute-heavy | -7.94 [-9.13, -2.43] | -7.76 [-15.48, +9.89] | failed / failed |
| gamegear | active / mixed-devices | -0.22 [-4.90, +5.53] | -3.59 [-18.52, +2.29] | passed / inconclusive |
| gamegear | active / ram-heavy | -5.00 [-5.08, -4.67] | -4.08 [-10.14, +20.34] | failed / inconclusive |
| gamegear | rom / collect-forward | -4.12 [-15.17, +8.76] | -4.27 [-14.47, -0.19] | inconclusive / inconclusive |
| gamegear | rom / collect-reverse | -0.74 [-4.92, +18.95] | -4.03 [-6.92, -1.05] | passed / inconclusive |

Compute-heavy benefit is absent in the fixed candidate; Game Boy also has clear
mixed-device and RAM-heavy regressions. Several gameplay intervals cross the
-5% margin. Consequently both cores fail competitive opt-in criteria, despite
the remap improvement. Default eligibility also remains closed. Direct entries
were measured as an exploratory alternative: their first-session Game Gear
compute loss is 8.46%, and all Game Boy cases lose 9.43–13.28%. A Game Gear
mixed-device improvement alone does not satisfy the predefined benefit workloads.

No 27-repeat follow-up is triggered: the overall decisions have clear failing
requirements, rather than only inconclusive evidence. The plan's bounded stop
rule applies to the remaining emitted losses. Refresh/dispatch specialization,
persistent register caching and device coalescing were not added. Whole-replay
diagnostic phase shares do not establish active-only cost shares. Independent
active-ROM diversity remains required before any default-promotion claim.

## Scheduling attribution and retained first attempt

The pre-change source archive is independently rebuilt for the identical default
headless scheduling test. Nine alternating matched pairs pass 7/9 baseline and
5/9 candidate. Maximum scheduling p99 is 37.03 ms baseline and 68.71 ms candidate
against the unchanged 16 ms limit. All runs report zero frame overwrites and
stale-epoch drops; maximum frame residence is 1.26 ms baseline and 0.59 ms
candidate. Failed runs show catch-up clamping and discarded guest time, with
sub-millisecond sleep overshoot. The scheduling metric measures against an ideal
absolute guest-frame timeline. These observations suggest loss before frame
publication, not accumulated queue residence; they do not isolate a cause or
prove equivalence between builds. Host configuration is unchanged.

The first v2 collection retains 190/191 CTest passes and 41/45 dedicated
performance passes. Its checker reports not-run because CTest replayed failed
metric output. This parser error was fixed and regression-tested; only the Python
checker/test source changed. The first source archive and complete raw bundle
remain immutable. A new source-bound collection uses the same C++ binaries and
the corrected checker. All nine final verbose performance rounds passed (45/45),
with 16 ms enforced. Those passes do not erase the earlier failures. Final full
CTest passed 191/191 in 338.98 seconds. The source-bound evaluator reports failed
for both cores, with no latency failures in this final bundle; active regressions
and uncertain per-case margins still prevent eligibility.

## Reproduction and source bindings

Final source SHA256: `884ae78a818040e086f302fb63461a7ed8a0087c1dda70d2badb632ac1dc6406`. The final source archive is
`build-working/completion-validation/acceleration-v2/tested-source-v2-final.tar.gz`;
its full file list and archive checksum are in the adjacent binding JSON. The
initial archive/source binding remains unchanged. Only the Python checker and
its test changed between collections; C++ executable hashes and CMake cache are
identical. The source/build/criteria-bound final bundle is
`build-working/completion-validation/acceleration-v2/qualification-final/evidence-v2.json`;
per-core reasons, intervals, raw logs and registration are adjacent.

Recheck the decision without rerunning measurements:

```sh
python3 tools/measure_x86_acceptance_v2.py --check \
  --output build-working/completion-validation/acceleration-v2/qualification-final
```

Exit code 1 represents the measured failed decision. Rechecking reproduces the
JSON decision exactly. `final-verification.json` records source/archive checks,
identical C++ binaries, and the retained gate exits. No promotion, commit, system
configuration change or native ARM64 timing claim occurred. No ownership/handoff
changed, so a new TSAN run was not needed. All changes remain uncommitted.
