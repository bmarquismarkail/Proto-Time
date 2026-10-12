# Plan: acceleration criteria v2 and Game Gear regression repair

Status: implemented, 2026-10-09. See [native evaluation results](time-acceleration-v2-results.md). Criteria v2 and allocation-free remapping are implemented; native measurements retain a no-go for both cores. Earlier results remain immutable, defaults/admission are unchanged, native ARM64 remains deferred, and changes are uncommitted.

## Outcome sought

Replace the mandatory 10% whole-gameplay gain with evidence of useful active
execution, representative-workload non-regression and independent latency health.
Repair or reject Game Gear configurations that regress; changing criteria must
not convert the measured 14.27% regression into a passing result. Ten percent
remains an optimization target, not the minimum admission requirement.

Separate three decisions: correctness-qualified research, competitive opt-in
acceleration, and eligibility for default selection. Each decision is reported per
core and native host. Passing an x86-64 experiment does not open family admission,
certify deferred ARM64 performance, or relax the two-core completion requirement.

## Evidence motivating the plan

The source-bound nine-repeat experiments record these per-core elapsed-time gains
against the fastest validated existing path:

| Configuration | Game Boy | Game Gear |
| --- | ---: | ---: |
| Canonical validation reuse | -3.20% | -3.47% |
| Lean replay + HALT-512 | -2.68% | -9.46% |
| All changes, expanded coverage included | +0.65% | -14.27% |

These configurations were measured in separate sequential runs; their differences
are hypotheses for controlled ablation, not a decomposition of causal effects.
Game Gear's expanded combined case requires 15,970–16,345 remaps. Current
`BoundBlock::remap` copies `Block`, then `CompiledBlock` copies it again and validates
it. Direct-register synthetic entries still lose 3.44–11.72% to existing paths.
Greater coverage alone has not made emitted work competitive.

Full CTest passed 188/189; default headless scheduling-to-present p99 was 31.09 ms
against 16 ms. Nine later dedicated rounds passed all 45 performance checks.
Retain the failure: there is no clean overall latency acceptance from that bundle.

## Proposed criteria

| Requirement | Research correctness | Competitive opt-in | Default-selection eligibility |
| --- | --- | --- | --- |
| Exact CPU/device state, cycles, feedback, tick/input boundaries and lifecycle checks | Required | Required | Required |
| Source/build/ROM/policy-bound native evidence | Required for timing claims | Required | Required |
| Representative and active workload non-regression | Report | Required on every case | Required on every case |
| Demonstrated active-execution benefit | Report | Required per core | Required per core |
| Demonstrated representative gameplay benefit | Report | Report; neutral is acceptable | Required per core |
| Full CTest and nine verbose performance rounds | Record failures | All required gates pass | All required gates pass |
| Reviewed promotion/admission decision | None | Explicit reviewed opt-in decision | Explicit reviewed default decision |

Non-regression preserves the existing 5% tolerance. Every ROM and synthetic case
must satisfy both conditions:

1. Neither the existing median-time-ratio check nor a majority of paired samples
   indicates a loss greater than 5%.
2. The lower endpoint of the proposed 95% paired confidence interval for elapsed
   time reduction is at least -5%. An interval crossing that boundary is
   **inconclusive**, not a pass.

Active benefit requires a 95% paired confidence interval wholly above zero and
at least eight of nine positive pairs on each of two preregistered workloads per
core: compute-heavy and mixed-device. Keep RAM-heavy as an additional mandatory
non-regression case. Run a second independent nine-repeat confirmation session
on the fixed candidate; both sessions must satisfy the criteria. Do not choose
winning workloads after seeing their timings. Retain the existing six synthetic
cases and add an independently authored sustained-active ROM per core, with a
memory/banking/device variant, before any default-promotion claim. Existing two
ports/two revisions are insufficient evidence of arbitrary-game behavior.

