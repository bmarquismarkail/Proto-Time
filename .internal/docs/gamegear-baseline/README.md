# Post-merge Game Gear baseline

This campaign separates machine smoke coverage, live host transport evidence,
reference-core comparisons, and human acceptance. No commercial ROMs are stored
in the repository. `corpus.json` pins each local cartridge by SHA-256 and records
its scenario, checkpoints, and optional soak. Sega header bytes are recorded in
each session; a missing header is reported rather than guessed.

The recorded campaign results, failures, candidate comparison and review blockers are in
[`results.md`](results.md). `matrix.json` is the machine-readable acceptance snapshot.

## Reproduce

From the repository root, verify HEAD is the intended remote `master`, then:

```sh
cmake -S . -B build-working
cmake --build build-working -j4
ctest --test-dir build-working -N
ctest --test-dir build-working --output-on-failure
python3 tests/fixtures/space/port-game/build.py build-working/gamegear-baseline-fixtures/forward --system gg
python3 tests/fixtures/space/port-reverse-game/build.py build-working/gamegear-baseline-fixtures/reverse --system gg
python3 tools/gamegear_baseline.py
```

Run build/tests before collection; do not benchmark concurrently with compilation
or smoke/performance tests. The full serial campaign takes at least 62 minutes:
21 two-minute runs and two ten-minute soaks, plus capture/shutdown overhead.
Requirements: live X11/XWayland, SDL frontend/audio modules, PulseAudio/PipeWire,
`pactl`, `ffmpeg` with pulse and x11grab support, Python 3, and libX11.
Long collections should be launched in an isolated process session and monitored through
the retained logs/session JSON so an interactive terminal interruption does not discard
the campaign. The collector also isolates each emulator and recorder process.

The runner deliberately uses SDL's X11 and pulseaudio drivers; dummy/headless
fallback is never accepted as live evidence. It verifies remote master, ROM hashes,
and binary identity before running; it never changes audio routing or volume.

`--case buttons --repetitions 1 --duration 50 --skip-soaks` is useful for a setup
probe, which is not the complete campaign. `--prepare-only` captures provenance
without launching devices. If a later network check is unavailable, explicitly pass
`--master-verification <earlier-successful-session.json>` to reuse and retain the
already captured remote-master verification for the same selected SHA. This does not
claim a new remote fetch. `--kind candidate --baseline-session <baseline-directory>`
records a separate candidate linked to the original evidence. Existing artifact directories are never reused.

Use `--skip-soaks` for the three ordinary rounds and `--soaks-only --case sonic2
--case megaman` for separate long-run collection. Soaks always last 600 seconds;
`--duration` and `--repetitions` govern ordinary runs. Soak scripts retain the
calibrated Start/menu sequence, remove blind directional movement, and exercise
jump/fire near the starting area. Periodic captures must confirm that gameplay
continues; the ordinary runs retain scrolling/directional coverage.

Artifacts live under `build-working/gamegear-baseline/<sha>/<UTC-session>/`.
Each run retains commands, actual scripted-event times, checkpoint PNGs, process
and audio-capture logs, diagnostics and catch-up-event JSONL, exit codes, and a
48 kHz stereo WAV captured from the selected physical sink's monitor. The monitor
can include other applications; it establishes the host output route, not what a
listener heard. No file-backend PCM is substituted for device capture.

The runner retains its own source, manifest, binary hashes, dirty diff, CMake cache,
compiler/host/device identity and compile commands. A tooling-only dirty tree is
reported explicitly; emulator baseline binaries must originate from the recorded
master build. Do not rebuild binaries during collection. Corrections to the
emulator require a separately labeled candidate campaign and comparison against
these immutable baseline artifacts.

## Interpretation

Timing uses differences in guest cycles and host elapsed time between adjacent
samples beginning after ten seconds. The nominal Game Gear clock is 3,579,545 Hz.
Two consecutive intervals outside 0.95–1.05× flag sustained speed failure; isolated
outliers remain in the report. No intentional pauses are scripted. Cumulative
callback percentile and high-water statistics are labeled as lifetime aggregates;
they are not recomputed by subtracting percentiles. Raw timing diagnostics retain
sleep, jitter, catch-up clamps, discarded cycles, frame age and presentation data.

Any recorded post-prime underrun, zero-fill, or ready-queue drop marks audio as
failed even when average speed is healthy. Missing metrics are not zero. Missing
or incomplete evidence never passes. Rendering, audible output and physical input
remain unverified until reviewed. Synthetic XSendEvent delivery is window-targeted
and does not steal keyboard focus; successful delivery does not prove guest input
handling. Capture images must confirm the intended gameplay checkpoint before a
session can be described as a gameplay soak.

Review startup, after-start, button1, directions and late images for corruption,
scrolling, sprite priority and palette problems. Compare matching scene/phase
against Genesis Plus GX, accounting for scaling/cropping; wall-time images are not
instruction-aligned and cannot support pixel-equality assertions. Existing authored
fixture proof scripts provide stricter committed-tick state/art/audio comparisons.

For human review, record per-ROM visible scene/defects, audible expected tones or
music/noise, directions, Button1/Button2/Start and release behavior. Use ButtonTest
for release checks and SN76489_TestRom for tones. Record reviewer and time. Keyboard
review is separate from synthetic input; controller acceptance stays unavailable
until a suitable physical controller is connected. Do not infer gameplay completion,
hardware accuracy, ARM64 acceptance or acceleration acceptance from these runs.

## Independent reference captures

```sh
python3 tools/gamegear_reference.py --case buttons --output build-working/reference-buttons
```

Run this in its own process for each corpus entry. The helper uses the installed
Genesis Plus GX library, records its hash/options, attaches a retropad explicitly,
and saves emulated-time checkpoint PNGs, audio, and active input-query counts.
Its Sega Button1/Button2 mapping follows the core's retropad B/A mapping in the
[Genesis Plus GX input adapter](https://github.com/libretro/Genesis-Plus-GX/blob/master/libretro/libretro.c).
Reference audio is synthesized output, not device acceptance. Captured status means
artifacts exist; it does not establish correctness or scene alignment. Earlier
setup/reference attempts remain available and must not be substituted for the
final source/manifest-bound captures.
