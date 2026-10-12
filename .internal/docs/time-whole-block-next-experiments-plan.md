# Whole-block acceleration: invocation specialization and event-aware HALT

Date: 2026-10-11. Status: bounded implementation and engineering validation complete; B stopped at its compute gate.
No new performance qualification is claimed. Planning source: `f6a1395` on `working`.

Implementation tracking: [invocation specialization results](time-invocation-specialization-experiment.md)
and [concrete Game Gear host results](time-concrete-host-experiment.md).
A improves over generic emitted execution. B's concrete-host screen did not
establish a competitive compute win in both registered sessions, so B is stopped
without Game Boy expansion. Dependent C and winner-only E are not entered for
this candidate. Independent [event-aware HALT (D)](time-halted-advance-experiment.md) now has a
two-core pilot with directed parity, two native shared-runtime collections and
current headless validation completed. Baseline execution,
admission and the native ARM64 deferral are unchanged.

## Objective and authority

Use the supplied “Proto T.I.M.E. — Whole-block acceleration review” to guide the
next bounded experiments toward the fastest correct Game Boy and Game Gear
execution. Separate emitted-backend competitiveness from shared runtime gains.
The first experiment isolates invocation overhead; event-aware HALT follows as
an independent shared-runtime investigation. Neither workstream changes default
execution, family admission, or the frozen historical backend.

The authorities remain [acceleration evaluation v2](time-acceleration-evaluation.md),
[`tools/acceleration-criteria-v2.json`](../../tools/acceleration-criteria-v2.json),
and the [two-core completion contract](time-feature-completion.md). This plan
does not revise their thresholds or certify any completion row. Native ARM64
performance remains user-deferred and open; QEMU supplies correctness evidence
only. A third core is outside scope.

## Verified starting point

The response is a useful interpretation, not a replacement for source-bound
evidence. The following historical results were checked against current reports;
they were not rerun while drafting this plan.

| Evidence | Consequence for this plan |
| --- | --- |
| [Feasibility study](time-whole-block-feasibility-evaluation.md): gameplay dominated by HALT polling and short emitted invocations | Treat whole-replay throughput separately from useful active CPU work. |
| [V2 results](time-acceleration-v2-results.md): both cores failed competitive opt-in; Game Boy compute gains were -15.61% and -18.23% | Retain failed evidence and unchanged qualification rules. |
| [Active profiling](time-active-workload-profiling.md): all six generic emitted active cases regressed; samples have zero HALT and steady remapping | Another binding-copy or HALT-only improvement cannot establish emitted active benefit. |
| [Checked code-view experiment](time-contiguous-code-view-experiment.md): no competitive active win in either session | Do not start another guard/cache/view combination sweep. |

Current source seams:

- [`WholeBlock.hpp`](../../inst_cycle/research/WholeBlock.hpp): `Invocation::begin`
  enforces ordering and budgets before its `Hooks::before` call;
  `Invocation::retire` accounts progress, calls `Hooks::retire`, and checks exits.
- [`MachineBlock.hpp`](../../inst_cycle/research/MachineBlock.hpp):
  `executeMachineBlock` supplies type-erased before/retire thunks and also handles
  budget/exit decisions. CPU retirement precedes machine retirement, which
  precedes the retirement observer. The observer still runs when machine
  retirement requests an exit; machine exit has priority over observer exit.
- [`GameBoyMachine.cpp`](../../cores/gameboy/GameBoyMachine.cpp) and
  [`GameGearMachine.cpp`](../../cores/gamegear/GameGearMachine.cpp):
  `runResearchBlock` supplies live guards, CPU retirement and machine retirement.
  Guards reread virtual policy/backend state and lifecycle after callbacks.
- [`WholeBlockEmitter.cpp`](../../inst_cycle/research/WholeBlockEmitter.cpp):
  build-time native generation is the starting mechanism. A runtime JIT is not
  required to test invocation specialization.
- [`diagnose_acceleration_active.cpp`](../../tests/diagnose_acceleration_active.cpp),
  [`PhaseDiagnostics.hpp`](../../inst_cycle/research/PhaseDiagnostics.hpp) and
  [`profile_acceleration_active.py`](../../tools/profile_acceleration_active.py):
  existing diagnostic infrastructure. Its inclusive sampled timings do not
  isolate bare thunk cost and must not be added into a wall-time budget.

## Invariants for every experiment

