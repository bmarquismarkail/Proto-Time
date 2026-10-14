# Game Boy starvation: investigation of five leads

2026-10-13. Bounded investigation on base `799ad55`, preserving the existing
uncommitted startup-tone fix and clamp diagnostics. No production implementation,
executable, governor, affinity, queue depth, pacing limit or admission policy was
changed by this investigation. The new changes are this report and its Git ignore
exception. Native ARM64 and physical audio/video acceptance remain separate.

## Main finding

Two code changes merit further controlled work: avoiding the CPU's unused legacy
APU work specifically in GameBoyMachine, and moving the hot raw I/O read case
earlier. Neither has been implemented in production or shown to eliminate live
starvation. Pacing also substantially changes host CPU cost and effective clock
frequency; unpaced headroom does not establish live deadline headroom.

## Method and evidence boundary

zvec-grep discovery was followed by current-source checks. Isolated diagnostic
copies of five core source files and the view-construction header were compiled
against the current core libraries. Production files and the executable were
hash-checked unchanged. Operator-new counters and sampled phase clocks run only
in the probe's measurement lane. The initial timing pilot showed approximately
40% instrumentation overhead; phase clocks therefore are not production cost
estimates. Inclusive/nested phase estimates must not be added together, and empty
clock calibration does not remove all nested instrumentation overhead.

The main collection contains 64 runs: three rotated reference/candidate rounds
on user-provided Pokémon Red and Super Mario Land 2 ROMs, one diagnostic pass per
consumer case, and synthetic pure-HALT/active-loop controls. Each real-ROM run
warms for two guest seconds and measures six; synthetic runs measure three.
Consumers are absent, no-op typed plugins, or an in-process audio/video packet
consumer. These model event and packet costs, without a real presenter, output
device, replayed live input or battery-save loading. The constructor default
baseline policy does not execute accelerated blocks; follow-ups explicitly attach
the app's default-step policy and disable the inactive block cache.

Twelve additional explicit-policy runs compare unpaced and fixed-deadline pacing;
four perf runs collect whole-process user cycles/instructions and user task-clock.
All 80 runs match their reference's instruction/cycle totals, before/after machine
fingerprints, and consumer event counts/audio-video stream hashes. Diagnostic
batch labels also match uninstrumented reference batch instruction/cycle counts.
These are bounded software parity checks, not full hardware conformance or
performance qualification. Three repeats do not establish confidence bounds.

Raw records, source archives, executable copies, build/source/ROM hashes, recipes,
failed parsing logs and summaries are retained under:
`build-working/completion-validation/gameboy-starvation-leads/`.
ROM binaries remain outside the Git evidence. Initial pilots are retained but
excluded from the registered collection. The original nine dirty files remain
unchanged; the prior 211/211 suite result was not rerun for scratch-only probes
and documentation additions.

## 1. Two APU implementations: strongest code lead

CPU retirement advances `LR3592_DMG::stepApuOneCycle()` for every guest cycle;
machine retirement additionally advances `GameBoyAPU`. Attached-memory writes
route through GameBoyMemoryMap's observer to the machine APU. Published PCM,
packets and the `gb.apu` save-state chunk use the machine APU. The CPU's SaveState
does not contain its legacy APU state, although standalone CPU APIs still expose
that legacy audio history/counter.

An isolated candidate omitted only the legacy CPU APU tick. Median paired CPU
time reductions were 13.69% for Pokémon Red and 13.90% for Mario; all three pairs
were positive on each ROM. Machine fingerprints and audio/video stream hashes
matched. Legacy CPU-private audio history/counters intentionally differ and are
not covered by that parity claim. This supports an explicit machine-owned mode,
not deleting standalone CPU audio support or claiming arbitrary adapter parity.

Relevant source: `cores/gameboy/gameboy.cpp` retirement/legacy APU;
`memory/templ/MemoryPool.impl.hpp` attached-store routing;
`cores/gameboy/GameBoyMachine.cpp` published audio and save-state ownership.

## 2. Raw register access: read routing wins this screen; store skipping does not