The active harness must time real machine execution including guards, memory,
helpers, retirement and binding work, with identical accounting in every mode.
Diagnostic phase timers and isolated arithmetic microbenchmarks cannot satisfy
this gate. Publish useful active retirement coverage, block lengths and HALT
share; do not treat HALT polls as accelerated useful instructions. The new active
ROMs should spend a majority of non-HALT retirements in supported blocks, verified
by accounting, without eliminating their banking/device semantics.

Default eligibility additionally requires a positive lower confidence bound for
the equally weighted representative-corpus result on each core. Opt-in eligibility
alone does not justify making a neutral or slower gameplay backend the default.
Feature completion can still use an explicitly reviewed measured no-go under the
existing contract; a failed acceleration attempt is not silently a success.

## Statistics and evidence implementation

Use nine rotated backend repetitions for confirmation, keeping all three existing
comparators. Select the fastest correctness-validated existing path per case
using its median time in the same configuration. Keep that comparator fixed for
all pairs in the case. Preserve raw times and the historical ratio-of-medians
metric alongside new paired metrics; label their differences explicitly.

For each repeat compute log(candidate time / comparator time). Estimate the
median log ratio and a percentile interval from 10,000 deterministic bootstrap
resamples of paired repeat indices; transform back to elapsed-time reduction.
Record seed, interval method, sample count and criteria version. These intervals
quantify variability in the recorded sessions, not all possible hosts.
For a representative per-core aggregate, average case log ratios with equal case
weights within each repeat, then apply the same paired procedure. Resample whole
rounds so cross-case pairing is preserved. Publish individual cases as mandatory
checks; a corpus aggregate cannot conceal a regression.

Randomize/balance variant order as well as backend order. Record CPU/governor,
affinity, host load, compiler, generated sources, binaries, CMake cache, manifests,
source hashes, warmup, instruction/cycle totals and complete endpoint evidence.
No builds, test suites or other benchmark jobs overlap accepted timing runs.
Predeclare one 27-repeat follow-up if nine-repeat evidence is inconclusive;
retain the initial result and stop as inconclusive if the follow-up cannot resolve
it. Do not repeatedly measure until a favorable result appears.

Criteria v2 gets a new report/evaluator schema and a hashed criteria document.
Historical v1 reports remain immutable and continue to mean what their original
rules meant. Recompute comparisons from complete raw evidence when useful, labeled
retrospective; use fresh preregistered runs for v2 qualification. Incompatible,
missing, stale, smoke-only or emulated evidence fails closed.

## Game Gear repair sequence

### 1. Establish attribution before optimizing

Retain the measured source/archive as the starting point. Add explicit variants
for lean+HALT-512 with generic original coverage, then toggle canonical reuse,
direct registers and expanded coverage independently. Add expanded coverage with
and without reuse and direct entries. Shared HALT/accounting options remain equal
across all comparators. Balance variant order; initial three-repeat screens guide
engineering only, with nine-repeat confirmation for candidates.

In a separate diagnostic run measure initial bind, remap allocations/copies,
entry code validation, continuation scalar guards, generated operations,
`CallHelper(0)`/Z80 refresh bookkeeping, CPU retirement and machine VDP/PSG
retirement. Use stratified or randomized sampling to avoid systematic one-in-64
phase aliasing. Count calls, copied metadata and exit reasons. Compare absolute
nanoseconds and cost per useful active retirement, not just percentages after
HALT work becomes cheaper. Keep diagnostic instrumentation out of throughput
binaries. Establish whether expanded coverage increases binding cost, helper
cost, guard cost or device cost before assigning a cause.

### 2. Remove mapping-specific metadata reconstruction

Keep one canonically validated immutable compiled artifact and a small binding
containing owner/lifecycle identity, current mapping identity, helper ABI, state
guard summary and fault state. Remapping updates bounded scalar binding state;
it must not copy instructions/operations/guards, allocate a new compiled artifact,
re-lower instructions or rerun structural validation of unchanged metadata.