Keep authoritative guest state on the machine lane. Audio callbacks drain
prepared samples; UI/render owns host events, uploads and presentation. No new
guest-state worker, shared mutable register cache, or callback allocation is
introduced. Use TSAN if implementation actually changes ownership or handoffs.

Keep owner, lifecycle, mapping, helper ABI, PC, CPU state, code-byte and exclusive
execution checks. Writable and observed continuations retain required byte
validation. Preserve instruction-fetch observer restrictions. Never retain a
live policy reference, memory view, or borrowed CPU state across callbacks or
binding lifetimes. Immutable artifact metadata may remain shared.

Retire each active instruction exactly once, preserving registers, cycles,
control-flow feedback, device effects, plugin emission and observer ordering.
Preserve exit precedence, saturated cycle accounting, zero/exact/overshooting
budgets, and faults after partial effects. Faults invalidate the research session;
there is no implicit retry of a partially executed instruction.

## Phase 0 — Freeze controls and register each hypothesis

1. Record current source digest, patch/untracked source archive, generated source,
   executable hashes, compiler/version, effective CMake cache, ROM hashes,
   backend flags and host environment in a new evidence directory.
2. Establish current-source correctness and an uninstrumented active control.
   Preserve existing reference, cached-guard and code-view candidates separately.
3. Register one candidate at a time, its comparator, workloads, collection order,
   and a bounded engineering screen before collecting results. Keep failures.
   No builds, tests or other measurements overlap timed collections.
4. Record governor, affinity, frequency/thermal observations and background load.
   Use identical conditions within a comparison. Host configuration changes
   require separate user authorization; first improve measurement discipline
   within the existing configuration.

Deliverable: reproducible control bundle and hypothesis registration. If current
correctness or latency fails, investigate separately before qualification. Do
not reinterpret an earlier passing suite as a current-source pass.

## Experiment A — Isolate invocation and hook overhead

Ownership: internal research contract/emitter, diagnostic harness and collector.
First implement a diagnostic-only comparison using the same instruction stream,
guards, helpers, register effects, CPU/device retirement and callback order:

1. Existing generated entry through `Invocation` and `executeMachineBlock`.
2. A statically typed, core-owned invocation context performing equivalent work.
3. An explicitly non-qualifying lower-bound control that isolates empty thunk
   dispatch or accounting. It must never enter eligibility evaluation.

Keep register strategy and guard implementation fixed so this experiment tests
invocation orchestration rather than combining direct-register/view changes.
Measure bare hook dispatch and repeated policy lookup separately from their
bodies. Prevent dead-code elimination and check equal endpoints for equivalent
variants. Use diagnostic counts, disassembly and `perf stat` where permitted to
inspect indirect calls, spills and call-clobbered state. Unavailable counters
are recorded as not run, not substituted with sampled phase estimates.

Deliverable: diagnostic attribution plus an uninstrumented equivalent-work
screen on both cores. Proceed to B only if measured overhead and compiler output
identify removable orchestration with preserved semantics. If not, retain the
negative result and review whether further specialization is justified.

## Experiment B — Specialized core-owned compiled executor

Ownership: internal research entry/context and emitter, first Game Gear machine
integration, then Game Boy, corresponding boundary/corpus tests and collectors.

Start with Game Gear compute. Add a separately selectable internal emitted entry
whose concrete context exposes operations, live guards and retirement to the
compiler. Share emission semantics with the generic path to avoid maintaining
two opcode implementations. Consolidate progress/budget bookkeeping only where
A proves redundancy; keep one explicit state-publication boundary per instruction.
Keep the generic research entry and public IR ABI intact as differential controls.

Specialization may inline a concrete core operation. It may not bypass virtual
policy/plugin semantics, suppress live lookups after callbacks, or batch away
required hardware/observer effects. Ordinary baseline/cached/portable execution
must not change in this experiment. Binding retains immutable artifacts and
scalar generations, never the invocation-local context.

Validate fault, byte mutation, observer mutation, policy replacement, mapping,
restore, interrupts and budgets with `smoke_research_whole_block.cpp`,
`smoke_research_machine_block.cpp`, `smoke_research_machine_boundaries.cpp`,
`smoke_research_remap.cpp` and `WholeBlockCorpus.hpp`. Add targeted regressions
for new behavior, including callback order and conflicting exit requests.