Diagnostic windows publish roughly three million raw I/O values per guest
second, with 98.28% unchanged in Pokémon Red and 98.15% in Mario. LY, STAT and
NR52 account for most unchanged publications. About 42% of Pokémon Red's raw
reads are in the FF page and about 43% are VRAM; repeated raw access is not merely
instruction fetch.

Moving the exact FF00–FF7F raw array-read case before the generic region chain
reduced paired CPU time by median 5.66%/5.36% for Pokémon Red/Mario, with three
positive pairs on each. The prototype retains read wrappers/capture behavior;
boot-ROM overlays cannot overlap this range because they are capped at 256 bytes.

Skipping only unchanged array stores, after preserving capture-record emission,
gave median reductions of -0.49%/-0.63%; only one of three pairs was positive per
ROM. A frequent unchanged store is not automatically a profitable optimization.
Do not use the 98% count as justification for removing observable retirements or
capture publications.

Relevant source: `GameBoyMachine::onInstructionRetired`,
`GameBoyMemoryMap::readRaw`, `setIoRegisterRaw`, and `mapBootRom`.

## 3. HALT polling: dominant volume, not established as the only slow-batch cause

HALT polls account for 94.64% of retirements in the Pokémon Red window and 86.26%
in Mario. The pure-HALT control performs 1,048,576 retirements per guest second;
the JR-loop active control performs 349,525. For nearly equal guest-cycle work,
median uninstrumented CPU cost is 324.27 versus 279.94 ms per guest second.
This exposes dispatch/per-retirement overhead even while halted.

Current baseline goes through empty fetch/decode/execute scaffolding and then
retires four cycles. The existing research bulk path excludes both retirement
observers and plugins, which are present in the app. A future direct idle-poll
path must preserve four-cycle accounting, interrupt/DMA/EI transitions, feedback,
device work and observer/event ordering; simply removing those guards is unsafe.

Unpaced packet references had only five Pokémon Red and seven Mario batches over
1 ms across the three rounds. These include HALT-heavy, mixed and active-heavy
batches. Their matched-position candidate improvements are subject to reference
outlier selection/regression to the mean and are not tail-latency proof.

## 4. View construction: real allocations, lower priority

No-op and packet cases construct 53,065 views over the Pokémon Red window and
53,453 over Mario's six seconds, with one C++ allocation per view. Metadata is
224 bytes per view: approximately 8.8–8.9 thousand allocations and 2 MB of
allocated payload per guest second. These are event-level descriptor vectors,
not full machine snapshots per instruction.

No-op versus no-plugin median CPU increases were 0.65%/6.43%, but pairs varied
considerably and include event dispatch as well as view construction. Packet
consumption adds other costs. This does not isolate a repeatable allocation-only
benefit or justify an ownership/API change ahead of the stronger leads.

Relevant source: `machine/Machine.hpp::view` and plugin event dispatch.

## 5. Scanline rendering: measurable work, cache candidate inconclusive

The windows captured 51,103/51,612 scanlines and made millions of per-pixel
background reads. The realtime packet path normally reuses captured indexed
pixels; the debug adapter's full-frame rebuild is not evidence of a duplicate
default realtime render.

An isolated 64-entry VRAM cache was scoped to a single scanline capture, with
CPU-backed reads and other rendering paths unchanged. Machine/stream hashes
matched, but median paired reductions were 1.54%/5.05%, with a negative pair on
each ROM. Results are mixed, not qualification. Preserve scanline-time capture
and palette/sprite semantics if pursuing a more efficient tile-row implementation.

## Additional lead: pacing and host power behavior

Attaching the actual default policy preserved all state/stream checks. Adding
fixed guest-deadline sleeping to the otherwise same probe increased measured
batch CPU time by 45.1–58.6% for Pokémon Red and 45.7–53.0% for Mario across three
pairs. Batches exceeding 1 ms changed from 0–1 per unpaced run to 39–94 and 38–71
per paced run, respectively. No catch-up budget was discarded in this fixed-guest
comparison, so guest state stayed identical.

Single perf pairs corroborate this interaction:

