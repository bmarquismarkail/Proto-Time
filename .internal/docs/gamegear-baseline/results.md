# Post-merge Game Gear baseline — November 3, 2026

Source: `6e5ffd62ef6e20b8483aaf557b1653acc9927dd4`, verified remote `master` before execution. Checkout branch: `working`; local `master` was older. Candidate collection used uncommitted changes recorded in the source snapshots.

## Untouched-master results

Completed 21 serial 120-second runs (three per ROM) and two 600-second gameplay soaks. Every completed run used baseline CPU execution, balanced 1× pacing, the live SDL XWayland frontend and a PipeWire/PulseAudio output device. The first ten seconds are separated from interval-speed measurements.

| ROM | Completed runs | Rendering | Audio | Physical input | Host timing | Total post-prime underruns | After-startup underruns |
|---|---:|---|---|---|---|---:|---:|
| sonic2 | 4 | unverified | fail | unverified | pass | 13 | 8 |
| megaman | 4 | unverified | fail | unverified | fail | 54 | 50 |
| dracula | 3 | unverified | fail | unverified | pass | 3 | 0 |
| buttons | 3 | unverified | fail | unverified | pass | 3 | 0 |
| psg | 3 | fail (missing text/tiles) | fail | unverified | pass | 3 | 0 |
| collect-forward | 3 | unverified | fail | unverified | pass | 3 | 0 |
| collect-reverse | 3 | unverified | fail | unverified | pass | 5 | 2 |

ButtonTest passed all seven automated press/release indicator checks in all three runs. This does not establish physical keyboard or controller acceptance. Other rendering fields remain unverified pending human review; selected Sonic and Mega Man captures align with recognizable reference scenes.

Sonic's gameplay soak had ten underruns, eight after startup, with median interval speed approximately 1.0000× and no out-of-band intervals. Mega Man's soak had 51 underruns and five out-of-band intervals. Its median remained approximately 1.0000×; the sustained interval criterion correctly failed. A recorded host sleep overshoot of approximately 1.90 seconds and a 3.10-second frontend tick delay accompany that failure. This establishes observed host pacing/transport loss, not a proven guest CPU timing defect.

## Artifact map

All paths below are relative to the repository root:

- `build-working/gamegear-baseline/6e5ffd62ef6e20b8483aaf557b1653acc9927dd4/campaign-20261103/`: 21 short runs, raw diagnostics/events, logs, exit codes, checkpoint PNGs and sink-monitor WAVs. The original movement-soak attempt was interrupted and preserved.
- `build-working/gamegear-baseline/6e5ffd62ef6e20b8483aaf557b1653acc9927dd4/gameplay-soaks-20261103/`: two completed gameplay soaks. These omit blind directional movement, retaining calibrated game admission and jump/fire; periodic images confirm the players remain in their levels.
- `build-working/gamegear-baseline/6e5ffd62ef6e20b8483aaf557b1653acc9927dd4/baseline-aggregate.json`: combined 23-run record and identical baseline binary identities.
- `build-working/gamegear-baseline-reference-campaign-20261103/`: independent Genesis Plus GX captures, core hash/options, exact scenarios, synthesized WAVs and emulated-time PNGs. Scene alignment is required; these are not device-audio evidence.
- `build-working/gamegear-baseline/6e5ffd62ef6e20b8483aaf557b1653acc9927dd4/verification-20261103/`: baseline configure/build/test logs, including failed/interrupted attempts. Full baseline CTest passed 216/216 in 629.03 seconds.
- `build-working/gamegear-baseline/6e5ffd62ef6e20b8483aaf557b1653acc9927dd4/baseline-regressions-20261103/`: three failing baseline regressions, exact commands and retained diagnostics.

Each session binds ROM hashes/header bytes, source commit and dirty diff, runner/manifest hashes, binary hashes, CMake cache/compile commands, compiler, CPU, display, audio sink and actual per-process stream identity. The successful remote-master preflight is retained explicitly after a subsequent SSH retry timed out.

## Targeted remediation and verification

The candidate configured and built successfully. All 19 focused checks and the three new regressions passed. Full CTest completed **218/219** in 826.80 seconds: `perf_headless_scheduling_video` exceeded its 16 ms scheduling-to-present gate once (20.58 ms); three isolated reruns passed unchanged. The original exit status and logs remain in `build-working/gamegear-candidate-verification-2/`, alongside source snapshots. An earlier candidate attempt caught a second missing occupancy-export layer; that failed attempt is retained separately. No perfect full-suite result is claimed.

Candidate live reruns completed; the separate results appear below. Human acceptance remains unverified.

- Correct register-only IX/IY byte operands while preserving ordinary H/L for indexed memory and DD/FD-CB destination exceptions. Baseline regression fails; CPU-only diagnostic candidate passes and restores the SN76489 GUI. The ROM trace executes `FD 7D` (`LD A,IYL`) at PC `0x3809`. The local source archive contains the identical cartridge hash; it was inspected, not rebuilt.
- Increase startup/recovery prefill to three blocks when capacity permits, retaining the two-block steady-state low-water policy and one/two-slot compatibility. This is a startup mitigation candidate, not a claim that all endurance audio loss is fixed. A modeled three-callback burst fails on baseline.
- Copy actual drain-occupancy counters through both `AudioService::transportStats()` and the frontend diagnostics adapter into the existing diagnostics fields. Baseline reported zero occupancy samples despite 535 drains. Treat its all-zero drain histogram as unavailable, not evidence of an empty FIFO; its separately exported ready depth/high-water counters remain usable.