Compare Game Gear compute against baseline, cached-block and portable IR in
rotated, uninstrumented runs. Require a repeatable competitive compute win in
two registered engineering collections before extending specialization to Game
Boy. Mixed-device and RAM cases must show no clear regression above 5% before
broadening. This is an engineering continuation gate, not opt-in qualification.
After Game Boy integration, screen all six active cases. If no competitive win
emerges, stop this candidate rather than immediately combining unrelated changes.

## Experiment C — Useful control flow and guarded continuation

Dependency: B demonstrates useful competitive active benefit. First measure
dynamic block length, active coverage, entry/dispatcher transitions, side-exit
reasons, helper frequency and time per useful retired instruction.

Add conditional/control-flow coverage in small independent stages: branch paths
and bounded loop backedges; then stack/call/return sequences; then guarded
successor continuation. Maintain bank-aware identities and canonical lowering.
Every successor rechecks the live state required after the previous retirement.
Interrupt, budget, observer, mapping, code mutation, segment and lifecycle exits
remain immediate. No unbounded linked loop may monopolize a host slice.

Extend per-retirement differential coverage to both branch outcomes, stack
wraparound, return-target mutation, RAM self-modification, bank transitions,
interrupt entry and exact exit precedence. Include Game Gear prefixes, refresh,
shadow/indexed state and deferred EI. Reject a variant that increases coverage
or length while worsening elapsed time. Qualify a winning B candidate before C
if C would delay review; longer blocks are not an admission requirement.

## Experiment D — Event-aware HALT, independent shared-runtime work

This experiment does not depend on B succeeding. Execute and measure it
separately; do not combine it into B's first qualification. Start with Game Gear
as the smaller pilot, then Game Boy only after exact event parity is established.

First inventory all observable halted-time effects and their next deadlines.
The candidate internal interface is provisionally `advanceHaltedCycles(limit)`;
the deadline/event model must be reviewed before choosing its final API.
Return elapsed cycles and a boundary reason to the machine-owned loop.

The limit is the earliest safe boundary among IRQ/NMI and CPU wake conditions,
timer edges, video transitions, audio generation, DMA/serial where applicable,
input/tick commits, slice budget and host service deadline. Unknown deadlines
fall back to existing stepping. Advancing devices must actually reduce polling
work; simply looping existing retirements inside the API is a batching control.

Define logical HALT progress and step-counter/event timestamps explicitly. Never
invent retired active instructions. If an observer, fetch hook, debugger,
capture/snapshot mode, plugin or other consumer needs every polling retirement
or exposes unsupported mutation, conservatively fall back. Even without an
observer, device/plugin events retain exact order and guest timestamps. Do not
skip an audio sample or input boundary because final registers match.

Differential tests compare IRQ/HALT wake, cycles, registers, refresh behavior,
ordered writes/events, device state, audio sample stream and video milestones at
each boundary. Game Boy additionally covers HALT bug, IME-disabled pending IRQ,
timer edges/reload races, FF50 boot semantics, DMA and serial. Game Gear covers
interrupt modes, deferred EI, NMI, refresh and VDP/PSG boundaries. Reset/restore
must invalidate pending deadlines. Use random bounded windows plus directed
boundary tests and the existing HALT/interrupt suites.

Apply the same HALT optimization to every eligible comparator and emitted path.
Collect a separate before/after shared-runtime study, then compare emitted
against the fastest existing backend with both sharing the optimization. A
shared gameplay improvement does not prove a relative emitted-backend gain.
Validate latency, FIFO occupancy/underruns, sleep/catch-up behavior and frame age;
keep audible/live presentation acceptance separate from headless evidence.

## Experiment E — Requalification and architectural decision

Freeze the winning candidate and extend collector/schema tests to select it.
The current v2 CLI only selects `emitted` or `direct`; specialized candidates
need explicit wiring before qualification. Preserve criteria and keep
diagnostic/lower-bound runs ineligible. Verify the collector executes the same
candidate in active and gameplay domains and binds its generated binaries.

Run two independent native x86-64 nine-repeat confirmations, full discovered
CTest and nine rounds of all existing performance gates. For each core:

- Compute and mixed-device require positive 95% paired confidence lower bounds
  and at least eight of nine positive pairs in both sessions.
- Every active and required gameplay case requires the unchanged 5%
  non-regression rules and confidence margin; RAM remains mandatory.
- Preserve the 16 ms latency gates, raw samples, endpoints, correctness traces,
  source/build/criteria bindings and all failed attempts.
