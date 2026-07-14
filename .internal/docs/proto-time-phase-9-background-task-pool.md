# Phase 9: Full Background Task Pool

## Status

Complete.

## Delivered contract

- `BackgroundTaskService` is backed by a bounded work-stealing pool. Its
  production default reserves host capacity for emulation, SDL/UI, and audio,
  caps automatic worker creation at eight, and supports explicit worker and
  total-queue configuration through `[background]`, `--background-workers`,
  and `--background-queue-capacity`.
- Every production task is classified as generic, visual decode/reload/capture,
  save flush, debug snapshot, or video capture. Diagnostics report per-category
  submission, completion, rejection, cancellation, queue-wait, and execution
  counters alongside pool occupancy and stealing counters.
- Visual reload parses complete candidate manifests on a worker and publishes
  an immutable replacement only if its source generation is still current.
  Replacement PNG file I/O and decode are also staged; `resolve()` polls and
  never waits or synchronously decodes when the production decoder is wired.
- Visual-resource PNG capture is serialized on the background lane. Capture
  shutdown waits for accepted writes, applies their completion records, and
  writes the final manifest/report.
- Cartridge saves retain a rejected immutable snapshot for later retry. No
  steady-state queue rejection performs filesystem I/O inline, while the host
  shutdown fence still guarantees durability.
- Debug model construction can run from an immutable `VideoStateView`; optional
  work without a supported immutable adapter is dropped rather than rebuilt on
  the emulation lane. Prebuilt video/audio debug packets are likewise delivered
  through the categorized background lane without synchronous overload fallback.
- Video capture drops on background overload instead of invoking capture plugins
  on the emulation thread. Host shutdown fences accepted jobs before plugin and
  machine teardown.

## Overload and ownership rules

The emulation lane owns guest state and may only publish immutable packets or
snapshots. Background jobs never retain mutable machine state. Nonessential
visual/debug/capture work is rejected or retried when the bounded pool is full;
save work is coalesced and retained for explicit shutdown durability.

## Verification

The background pool smoke tests cover work stealing, global saturation,
categorized rejection, per-category latency counters, and drain behavior.
Visual reload, async image decode, capture fencing, debug delivery, and save
durability have focused smoke coverage. The full 108-test CTest suite passes.
The focused ThreadSanitizer targets build successfully; on the completion host,
`smoke-background-task-service` runs cleanly under TSAN while the other focused
binaries are stopped by the host runtime before `main` with ThreadSanitizer's
`unexpected memory mapping` fatal error rather than a reported data race.
