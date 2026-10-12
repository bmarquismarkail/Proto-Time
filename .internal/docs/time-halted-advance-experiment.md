# Event-aware HALT experiment

2026-10-11. Experiment D of the
[whole-block next-experiments plan](time-whole-block-next-experiments-plan.md).
Status: bounded two-core engineering study complete. Shared-runtime benefit is
reproduced; emitted competitiveness remains unestablished. Current headless
validation passed with the recorded sandbox split. No production promotion or
performance qualification is claimed.

## Outcome and source-bound results

Two independent native Linux x86-64 collections each retained 288 rotated timed
samples and 32 backend/selector correctness rows: 576 timed samples overall.
The same 128-poll halted-entry limit and runtime optimization were available to
baseline, cached-block, portable IR and emitted execution. These are inclusive
headless authored-gameplay replay measurements, not isolated CPU throughput or
independent ROM diversity. No governor, affinity or other host setting changed.

Baseline selector-on versus selector-off paired median time reduction, with 95%
confidence intervals; every row had 9/9 positive pairs in both collections:

| Core | Replay | Session 1 | Session 2 |
| --- | --- | --- | --- |
| gameboy | collect-forward | 32.05% [23.43, 34.73] | 32.03% [29.26, 34.15] |
| gameboy | collect-reverse | 32.24% [28.29, 35.90] | 32.79% [29.26, 38.64] |
| gamegear | collect-forward | 54.27% [53.24, 56.35] | 54.84% [51.72, 56.17] |
| gamegear | collect-reverse | 53.66% [51.88, 55.54] | 53.52% [51.82, 54.76] |

The fastest optimized existing comparator was baseline for all four replays.
Matched emitted execution established no repeatable relative benefit in either
session. Its first-session Game Gear forward comparison was -5.69%
[-9.57, -0.30], triggering the unchanged non-regression rule; other relative
comparisons were passing or inconclusive on non-regression, without established
benefit. This study does not reopen B's failed active-compute gate or enter C/E.
Keep the whole-block backend and this shared-runtime pilot as internal research.

Both native sessions used source SHA256 `e1c0c439b6c9f3ed51a390c67e7c283ac40debb88f6160eb2f2e4298d2a722a7`.
Registrations, raw outputs, environment/frequency/thermal observations, source
archives/patches, generated-source/ROM/CMake/binary bindings and comparisons are
in `build-working/completion-validation/halted-advance/shared-runtime/`.
The retained one-repeat preflight was a schema/correctness check, not a
confirmation collection. Both nine-repeat summaries reproduce from their raw
samples. All 254 checked runtime/study source files and measured build bindings
still match after adding the separate FIFO validation target.

Final full build and discovered CTest ran 211 tests: **205/211 passed in the
sandbox** (655.19 seconds); the six socket/Python-worker failures passed **6/6
outside the sandbox** (5.31 seconds). Both records are retained, not described as
a clean sandboxed run. All A/B/D tests and existing performance gates passed.
Full-suite source SHA256 `b497e5b961455bf5c29187d6e0f33b07671a233ea717a531a5ab329f57965611` and binary/archive bindings are retained in
`build-working/completion-validation/halted-advance/final-validation/`.
The FIFO regression and its CMake registration were added after the native
collections; the measured runtime, study sources and binaries did not change.
Only documentation status/outcome updates followed full-suite validation.

HALT-enabled plugin fallback passed the four unchanged 16 ms headless gates:
frame residence 0.610 ms, presentation 0.000083 ms, scheduling-to-present 1.630 ms,
and input-to-frame response 12.139 ms. It published/presented all 30 frames,
with zero overwrites/stale drops, zero catch-up clamps and zero discarded guest
time; maximum sleep overshoot was 0.371 ms and execution batch 0.862 ms.
Its bulk counter was zero, as required by the attached video plugin. These
measurements validate conservative consumer fallback, not optimized live video.

`smoke-halted-fifo` separately feeds identical nonzero PCM from both eligible
halted pilots through prepared source/device-rate transports. Both cores matched
slice/device/packet/output state, with one prepared chunk before each drain and
zero afterward, zero underruns, source overruns, dropped ready blocks or engine
overruns. Game Boy exercised 22,827 bulk calls and 213 drains; Game Gear 20,160
bulk calls and 249 drains. Final full-suite callback p99 was 104 ns and 118 ns
respectively against the unchanged 50,000 ns prepared-drain gate. This is a
controlled headless transport check, not hardware callback cadence or audibility.
The existing stats getter does not publish its recorded drain-occupancy histogram;
the fixture samples published queue depth immediately before/after each drain
outside the timed drain region. No shared stats-getter change was made.

The production executable contains none of the research selectors. Admission,
default policy, feature-completion rows and the user-deferred native ARM64
obligation remain unchanged. No audible/live presentation or ARM64 performance
acceptance is claimed. Implementation and reports are included in the user-requested
2026-10-12 commit checkpoint; this does not change their research-only status.

## Game Gear deadline inventory