| ROM | Unpaced approximate user GHz | Paced approximate user GHz | User instruction increase | User cycle increase |
| --- | ---: | ---: | ---: | ---: |
| Pokémon Red | 3.73 | 2.54 | 0.0039% | 6.37% |
| Mario | 3.77 | 2.91 | 0.0038% | 9.17% |

GHz is user hardware cycles divided by user task-clock, averaged over the whole
process including warmup/serialization. It is not the frequency of an individual
clamp event. Current governor reads reported `powersave` on the i7-8650U host.
The evidence supports lower effective frequency and additional cycle cost as
pacing-related contributors; it does not attribute the extra cycles specifically
to cache misses, prove the governor alone caused all live underruns, or justify
changing host settings without a separate controlled comparison.

## Recommended order

1. Pursue an explicit GameBoyMachine-owned audio mode that avoids unused legacy
   CPU audio, preserving standalone CPU behavior and testing lifecycle/IR/observer
   contracts before any production change.
2. Validate the hot raw-I/O read ordering across complete memory/capture/boot
   contracts and more representative windows.
3. Measure actual paced batch frequency/core residency and wake behavior; compare
   power policies only as a separate authorized experiment. Do not infer live
   headroom from unpaced throughput or increase queues to hide lost guest time.
4. Keep retirement-preserving HALT dispatch simplification as a separate candidate.
   Defer view ownership changes, same-value store suppression, and this scanline
   cache until they show a clearer, repeatable benefit.

## Subsequent implementation and bounded verification

The first two recommendations were subsequently implemented on the same dirty
base. GameBoyMachine explicitly disables the CPU's standalone APU clock, while
direct CPU instances retain it by default. The setting is host configuration,
outside guest save state. Timer/interrupt/serial/DMA/LCD retirement work remains
active. Research HALT deadlines omit the disabled CPU audio clock and still
include the machine APU deadline. Machine audio frame bookkeeping now reads the
machine APU. Raw I/O reads take the direct FF00–FF7F case before the region chain;
boot mapping, capture wrappers and writes retain their existing contracts.

The final build and nine focused regressions passed, including standalone tone
generation, disabling/re-enabling its clock, divider progress while disabled,
retaining the setting through CPU restore/reset, machine startup silence and
guest-triggered sound, machine/APU state round trips, PPU and HALT equivalence.

Twenty-four uninstrumented explicit-policy probe runs form twelve before/after
pairs: three repeats per ROM and pacing mode, with pair order alternated.
Each warms for two guest seconds and measures six. All pairs match machine
fingerprints, guest instruction/cycle totals, and complete emitted PCM/video
stream hashes. CPU-private standalone audio histories intentionally stop
advancing in machine mode; they are outside the machine fingerprint contract.

| ROM | Pacing | Median batch CPU reduction | Positive pairs |
| --- | --- | ---: | ---: |
| Pokémon Red | Unpaced | 19.98% | 3/3 |
| Mario | Unpaced | 17.80% | 3/3 |
| Pokémon Red | Fixed guest deadlines | 5.11% | 2/3 |
| Mario | Fixed guest deadlines | 2.33% | 2/3 |

Paced reductions range from -1.75% to 5.39% for Pokémon Red and -10.18% to
3.39% for Mario. Pokémon Red batches exceeding 1 ms change from 32/21/63 to
104/25/48; Mario changes from 88/30/110 to 90/20/44. This is mixed deadline
evidence, not proof that live starvation or clamping has been resolved. These
short probes do not contain physical playback, presentation or live input;
power policy and clock behavior remain uncontrolled. No formal acceleration
qualification or native ARM64 claim follows from this engineering screen.

Reproducible sources, both probe binaries, bindings, raw pairs, summaries and
validation logs are retained under
`build-working/completion-validation/gameboy-starvation-implementation/`.
Host settings, queue depths, timing limits and production acceleration admission
remain unchanged. A fresh live baseline session must use a new diagnostics path,
such as `/tmp/time-live-gameboy-04.jsonl`, and retain its companion events file.

