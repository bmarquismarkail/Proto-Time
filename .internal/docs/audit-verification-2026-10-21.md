# Audio/concurrency audit verification (2026-10-21)

Reviewed `FINAL-RESULT.md` from the harness run
`proto-time-openai-audit-2026-10-21T21-44-14.932084+00-00` against the current
checkout. The harness itself left q1 and q3 incomplete and made no changes.

| Question | Current verification |
| --- | --- |
| Does concurrent VDP load cause audio underruns? | Unverified. The prepared-audio microbenchmark in `tests/perf_concurrency_success_gates.cpp` produces each block synchronously before timing its drain. `tests/perf_headless_scheduling_video.cpp` exercises a Game Boy CPU/PPU and a separate headless render lane, without an audio backend. `tests/smoke_halted_fifo.cpp` compares synchronous FIFO streams. None establishes causation between representative VDP load and live-device underruns. |
| Is wake/jitter budgeting telemetry missing? | Disproven. `TimingService` records configured sleep/batch intervals, sleep overshoot, wake-jitter buckets, catch-up clamps, cycle debt, and wake-burst bounds. `emulator/DiagnosticsJson.hpp` exports these in JSON; the pacing loop in `emulator.cpp` records actual host sleeps and wake bursts. `AudioService` records worker wake latency and FIFO/callback statistics. |
| Is drain synchronization unsafe or missing lifecycle barriers? | Verified for epoch transitions. Steady-state SPSC block publication and consumption use acquire/release indices, but `bumpTransportEpochLocked()` previously reset both indices without excluding the output producer or an in-flight drain. Its source-drain mutex did not cover ready-block publication. `setBackendPausedOrClosed(true)` invokes this same reset, including from `MachineLifecycleCoordinator`. Merely setting the paused flag does not stop an existing callback. |
| Are callback-tail-latency and FIFO-starvation tests missing? | Existing coverage disproves the broad claim: `perf_concurrency_success_gates` measures 2,000 prepared callback drains with a 50 us p99 limit and requires zero underruns/silence. `smoke_audio_service.cpp` checks startup silence and FIFO prefill; `smoke_halted_fifo.cpp` checks prepared FIFO occupancy, underruns, silence, and callback p99. However, these tests do not deliberately starve a primed ready FIFO and assert re-priming. The new lifecycle test adds that narrower missing coverage. `PerfTimingSupport.hpp` explicitly waives wall-clock limits under TSAN, while retaining correctness checks. |

## Verified race and fix

The new `smoke-audio-transport-lifecycle` test has a single emulation/control
source producer, the service-owned audio worker, and a separate drain consumer.
It first requires audible output, then performs 4,000 epoch changes while the
worker and drain are active, stops production while draining continues, and
checks stream recovery after reset/reconfiguration, intentional post-prime
starvation, and subsequent re-priming.

Compiled against the original `HEAD` audio headers with
`c++ -std=c++20 -O1 -g -fsanitize=thread -pthread`, this test exits 66 with a
ThreadSanitizer race between `produceReadyOutputBlock()` assigning
`block.samples` and `drainReadyOutput()` copying the same block. A smaller
independent reproducer reports the same race.

The fix serializes the entire production operation with epoch mutation. Ring
clears suspend callback admission using an atomic state and wait on the control
lane for an already admitted drain to finish. The callback makes one admission
attempt and returns silence during the barrier, with no mutex, waiting,
allocation, or worker notification. Transition silence remains counted as
priming silence. Backend close/join and the existing single control/source-lane
ownership contract are still required before reconfiguration or destruction.

Retained local reproduction logs are under
`build-working/audit-verification-2026-10-21/` (generated, untracked evidence).
Passing headless or sanitizer checks does not establish real-device callback
latency, audible quality, or VDP/audio interference.

## Validation

- The original-header TSAN regression fails with the ready-block race (exit 66).
- The fixed regression passes in a native build.
- `cmake -S . -B build-tsan -DBMMQ_ENABLE_TSAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo`
  and the `time-smoke-audio-transport-lifecycle` target build succeed.
- `ctest --test-dir build-tsan -R '^smoke-audio-transport-lifecycle$' --output-on-failure`
  passes (1/1, 1.79 seconds) without sanitizer reports.
- Normal configure and full `cmake --build build-working -j4` succeed.
- The native CMake regression passes (1/1, 0.10 seconds).
- Focused audio/lifecycle/timing/diagnostics/concurrency/FIFO tests pass (22/22,
  29.27 seconds), including the normal callback p99 gate.
- The full sandboxed run cannot create/bind some sockets or run Python worker
  IPC. Its six affected tests (`smoke-river-xmb-telemetry`, `smoke-scripting`,
  `smoke-netplay-transport`, `smoke-netplay-cli`, `smoke-browser-cli`, and
  `smoke-visual-authoring`) all pass outside the sandbox. These reruns use the
  same generated CTest commands and binaries, with a separate temporary CTest
  log directory to avoid interfering with the full run.
- Full `ctest --test-dir build-working --output-on-failure` completes in 615.56
  seconds: 207/213 pass in the sandbox, with only the six socket/IPC failures
  listed above. All six pass in the targeted unrestricted reruns (3/3 in 2.81
  seconds and 3/3 in 2.29 seconds), accounting for all 213 registered tests.
  `smoke-halted-runtime` passes in 190.25 seconds; both normal scheduling gates
  and the callback p99 gate pass. No full-suite test remains unverified after
  the targeted environment rechecks.
- `git diff --check` passes. Baseline headers, baseline commit identity,
  current source hashes, full CTest details, and before/after sanitizer logs
  are retained with the generated evidence.