The current emulation lane owns CPU, VDP, PSG, input commits and event emission.
No audio callback, render lane or background worker acquires guest state.
The graph and semantic-search services were unavailable during this inventory;
current source provides the evidence.

| Effect | Current behavior and safe boundary |
| --- | --- |
| CPU HALT | `Z80Interpreter::step()` first checks the internal IRQ provider, then returns four cycles when halted. An idle poll does not fetch, increment refresh, decrement deferred EI, or change PC. Preserve these current semantics; this optimization does not correct hardware accuracy. |
| IRQ | The machine provider checks VDP IRQ level and clears the machine's request flag. IM0/IM1/IM2 acceptance is an ordinary CPU step. Pending machine or VDP IRQ forbids batching, including the current IME-disabled boundary behavior. |
| NMI | The current Z80 interpreter/machine has no NMI injection or acceptance path. No NMI hardware support/parity is claimed. Adding a future wake source requires an explicit deadline or fallback before enabling this fast path. |
| PSG | `GameGearPSG::step()` adds `cycles * 48000` to integer phase; at 3579545 it generates a sample and advances oscillators/noise. The next sample deadline is the integer ceiling of remaining phase divided by 48000. Chunk generation and audio notification remain on ordinary boundary retirement. |
| VDP | `GameGearVDP::step()` accumulates cycles, advancing scanline/status/IRQ at 228 cycles. Before that boundary only the phase and approximate H counter change. Stop strictly before the next scanline, then use ordinary retirement to consume scanline/VBlank/IRQ flags. |
| Pending effects | An already pending scanline, VBlank or IRQ event, or a PSG frame counter not yet acknowledged by machine retirement, forces ordinary stepping. Restored phase values outside the known range return an unknown deadline and force stepping. |
| Input | Input remains committed by the caller's existing service/tick boundaries. The supplied slice budget bounds host/input service, with the same single-poll soft cycle overshoot. Tests vary input at caller boundaries. |
| Save flushing | VBlank-triggered save scheduling remains in `onInstructionRetired`, at the same logical poll timestamp. No VBlank is crossed by the interior advance. |
| Observers/capture | Retirement observers, instruction-fetch observers, capture/snapshot controllers, debugging and native trampolines force the ordinary path. Per-poll observation or mutation cannot be suppressed. |
| Plugins/custom execution | Any attached plugin forces ordinary stepping, even if disabled. Only the exact final built-in baseline/cached/portable policy types with built-in IR components are eligible. Custom policy, adapter or backend semantics remain on the ordinary path. |

Sources: `cores/gamegear/Z80Interpreter.cpp`, `GameGearMachine.cpp`,
`GameGearPSG.cpp`, `GameGearVDP.cpp`, and `machine/RuntimeContext.hpp`.
The Sega hardware reference skill was used as hardware context; this inventory
states current source behavior, not physical hardware acceptance.

## Pilot contract

`TIME_RESEARCH_HALTED_ADVANCE` is defined only by the isolated
`time-research-halt-advance` archive. A thread-local research selector defaults
to false. Ordinary production core objects do not compile the advance path.
All eligible comparator backends share the same selector and runtime mechanism;
there is no emitted-only HALT shortcut.

The pilot enters only when a slice begins already halted. Active entry uses the
ordinary machine loop. Eligible halted slices retain the same begin/end lifecycle,
zero-budget precedence, soft cycle overshoot, saturated cycle accounting and
machine/segment/cycle/instruction exit precedence. Unknown conditions fall back.

Each Game Gear bulk advance is bounded by the minimum fresh PSG/VDP deadline and remaining
budgets. It advances whole four-cycle polls strictly before either device event,
and only when at least two polls can be combined. VDP and PSG each receive one
interior step. No CPU step or full machine retirement loop runs for those polls.
The terminal sample/scanline/IRQ poll uses the existing CPU/machine retirement.
No deadline is retained across load, reset or restore.

Logical idle credits and the machine step counter match the existing poll count.
The legacy `retiredInstructions` field therefore retains its existing HALT-poll
meaning; these credits must not be presented as retired active CPU work.
Last feedback describes the last logical four-cycle poll, not the aggregate span.
Research counters report physical bulk calls, skipped logical idle polls and
canonical steps inside the experimental halted loop. Complete ordinary fallback
is not instrumented by these counters. Cached/portable IR diagnostic dispatch counters reflect
actual attempted dispatches, which decrease when polling work is skipped; they
are not architectural retirement counters.

## Initial pilot verification record

`tests/smoke_halted_advance.cpp` compares full device serialization immediately
before and at event boundaries, machine deterministic fingerprints (including
step-counter metadata), CPU/refresh/deferred-EI state, feedback, exact slice
progress/exit, recent PCM samples and chunk counters. It covers randomized bounded
windows on baseline/cached/portable execution; IM0/IM1/IM2 IRQ wake; input commits;
restore/reload; emitted-to-HALT shared fallback; and observer, plugin, capture and custom execution fallback.
Plugin fallback additionally compares ordered events and logical timestamps.
No audible/live presentation result is claimed by these headless checks.