Full CTest completed in 622.90 seconds with 210/211 passing. The unchanged
`perf_halted_scheduling_video` input-to-frame p99 gate failed once at 19.23 ms
against 16 ms (five clamps in that run); its other three latency gates passed.
Three subsequent unchanged targeted repeats passed at 11.85, 11.85 and 11.91 ms,
with zero clamps. The original failure is retained, and the full suite is not
reported as clean. These repeats show that the failure did not immediately
reproduce; they do not establish its root cause or remove the timing risk.

Both user ROMs also completed separate 20-second headless, no-output-audio
application checks with 21 valid diagnostic snapshots each and graceful shutdown.
Those checks verify application integration and reporting, not physical playback
or live starvation acceptance. Final source/library/probe/application hashes
matched the recorded binding. All source changes remain uncommitted.

The application checks still recorded catch-up clamping: Pokémon Red had 40
clamps and 50,631.9 discarded cycles (about 12.07 ms); Mario had 457 clamps and
820,046 discarded cycles (about 195.51 ms). There is no matched pre-change
application control for these runs, and output audio was disabled. They show
that clamping remains possible; they do not quantify a live before/after gain.

## Live session 04 acceptance

The user confirmed the same Pokémon Red ROM and gameplay pattern as session 03,
with a few audible clicks, no noticed gaps and no startup beep. Both logs contain
600 snapshots over approximately 599.5 seconds. Their reported baseline policy,
balanced timing profile, 1 ms batch/minimum sleep settings, 48 kHz mono audio,
256-sample chunks and three-chunk ready queue match.

| Metric | Session 03 | Session 04 |
| --- | ---: | ---: |
| Audio underruns | 114 | 1,255 |
| Cumulative zero-filled audio | 608 ms | 6,693 ms |
| Catch-up clamps | 3,359 | 35,218 |
| Discarded guest time | 1.400 s | 15.225 s |
| Frames submitted/presented | 35,695/35,695 | 34,842/34,842 |
| Ready drops / mailbox overwrites / fallback presents | 0/0/0 | 0/0/0 |
| Audio callback maximum | 41.313 us | 36.919 us |

The companion events exactly match each session's cumulative clamp count.
Session 04 has 34,739 events whose previous batch CPU time exceeds 1 ms;
the conditional median is 1.324 ms versus 1.351 ms in session 03. These are
clamp-conditioned samples, not overall batch distributions. Source occupancy is
below one 256-sample chunk at 34,877 clamps, and the ready queue is empty at
11,132. Underruns occur throughout the run (636 first half, 619 second half),
so this is not only a startup fault. The callback maximum remains short.
There is also one background task failure in session 04; counters alone do not
identify its cause or connect it to starvation.

Live starvation acceptance has not passed. The code changes and host response
must be isolated with a matched source-bound control before assigning causality;
the unpaced CPU improvements do not justify accepting this live regression.
Keep both changes uncommitted pending that comparison. Review and input hashes
are recorded in `/tmp/time-live-gameboy-04-review.json`; retain the raw session
and companion events files. Cumulative zero-fill is not a single continuous gap.

## Controlled frequency and source A/B follow-up

Isolated diagnostic builds reconstruct the pre-change behavior, audio-only change,
I/O-only change and combined implementation. They share current CPU headers and
linked dependencies; reconstructed controls restore unconditional CPU APU ticking,
machine startup audio bookkeeping and the original raw-I/O region ordering.
They preserve the startup-tone fix and clamp instrumentation. A retained original
pre-change probe independently matches the reconstruction's 20-second guest
fingerprints, instruction/cycle totals and PCM/video hashes.

The final paired analysis uses three complete rotated rounds (1, 2, 3), each
warming for two guest seconds and measuring twenty. Twelve candidate/reference
pairs cover Pokémon Red's three configurations and Mario's combined change.
All match guest state and emitted output. Four unpaced controls and initial
exploratory runs are retained separately. Hardware events ran at 100% in the
collected counter files. Probe counters cover the whole single-thread process,
including warmup and reporting; batch CPU time covers the measured guest window.
Effective GHz is user hardware cycles divided by task-clock, not a per-event
frequency measurement. This remains an engineering screen, not qualification.