- Use a single preregistered 27-repeat follow-up only for an inconclusive overall
  result, never to overwrite a clear failing requirement.

Before default review, add independently selected sustained-active ROMs on both
cores with reproducible hashes, scenarios and hardware demands. Define corpus
selection before timing; use distributable fixtures or user-provided local ROMs
without adding copyrighted game binaries to Git. Positive representative benefit
and independent diversity are additional gates. Native ARM64 remains deferred;
Linux x86-64 success does not complete both-host acceptance or open admission.

Outcome report: per-core opt-in/default eligibility; observed shared-runtime
gain; current latency/correctness status; live versus headless checks; deferred
work; and reviewer decision. Keeping cached execution and retaining whole-block
as research is a valid outcome. A reviewed no-go follows the completion contract
and cannot substitute for ordinary missing feature or native-host evidence.

## Execution commands and deliverables

Existing commands for Phase 0 and ongoing correctness work:

```sh
cmake -S . -B build-working
cmake --build build-working -j4
ctest --test-dir build-working -N
ctest --test-dir build-working -R 'research|acceleration|whole-block' --output-on-failure
ctest --test-dir build-working --output-on-failure
python3 tools/measure_acceleration_experiments.py --build build-working --synthetic --output build-working/completion-validation/invocation-specialization/control.json
python3 tools/profile_acceleration_active.py --build build-working --repetitions 3 --output build-working/completion-validation/invocation-specialization/phases.json
```

Use fresh evidence directories for later candidates. Do not run qualification
with a guessed candidate flag. Once E's selection plumbing is implemented,
document its exact invocation and check-only command in the result report.

Each experiment delivers localized implementation, targeted regressions, raw
source-bound evidence and a report under `.internal/docs/`. Add a discovery link
when reports are ready. No implementation commit, host change or promotion is
authorized merely by creation of this plan.

Deferred: adaptive backend selection, persistent register caching, runtime JIT
libraries and further micro-optimization combinations. Revisit only after these
experiments establish a specific remaining bottleneck. External projects named
by the supplied response may guide later research; their implementation details
and historical speedups have not been independently verified for this plan and
are not performance predictions for Proto-Time.

## Completion audit — 2026-10-11

| Item | Disposition and evidence |
| --- | --- |
| Phase 0 | Source/binary/ROM/environment controls registered and archived for every timed candidate. Failed runs retained; no overlapping builds/tests during confirmation collections. |
| A | Typed invocation and empty-hook/policy controls implemented on both cores, with equivalent-work differential coverage. Game Gear improves over generic emitted execution but still loses to baseline compute. Frozen invocation and live callback budget checks serve distinct contracts; accounting was not consolidated without proof. See the invocation report. |
| B | Concrete Game Gear host plus typed invocation implemented and screened twice. Competitive compute benefit was not established in both sessions. Stop rule applied; Game Boy specialization was not entered. See the concrete-host report. |
| C | Not entered: B's required active-compute continuation gate failed. No unsupported control-flow or chaining expansion was added. |
| D | Both core pilots, fresh event deadlines, actual idle-dispatch reduction, hardware-state/observer fallback parity, two nine-repeat native shared-runtime studies, matched optimized comparators, prepared-PCM FIFO checks and headless scheduling diagnostics completed. See the HALT report. |
| E | Not entered: no winning whole-block active candidate. No collector admission wiring, nine-round opt-in qualification or default review was claimed. D's shared runtime gain cannot satisfy B/E active competitiveness. |
| Final validation | Full build; 211 discovered tests, 205/211 passed in the sandbox and the six environment-restricted cases passed 6/6 outside it. Existing performance gates and D latency/FIFO checks passed; both raw records retained. |
| Production and deferred acceptance | Defaults/admission/feature rows unchanged; native ARM64 remains user-deferred. Independent ROM diversity, broader consumer support and audible/live presentation acceptance are separate future obligations before production review. |

The bounded outcome is to retain whole-block compilation as research, stop this
specialization candidate, and retain the promising event-aware HALT pilot for
separate production review. This is an engineering disposition, not a reviewed
feature-completion exemption or a substitute for missing native-host evidence.
Measured runtime/study sources and binaries remained unchanged when adding the
FIFO validation target; only report/status edits followed the final suite.
Implementation and reports are included in the user-requested 2026-10-12 commit
checkpoint. Production defaults and deferred acceptance obligations remain unchanged.
