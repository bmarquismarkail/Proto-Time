# Concurrency Success Gates

`time-perf-concurrency-success-gates` turns the portable, measurable criteria in
the concurrency implementation plan into CTest failures. It runs against the
production audio drain, video mailbox/decoder, input service, `MachineView`, and
realtime video packet paths.

## Enforced thresholds

| Gate | Threshold | Statistic |
|---|---:|---|
| Prepared audio callback drain | `< 50 us` | p99 of 2,000 samples |
| Audio underruns and post-prime silence | `0` | complete benchmark run |
| Video publish-to-reconstruction age | `< 16 ms` | p99 of 1,000 samples |
| Input publish-to-committed-state handoff | `< 16 ms` | p99 of 2,000 samples |
| Rich video `MachineView` construction | `< 10 us` | warmed p95 of 1,000 samples |
| Rich audio `MachineView` construction | `< 5 us` | warmed p95 of 1,000 samples |
| Deterministic video publication | exact | palette metadata and packed bytes |

The snapshot gates use warmed p95 because the work itself is being measured;
an unrelated host scheduler preemption is not snapshot construction time. Audio,
video, and input retain the tail-latency statistic specified by the plan.

Run the gate directly or through CTest:

```bash
cmake --build build-working --target time-perf-concurrency-success-gates -j4
ctest --test-dir build-working -R perf_concurrency_success_gates --output-on-failure
```

## Criteria requiring specialized validation

- **TSAN clean:** configure a dedicated tree with
  `cmake -S . -B build-tsan -DBMMQ_ENABLE_TSAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo`,
  then build and run the complete CTest suite. Wall-clock thresholds are reported
  but disabled under TSAN because sanitizer overhead is not production latency;
  correctness, underrun, and determinism gates remain active.
- **No hot-path lock contention:** wall-clock timing cannot prove the absence of
  a lock. Code review and TSAN cover synchronization structure, while the audio
  and video p99 gates detect resulting stalls. The production callback and video
  mailbox functions remain the code paths exercised by this benchmark.
- **Input-to-visual response and visible/audible quality:** the automated gate
  enforces host-to-machine input handoff and byte-exact video output. End-to-end
  game response and subjective pops/stutter still require representative ROMs
  and physical host devices, as specified by the scenario matrix in the plan.

The benchmark is intentionally part of default CTest so threshold regressions
are release-blocking rather than informational.