Index-byte semantics follow chapter 3 of [Sean Young’s hardware research](https://www.grimware.org/lib/exe/fetch.php/documentations/devices/z80/datasheets/the.undocumented.z80.documented.2005.sean.young.pdf). Reference retropad mapping follows [Genesis Plus GX](https://github.com/libretro/Genesis-Plus-GX/blob/master/libretro/libretro.c).

## Candidate results

Completed **9/9** targeted device reruns: one 120-second run per ROM and both 600-second gameplay soaks. All use the same candidate binary hashes, preserved in `candidate-aggregate.json`. These are not a second three-round baseline. No runtime source or binary changed during measurement.

| ROM | Rendering | Audio | Physical input | Timing | Underruns / zero-filled samples |
|---|---|---|---|---|---:|
| sonic2 | unverified | fail | unverified | pass | 1 / 512 |
| megaman | unverified | unverified | unverified | pass | 0 / 0 |
| dracula | unverified | unverified | unverified | pass | 0 / 0 |
| buttons | unverified | fail | unverified | pass | 1 / 512 |
| psg | unverified | unverified | unverified | pass | 0 / 0 |
| collect-forward | unverified | unverified | unverified | pass | 0 / 0 |
| collect-reverse | unverified | unverified | unverified | pass | 0 / 0 |

PSG text/digits/keyboard tiles are restored in the candidate and agree with the reference layout; full rendering acceptance remains unverified. The side-by-side image is `build-working/gamegear-baseline/6e5ffd62ef6e20b8483aaf557b1653acc9927dd4/review/psg-startup-comparison.png`. Wall-time checkpoint comparison is not pixel equality or hardware validation.

All nine candidate runs had no out-of-band post-startup speed intervals. Median interval speeds range from 0.999987× to 1.000022×. Sonic’s soak recorded one after-startup underrun (512 samples), compared with ten total in its original soak. ButtonTest also recorded one after-startup underrun around 85 seconds; all seven automated press/release pairs passed. Mega Man’s candidate soak had no audio loss and no out-of-band intervals. Its baseline host-stall failure remains valid; no pacing fix or causal elimination is claimed.

All candidate ready-queue drop counters were zero. Audio callback p50/p95/p99/p999 are all ≤50 µs bucket ceilings. Per-run occupancy buckets, callback tails, and full timing/video state are in `candidate-aggregate.json`. Sink-monitor WAVs are retained for all nine runs (approximately 119.9–120 s or 599.9 s), with capture route and recorder exit status in each metadata file. Recordings establish a capture route, not listener confirmation.

| Run | Sleep overshoot max (ms) | Catch-up clamps | Discarded cycles | Frame-age max (ms) | Present-duration max (ms) |
|---|---:|---:|---:|---:|---:|
| psg/1 | 2.807 | 0 | 0.00 | 1.629 | 5.008 |
| sonic2/1 | 2.685 | 1 | 1608.19 | 1.155 | 7.754 |
| megaman/1 | 2.739 | 1 | 2602.80 | 2.769 | 3.690 |
| dracula/1 | 2.693 | 0 | 0.00 | 1.597 | 6.813 |
| buttons/1 | 3.442 | 0 | 0.00 | 2.397 | 12.498 |
| collect-forward/1 | 2.810 | 1 | 3357.09 | 2.793 | 9.795 |
| collect-reverse/1 | 2.679 | 0 | 0.00 | 2.561 | 12.826 |
| sonic2/soak | 3.012 | 1 | 3246.62 | 3.033 | 6.274 |
| megaman/soak | 3.467 | 3 | 8731.51 | 3.542 | 7.348 |

Candidate artifacts under the same SHA directory: `candidate-psg-20261103/`, `candidate-corpus-20261103/`, `candidate-soaks-20261103/`, `candidate-aggregate.json`, and `candidate-metrics.md`. The separate intrusive startup traces in `startup-traces-20261103/` show baseline 1 underrun/512 samples versus candidate 0 during a one-second comparison; they explain startup behavior and are not normal-rate acceptance.

Focused ThreadSanitizer startup-burst and concurrent audio-lifecycle checks passed **2/2** after device collection; logs and exact commands are in `build-working/gamegear-candidate-tsan/`. The full candidate suite remains **218/219**, with the single timing gate passing three unchanged isolated reruns. The baseline full suite passed **216/216**.

Reproduction commands and dependencies are in `README.md`; per-ROM SHA/header/scenario inputs are bound in `corpus.json` and each session snapshot. `matrix.json` is the published acceptance snapshot. Candidate collection used the source changes retained in the verification snapshots.

## Acceptance still required

Artifact inspection confirmed the restored PSG GUI, active Sonic and Mega Man stages, Dracula gameplay/game-over transition, and ButtonTest indicator behavior. This is bounded checkpoint review, not continuous gameplay acceptance. Sonic's ordinary late checkpoint is black in both emulator and reference; its earlier gameplay images and separate safe soak are the useful visual evidence.

Human visible-gameplay, audible-output and physical-keyboard review remain pending. Each session includes `human-review.json` for a named reviewer to record these observations without altering the raw diagnostics. A suitable physical controller was not connected. Sink-monitor recordings can contain other applications using that sink. Runtime callback percentiles are histogram bucket estimates. Optional video/worker histograms with no populated bucket counts are unavailable; their zero percentile fields do not mean zero latency. The separately populated last/high-water timing values remain in the artifacts. Occupancy low-water counters can retain their initialized zero; use the actual bucket counts to interpret sampled depths. No full-game completion, hardware-accuracy or ARM64 claim is made.