The final focused build and `smoke-halted-advance` passed (1/1 in 1.37 seconds).
The test executed 1500 direct device-boundary comparisons, 500 randomized machine
windows for each of the three built-in backends, 700 IRQ windows for each supported
interrupt mode, emitted-to-HALT continuation, restore/reload and consumer/custom
execution fallbacks. Bulk counters prove that multiple logical idle polls used
one device advance instead of a loop of full retirements. They are not throughput
or live latency evidence.

The source/archive and binary binding, patch and focused log are retained under
`build-working/completion-validation/halted-advance/gamegear-pilot/`.
Validated source SHA256:
`fb7057ed80caf4f7fab62da35dfe30a090a9a23e4d80ea46e10d68f3330749f9`.
This report's validation status was updated afterward, without rebinding that
archive. The preceding B full-suite evidence predates these changes and is not
claimed as a current D full-suite pass. The Game Gear pilot supplies the focused
parity gate for starting the Game Boy deadline implementation.

The initial Game Gear pilot supplied the parity gate for Game Boy expansion.
The completed both-core study and final validation are reported above. Failed
fixture runs remain retained; no failing performance collection was overwritten
or rerun solely to obtain a pass. Broader consumer support, independent ROM
diversity and live/native-ARM64 acceptance require separate future work before
production review. B's active gate remains closed.

## Game Boy deadline inventory and scope

The Game Boy pilot now uses the same isolated archive and selector. Its minimum
fresh deadline covers the CPU's DIV publication, enabled TIMA falling edge,
serial completion, CPU APU sample/frame sequencer, CPU PPU mode/STAT edge, machine
PPU mode/capture/line event, machine APU sample/frame event, and the 4096-logical-
poll periodic save boundary. Pending IRQ, STOP/HALT-bug/deferred-EI state,
unconsumed cycle charges, invalid phases or pending device notifications force
ordinary stepping. Active DMA has a four-T-cycle byte cadence and always falls
back. Budgets retain the existing single-poll soft overshoot.

A bulk span combines only complete four-T-cycle idle polls strictly before the
nearest event. CPU `retireInstruction(totalCycles)` still executes its mandatory
per-T-cycle timer, serial, DMA, APU and PPU work. Machine PPU/APU stepping and raw
I/O publication run once for the interior span; event polls use ordinary CPU and
machine retirement. This removes repeated polling dispatch/accounting rather
than deleting required device work. Duplicated CPU and machine device clocks are
bounded independently, including tests that offset them through the public
context API. Eligibility is reevaluated before each bulk span, so an IRQ handler
callback that replaces the executor policy cannot retain earlier permission.

The tests preserve the current timer implementation, which reloads TIMA
immediately rather than implementing the physical four-cycle reload race.
Serial/DMA activation is injected through the existing CPU interceptor store:
ordinary attached-map writes currently synchronize cached I/O without invoking
these CPU-owned triggers. The current DMA implementation reads/writes backing
storage spans, whereas the attached map owns separate WRAM/OAM arrays. The DMA
fixture therefore seeds and compares CPU backing spans as well as full machine
fingerprints. These are current-source parity checks, not evidence that guest
bus activation or physical OAM behavior is correct. Those preexisting hardware
issues are outside this optimization; earlier failing fixture logs are retained.

Direct memory-map analysis capture also forces fallback, preserving every raw
I/O publication. Plugin event tests initialize the manager before requiring a
nonempty ordered event stream. Retirement/fetch consumers, custom policy/IR,
debuggers, capture/snapshot controllers and native trampolines remain on the
ordinary path. The active emitted transition permits its deliberate `NativeIr`
diagnostic path hint; subsequent halted feedback must match exactly.

## Shared-runtime measurement contract

`time-measure-halted-runtime` reuses the authored standalone gameplay ROMs,
input scenarios, committed-tick state checks and observed differential windows.
All eight combinations of four backends and selector off/on use the same
128-poll halted-entry slice limit, lean accounting and immutable binding reuse.
Observed windows conservatively fall back; optimized intervals are checked at
committed ticks against the matched control. The collector binds both generated
sources, binaries, CMake cache, ROM manifest, source archive/patch, environment,
raw output and registrations. It rejects missing/duplicate samples, divergent
warm/tick/final state, absent emitted work and candidates that never reduce
polling. All reports explicitly remain ineligible for backend admission.

HALT-entry slices can contain active IRQ-handler work after waking. The legacy
harness HALT-entry accounting is not an active-instruction count. Only
`haltedAdvance.logicalIdlePolls` reports actual skipped idle polling. Before/after
pairs are reported separately for each backend; emitted competitiveness uses the
fastest existing backend with the same optimization enabled.

`time-perf-halted-scheduling-video` enables the selector on the emulation thread
and runs the existing 16 ms headless video/input scheduling gates and sleep/
catch-up diagnostics. Its video plugin requires ordinary retirement, so its bulk
counter must remain zero: it tests the production-consumer fallback, not optimized
live presentation. Prepared-sample FIFO and frame-age checks remain shared
transport tests. No audible/live result or native ARM64 measurement is claimed.