| ROM / candidate | Median hardware-cycle reduction | Instruction reduction | Effective-clock reduction | Batch CPU-time reduction |
| --- | ---: | ---: | ---: | ---: |
| Pokémon Red / audio only | 10.22% | 11.38% | 5.51% | 6.31% |
| Pokémon Red / I/O only | 4.02% | 2.65% | 2.34% | 1.24% |
| Pokémon Red / combined | 17.97% | 14.01% | 10.52% | 8.61% |
| Mario / combined | 15.98% | 13.79% | 9.38% | 5.66% |

Deadline tails do not follow those aggregate gains. Pokémon Red combined counts
of batches exceeding 1 ms are 1,876/3,299/3,112 versus 2,435/2,376/3,139 before.
Mario combined counts are 3,519/2,687/2,944 versus 3,051/2,519/2,321 before;
all three Mario pairs worsen on that measure.

Six separate application controls use identical frozen initial ROM/save bytes,
two host seconds of startup and twenty seconds of emulation-thread counters.
Each launches headless without output audio. Source inspection confirms guest
execution is a separate thread; the dominant non-UI worker was selected and
accounted for 89–94% of initial process thread CPU ticks. Its mean effective
clock is approximately 1.61 GHz before and 1.41 GHz combined (group medians).

| Pair | Before clamps / discarded guest ms | Combined clamps / discarded guest ms |
| --- | ---: | ---: |
| 0 | 613 / 313.15 | 1,345 / 721.14 |
| 1 | 785 / 376.95 | 1,313 / 641.80 |
| 2 | 1,112 / 487.78 | 1,250 / 627.61 |

These counts cover the complete approximately 22-second application runs,
including startup, rather than just the counter interval. Clamping worsens in
all three matched pairs. At 100 ms sampling intervals, observed emulation-core
changes are 63/59/61 before and 88/82/81 combined; these are lower bounds on
migrations. Perf's software context-switch/migration counters returned zero,
inconsistent with those observed CPU changes, and are excluded from conclusions.

The host reported `powersave` and `balance_power`; recorded governor, EPP and
frequency limits matched before/after, and no settings or affinity were changed.
One mid-run thermal snapshot showed CPU/package values around 50–54 C, not a
thermal history. The user reported no known power-state change between sessions
03 and 04. Lower effective clock and increased observed core movement accompany
the reduced work and help explain weaker deadline gains. A separately authorized
power-policy comparison is still needed to establish that policy's causal role;
these controls do not retrospectively measure session 03 or 04 CPU frequency.

Two collection issues are preserved and corrected: the initial application perf
scope measured the UI thread, so its frequency values are excluded; and unpaced
filenames overwrote four first-round paced raw records. Their summaries remain
in `collection-before-repair.json`, with collision markers in the final collection.
The affected factorial round was repeated under distinct names as round 3; all
round-0 records are excluded from the paired table above. The revised recipe
uses pacing-specific filenames. Initial launcher smoke checks also exposed an
exit-time `/proc` race; it was fixed and both variants were rechecked successfully.

The optimization candidates have not passed deadline acceptance. Production
source/binary bindings remain unchanged during this follow-up and changes remain
uncommitted. Next, confirm with matched live SDL runs and consider a separate
temporary power-policy control before accepting either optimization as a default.
Do not treat lower work alone as sufficient or alter the acceptance limits.

Evidence and recipes reside under
`build-working/completion-validation/gameboy-starvation-controlled-ab/`:
`analysis.json`, `app-emulation-controls.json`, raw counters/CPU samples,
`final-binding.json`, source overrides and both sets of binaries. The validated
`launch-live.py` freezes the same user ROM/save template for each live run,
selects the emulation thread for perf, records CPU/frequency samples and host
settings, and refuses existing report filenames. It uses explicit SDL module
paths. Its two final five-second headless checks and overwrite guards passed;
physical/live playback is still pending.

From repository root, run the same gameplay sequence for each configuration:

