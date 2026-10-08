# Headless scheduling latency repair

The scheduling gate must exercise baseline guest execution and the production
host pacing policy while retaining all four 16 ms limits, 30 frames, six visible
input responses, zero mailbox overwrites, and zero stale-epoch drops.

## Findings and changes

The initial unchanged checkout passed isolated checks and all 184 CTest tests.
Longer diagnostic runs subsequently reproduced intermittent failures. Recorded
failing runs accumulated catch-up clamps; Linux thread CPU measurements showed
that their longest execution batches spent nearly all their time executing.
Wake overshoot and frame residence were measured separately. These observations
identify contributing costs, rather than establish the cause of every historical
failure on this host.

- The render lane called `yield()` continuously between frames. The unchanged
  benchmark consumed 135–139% of one CPU across its two threads. Idle render
  polling now sleeps for at most 250 us, shortened to any pending input deadline.
- Emulation counters were atomic even though they are read only after joining
  the owning lane. They are now lane-owned values, avoiding per-instruction
  atomic updates next to the render lane's completion flag.
- The benchmark repeatedly entered one-instruction machine slices. Its loop
  also cut off the fourth permitted timing slice after its first instruction.
  It now uses the production bounded `runSlice` path, charges each retirement
  through a sink, and checks the slice limit before entering a new timing slice.
- Although adaptive sleep was enabled in its configuration, the benchmark slept
  through the entire interval. It now shares `waitForTimingWake` with the
  executable. The production policy is preserved: coarse sleep followed by a
  bounded yield tail; paused and non-adaptive modes omit that tail; production
  stop checks remain in the tail.
- Profiling found register-view RTTI work on the ordinary interpreter path.
  `LR3592_DMG::execute` skips the sparse-register cross-cast for its known
  concrete canonical memory pool. Alternate snapshots retain the existing cast
  and register publication/restoration behavior.

Catch-up horizon, batch interval, spin limits, cycle charging, and baseline
selection are unchanged. Diagnostics report discarded catch-up time, maximum
update gap, maximum execution batch wall time and matching Linux thread CPU time,
and maximum sleep overshoot. Each latency line reports its own result; a failure
in one metric no longer incorrectly labels the others as failures.

## Evidence

Logs, diagnostic experiments (including unsuccessful intermediate changes),
profiling data, before/after CPU measurements, and final source/build-bound
validation are retained under
`build-working/completion-validation/scheduling-latency/`. The final
`validation.json` distinguishes native performance, native TSAN, and emulated
ARM64 correctness. QEMU timing is not native ARM64 performance acceptance.
Native ARM64 performance remains deferred. Headless results do not replace live
presentation, controller, GPU, or audible acceptance.
