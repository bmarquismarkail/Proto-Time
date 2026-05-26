# Concurrency Phase Gates

This checklist gates the upcoming concurrency and acceleration phases. Treat each phase as a separate commit series with its own build, smoke tests, and diagnostic comparison against the previous phase.

## Baseline Before Phase 5

Capture a baseline run before changing mailbox, packet, presenter, task-pool, or interpreter behavior.

Required metrics:

- audio callback duration p95, p99, p999, and high water
- audio ready FIFO depth at drain, underruns, overruns, and silence fill samples
- audio worker wake source counts and source-buffered sample high/low water
- video mailbox depth, high water, overwrite count, stale drop count, and generation gaps
- frame age buckets and high water
- debug/realtime frame build counts and total build nanoseconds
- presenter duration p95, p99, p999, texture upload count, render count, and fallback count
- timing sleep overshoot, catch-up burst cycles/slices, and frontend tick delay

Store baseline notes under `.internal/` with the command used and the exact commit hash.

## Common Gate For Every Phase

Each phase must satisfy these before commit:

- `cmake -S . -B build-working`
- `cmake --build build-working -j4`
- `ctest --test-dir build-working --output-on-failure`
- targeted smoke tests for the changed subsystem
- no generated build artifacts staged
- no experimental source left untracked in live source directories
- public diagnostics either remain meaningful or are explicitly deprecated/removed
- machine-owned deterministic state remains on the emulation thread
- audio callback drains prepared device-rate samples only
- UI/render thread owns host events, window affinity, texture upload, and present
- background workers do not touch guest CPU/APU/PPU state directly

## Phase 5: Convert Video Mailbox To True Latest-Only

Acceptance criteria:

- mailbox has latest-only semantics with a single visible newest generation for the consumer
- stale generations are dropped deterministically and counted
- presenter never presents an older generation after a newer one has been accepted
- diagnostics distinguish overwrite, stale drop, and empty/fallback presentation
- smoke coverage proves out-of-order and burst submission behavior

Suggested focused tests:

- submit generations 1, 2, 3 before consume; consume returns only 3
- consumer cannot observe generation 2 after generation 3 has been published
- fallback behavior remains stable when no frame exists

## Phase 6: Slim Real-Time Video/Audio Packets

Acceptance criteria:

- packet ownership and lifetime are explicit
- `contractVersion` changes are intentional and tested
- packet empty/invalid behavior is deterministic
- copies of large pixel/audio vectors are reduced or justified by measurement
- plugin-facing ABI/API compatibility is documented

Suggested focused tests:

- realtime packet version mismatch follows fallback path
- empty packet is rejected without mutating publish state
- old diagnostics still report bytes/frame counts correctly or are replaced

## Phase 7: Hardware-Backed Presenter

Acceptance criteria:

- presenter owns host renderer/window affinity on the render thread
- hardware path reports renderer name, upload count, render count, and fallback reason
- software fallback remains deterministic and observable
- lifecycle pause/resume/teardown do not race the render thread

Suggested focused tests:

- hardware unavailable falls back cleanly
- runtime present failure records fallback/fault diagnostics
- repeated pause/resume does not leak presenter state

## Phase 8: SIMD-Optimized Software Frame Path

Acceptance criteria:

- scalar and SIMD output match pixel-for-pixel for supported formats
- SIMD feature detection has a scalar fallback
- alignment and tail handling are covered
- speedup is measured separately from correctness

Suggested focused tests:

- exact-size, odd-width, and tail-pixel frame comparisons
- forced scalar and forced SIMD paths produce identical output

## Phase 9: Full Background Task Pool

Acceptance criteria:

- task pool is restricted to non-real-time work: decode, screenshots, save flushes, debugger snapshots, offline analysis
- task cancellation and shutdown are deterministic
- worker exceptions/failures are contained and surfaced through diagnostics
- no task directly mutates guest CPU/APU/PPU state
- work stealing does not starve lifecycle shutdown

Suggested focused tests:

- enqueue/drain ordering for dependent tasks
- cancellation before start and during shutdown
- task pool teardown with queued work

## Phase 10: Basic-Block Cache / Threaded Interpreter

High risk. Do not start until Phases 5-9 are stable.

Acceptance criteria:

- block cache invalidation is explicit for self-modifying or banked memory
- interrupt/IME/halt/timing behavior matches interpreter baseline
- trace tests prove register, memory, and cycle equivalence for representative blocks
- cache can be disabled at runtime for bisecting

Suggested focused tests:

- block boundary around branch, call, return, interrupt, and HALT
- memory bank switch invalidates affected blocks
- trace executor comparison against classic interpreter

## Phase 11: Guarded JIT / DBT

Very high risk. Defer until Phase 10 is stable and well measured.

Acceptance criteria:

- all generated code is guarded by memory map, bank, and CPU mode assumptions
- invalidation and bailout paths are tested before performance tuning
- JIT can be disabled at runtime and at build time
- deterministic trace comparison remains the primary correctness gate
- executable memory policy is documented for supported platforms

Suggested focused tests:

- guard failure returns to interpreter with exact state
- generated block invalidation on write/bank switch
- repeated enable/disable does not change execution results