```bash
python3 build-working/completion-validation/gameboy-starvation-controlled-ab/launch-live.py before /tmp/time-live-gameboy-05-before.jsonl
python3 build-working/completion-validation/gameboy-starvation-controlled-ab/launch-live.py combined /tmp/time-live-gameboy-05-combined.jsonl
```

Each collects the regular report/events plus `.perf.jsonl` and `.frequency.json`
sidecars. Exit the emulator normally after each matched session. Original ROM,
save files and sessions 03/04 are preserved; each control writes to its own copy.

## Live session 05 pair

Both launcher runs completed normally, with matching initial ROM/save hashes,
baseline policy, balanced timing and recorded `powersave`/`balance_power`
settings. Counter output exists at 100% running time and perf stderr is empty;
perf's recorded -2 exit reflects SIGINT shutdown, not missing counters. The
selected non-UI thread has the dominant initial CPU ticks in both runs. There
is no input replay. The user subsequently confirmed the same gameplay sequence,
a slightly longer before run, a possible workspace switch during before, and
audible clicks in combined. Gaps and startup-beep status were not specified in
that reply. The possible workspace switch is consistent with the later rendering
disturbance, but does not prove its cause.

| Metric | Before | Combined |
| --- | ---: | ---: |
| Host duration | 95.39 s | 91.44 s |
| Underruns | 103 | 190 |
| Underruns/minute | 64.78 | 124.67 |
| Catch-up clamps/second | 30.21 | 59.96 |
| Discarded guest time | 1.350 s | 2.250 s |
| Discarded guest time / host time | 1.42% | 2.46% |
| Approximate emulation-thread effective clock | 1.703 GHz | 1.458 GHz |
| Ready queue drops | 0 | 0 |
| Mailbox overwrites | 1,952 | 0 |

The before run has a separate rendering disturbance from roughly 56–94 seconds:
presentation falls to about one frame per second and maximum presenter duration
reaches 1.001 seconds. The final report has 5,592 submitted and 3,640 presented
frames. Combined presents all 5,302 submitted frames, with a 7.99 ms presenter
maximum. Whole-run clock comparisons include this rendering-condition difference;
they cannot establish power-policy causality or quantify a fixed-work speedup.

The earlier uncontaminated comparison uses snapshots at approximately 54 seconds,
before either run records a mailbox overwrite:

| Early-window metric | Before | Combined |
| --- | ---: | ---: |
| Underruns | 50 | 125 |
| Catch-up clamps | 1,408 | 3,551 |
| Discarded guest time | 0.688 s | 1.509 s |
| Discarded guest time / host time | 1.27% | 2.79% |
| Mailbox overwrites | 0 | 0 |

Thus worse audio/clamping already appears before the later rendering stall.
Both configurations still starve, and the combined candidate does not pass live
acceptance in this pair. The results support withholding the two optimizations
and favor restoring pre-change behavior while preserving startup silence and
the diagnostic instrumentation. A separate controlled power-policy comparison
remains necessary to assign that policy's causal role. No source defaults or
host settings were changed during this review.

Raw files and bindings plus normalized metrics are recorded in
`/tmp/time-live-gameboy-05-review.json`. These short runs supplement the longer
sessions; they are not a full repeated ten-minute acceptance campaign.

## Rollback after session 05

At the user's request, both production optimizations were reverted: CPU APU
ticking, audio bookkeeping/HALT deadlines and raw-I/O read ordering are restored.
The experimental standalone-clock API and its associated tests/documentation
were removed. Startup-trigger suppression, its silent-start/guest-trigger tests,
clamp/batch diagnostics, reports and experiment artifacts remain preserved.

All nine startup-fix/diagnostic source hashes match their recorded pre-optimization
binding. CPU sources/header, memory-map source and memory-map state test match
base `799ad55`. The full rebuild and nine focused tests passed. Full CTest passed
211/211 in 725.78 seconds, including the unchanged HALT pacing gate. Validation
logs, restored-source binding and executable hash are retained under
`build-working/completion-validation/gameboy-starvation-rollback/`.
The normal `build-working/timeEmulator` now uses the restored behavior. This rollback does not claim the original starvation is fixed.
