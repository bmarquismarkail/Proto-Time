# Timing Jitter Fix Brief

## Objective

Reduce or eliminate emulation-rate jitter that presents most obviously as audio slowing down and speeding up.

## Established Findings

1. The earlier timing hot-path mutex issue was addressed by introducing `TimingEngine` and moving hot-path execution checks there.
2. The sleep-quantum patch was a valid improvement. Tiny instruction-scale deficits should not call `sleep_until()`.
3. The remaining wobble is primarily caused by execution granularity.
4. The inner execution loop still invokes frontend servicing logic too frequently.
5. That frequent host-time/frontend checking breaks execution into irregular tiny chunks, which makes audio production lumpy.
6. The execution loop lacks a clear maximum execution slice, so budget can still be consumed in bursty chunks between frontend interruptions.

## Implementation Direction

Keep the current `TimingEngine` / `TimingService` split.

Focus on these changes:

1. Coarsen frontend servicing cadence inside the hot loop.
2. Bound execution into explicit slices.

## Required Changes

### 1. Remove per-instruction frontend servicing from the hot loop

Do not call frontend servicing logic after every instruction.

Instead, service frontend only after a bounded execution slice has completed, or when a coarser service threshold is reached.

Accepted approaches:
- service frontend every N instructions
- service frontend every retired-cycle chunk
- service frontend after a bounded execution slice ends

Preferred approach:
- use retired-cycle chunking or bounded execution slices, not raw instruction count

### 2. Add a bounded execution slice

Add an explicit execution-slice limit to the outer loop.

Recommended initial value:
- approximately 1 ms worth of emulated cycles

Suggested model:

```cpp
const double kExecutionSliceSeconds = 0.001;
const double kMaxExecutionSliceCycles = static_cast<double>(cpuClockHz) * kExecutionSliceSeconds;
```

Within the inner loop:
- accumulate charged or retired cycles into `sliceCycles`
- break the loop once `sliceCycles >= kMaxExecutionSliceCycles`

This slice bound is separate from:
- `minInstructionCycles`
- `maxCatchUp`
- `minSleepQuantum`

### 3. Prefer cycle-based frontend servicing threshold

If frontend service remains inside the execution loop, it should be coarsened.

Preferred pattern:
- accumulate retired or charged cycles into `frontendSliceCycles`
- only call frontend servicing once a cycle threshold is reached

Recommended initial threshold:
- about 1 ms worth of emulated cycles

Example target shape:

```cpp
double sliceCycles = 0.0;
double frontendSliceCycles = 0.0;

while (timingEngine.canExecute() && gStopRequested == 0) {
    machine.step();
    ++steps;

    const double retiredCycles = static_cast<double>(machine.runtimeContext().getLastFeedback().retiredCycles);
    const double chargedCycles = std::max(kMinInstructionCycles, retiredCycles);

    timingEngine.charge(retiredCycles);
    sliceCycles += chargedCycles;
    frontendSliceCycles += chargedCycles;

    if (frontendSliceCycles >= kFrontendServiceCycleSlice) {
        frontendSliceCycles = 0.0;
        if (serviceFrontendUntil(SteadyClock::now())) {
            gStopRequested = 1;
            break;
        }
    }

    if (sliceCycles >= kMaxExecutionSliceCycles) {
        break;
    }
}
```

### 4. Keep the sleep quantum behavior

Do not remove the `minSleepQuantum` fix.

The host should still avoid `sleep_until()` for tiny deficits.

### 5. Minimize host clock reads inside the hot loop

Reduce `SteadyClock::now()` calls inside the inner execution loop as much as practical.

Acceptable places:
- outer-loop boundaries
- when a frontend-service slice threshold is reached
- when computing idle sleep behavior

Avoid reading the clock after every instruction.

### 6. Preserve responsiveness

The patch must not make the frontend unresponsive.

Use bounded slices rather than large uncontrolled bursts.

## Diagnostics

Add lightweight diagnostics such as:
- execution slices entered
- average or last slice cycles retired
- frontend service checks triggered by slice threshold

## Validation

Check these behaviors after the patch:

1. Audio sounds steadier in normal throttled mode.
2. Emulation does not visibly stutter more than before.
3. Frontend/input responsiveness remains acceptable.
4. `minSleepQuantum` behavior still works as intended.
5. No reintroduction of per-instruction mutexed timing calls in the hot path.

## Constraints

- Do not remove the `TimingEngine` / `TimingService` separation.
- Do not reintroduce per-instruction mutexed service calls in the hot loop.
- Do not regress pause, throttle, speed multiplier, or single-step behavior.
- Do not switch to audio-led master clocking in this patch.

## Expected Deliverable

Implement the code changes in the current branch and summarize:
- what changed
- which files changed
- what thresholds were chosen
- whether tests were added or updated
- any remaining caveats