Unify guard evaluation around artifact invariants plus live binding mapping.
Never leave a stale artifact mapping guard and simply skip the contradictory
check. Validate all live executable code bytes after remap and on invocation entry;
retain full checks for arbitrary observers and writable code. Where supported,
record physical ROM/backing identity for the guarded range; live byte checks
remain authoritative. Initially restrict this path to the already validated
low-ROM envelope and reject unknown guard shapes rather than weakening them.

Update both core bindings consistently. A Game Gear-only benchmark adapter must
not create different safety rules. Prove zero metadata allocations/copies during
steady remap with focused diagnostic counters, then benchmark allocation-free
reuse alone and in the shared-HALT combination against the current copy-based
prototype and all existing paths.

### 3. Reduce only demonstrated emitted overhead

If remapping does not resolve the regression, use the profile to prioritize entry
and retirement dispatch. Consider a Game Gear-specific internal adapter with
inlinable register access and refresh bookkeeping; preserve Z80 R semantics,
fetch observers, helper ABI, immediate state publication, interrupts and canonical
memory access. Direct entries remain an independent switch until they establish
a measured benefit. Do not retain guest registers across retirements in this phase.

If expanded blocks remain costly, evaluate instruction families separately and
retain only measured, correctness-qualified combinations for a future fixed
policy. Keep every adverse case in the report. Do not silently route a benchmark
to baseline and label its retirements emitted. Any future dispatch/fallback policy
needs its own predefined behavior, coverage accounting and whole-policy timing.
The current emitter is not assumed universally faster than baseline or portable IR.

### 4. Confirm and stop on evidence

Promote the best fixed combination to fresh nine-repeat sessions, full retirement
windows and the complete tick/endpoint oracle. Require v2 synthetic and ROM gates
on Game Gear and a Game Boy non-regression check. If the profile shows hardware
retirement dominates or the remaining emitted path still loses, retain research
status or publish a measured no-go rather than disabling devices or safety guards.
Event-driven HALT/device coalescing, persistent register caching and block chaining
remain separate later designs, not shortcuts in this repair.

## Validation, contract migration and deliverables

Implementation touches `MachineBlock.hpp/.cpp`, `WholeBlock.hpp`, both machine
binding/guard paths, research benchmark options and collectors as needed. Add
boundary tests for bank switches with identical/different backing bytes, BIOS
unmap, mapping writes within a block, RAM rewriting, interrupt/HALT wake, budget
and observer exits, attached fetch observers, checkpoint/ROM reload, sibling
owners and faults. Verify exact state and feedback for both generic/direct entries.

Version the acceptance collector and its tests. Add fixtures for small but
repeatable active gains, neutral gameplay, a 14% GG regression, aggregate masking,
uncertain intervals, inconsistent sessions, changed criteria, missing samples,
invalid values, skipped tests, stale artifacts and latency failures. Update
`time-acceleration-evaluation.md`, `time-x86-acceleration-acceptance.md`, and the
acceleration acceptance/coreAcceptance strings in `time-feature-completion.json`
together; update references/tests in the completion checker and docs index as
needed. Do not rewrite historical results or unrelated feature requirements.

Configure/build, run focused differential tests, then full discovered CTest. For
latency collect nine **verbose** fixed rounds with exact gate values, enforced
limits and source/binary bindings. Passing-only CTest summaries do not replace
metric records. Investigate the default headless scheduling failure independently
using matched baseline/candidate runs and scheduler/catch-up/queue diagnostics;
record host conditions without changing user/system configuration. A failing full
suite remains a failing qualification bundle, even if isolated reruns pass. No
latency limit change or automatic omission is part of v2.

Use TSAN if shared ownership or handoff changes. Preserve the machine lane as the
single writer; no allocation or extra work enters audio callbacks. Native ARM64
performance stays explicitly deferred; QEMU can provide correctness only.

Deliver a versioned proposed criteria document/evaluator, a Game Gear attribution
report, a bounded immutable-artifact binding implementation, source-bound raw
measurements and per-core/host decisions with unresolved latency noted. Preserve
all existing work, leave changes uncommitted, and keep default execution unchanged
until a separate reviewed promotion decision is made.
