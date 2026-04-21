# Timing Sleep Quantum Patch Plan

This patch plan addresses emulation-rate wobble that presents most obviously as audio speeding up and slowing down.

## Problem Summary

The current timing loop regulates host sleeping at an instruction-scale threshold.

Today:
- `TimingEngine::canExecute()` uses `minInstructionCycles` to decide whether another step may run.
- `TimingEngine::nextWakeTime()` also uses `minInstructionCycles` to decide when the host should wake next.
- `emulator.cpp` sleeps until that computed wake time when no instruction executed.

This means the host may be asked to sleep for intervals on the order of microseconds. General-purpose `sleep_until()` is not precise enough for this. The result is:

- oversleep by the host scheduler
- budget replenished in lumps on the next update
- burst execution to catch up
- bursty PCM production
- audible speed fluctuation

The execution threshold and the host sleep threshold must be separated.

## Design Goal

Keep instruction eligibility fine-grained, but make host sleeping coarse-grained and scheduler-friendly.

In short:
- execution can remain cycle-based
- sleeping must use a larger pacing quantum

## Required Changes

### 1. Add a host sleep quantum to TimingConfig

Add a new field to `TimingConfig`:

```cpp
std::chrono::nanoseconds minSleepQuantum = std::chrono::milliseconds(1);
```

Recommended initial default:
- `1 ms`

Optional future tuning:
- `500 us` for platforms where this proves stable
- never lower by default unless profiling and listening tests prove it helps

This value is independent from `minInstructionCycles`.

## 2. Keep `minInstructionCycles` for execution only

Do not remove or repurpose `minInstructionCycles`.

It should continue to answer:
- do we have enough budget to run another instruction?
- how many cycles should be charged at minimum?

It should no longer directly determine whether the host should sleep.

## 3. Add a sleep-decision helper to TimingEngine

Add a helper that answers whether the deficit is large enough to justify sleeping.

Suggested interface:

```cpp
[[nodiscard]] bool shouldSleep(std::chrono::steady_clock::time_point now) const noexcept;
```

Suggested behavior:
- return `false` if not throttled
- return `false` if paused
- return `false` if effective clock is invalid
- return `false` if current budget already permits execution
- compute the duration until the next execution point
- return `true` only if that duration is at least `minSleepQuantum`

This keeps the sleep policy in the timing subsystem instead of spreading more timing math through `emulator.cpp`.

## 4. Update `TimingEngine::nextWakeTime()` to use the sleep quantum

The function should continue to compute when the engine would ideally wake next, but it should cooperate with the new coarse sleep policy.

Recommended behavior:
- if not throttled or paused, return `now`
- if enough budget exists to execute, return `now`
- compute `secondsUntilInstruction`
- convert that to a duration
- if the duration is smaller than `minSleepQuantum`, return `now`
- otherwise return `now + duration`

Conceptually:

```cpp
const auto wait = duration_cast<steady_clock::duration>(duration<double>(secondsUntilInstruction));
if (wait < config_.minSleepQuantum) {
    return now;
}
return now + wait;
```

This means small deficits will no longer trigger `sleep_until()`.

## 5. Update the idle branch in `emulator.cpp`

Current behavior:
- if no instruction executed, compute `nextStepTime`
- pick the minimum of `nextFrontendService` and `nextStepTime`
- sleep until that time if it is in the future

Required behavior:
- continue to compute the next frontend service time
- ask `TimingEngine` for the next step time
- only sleep if that next step time is truly in the future because of a scheduler-sized deficit
- otherwise continue the loop without sleeping

Recommended implementation shape:

```cpp
if (!executedInstruction) {
    const auto nextStepTime = timingEngine.nextWakeTime(idleNow);
    const auto nextWakeTime = (frontend != nullptr)
        ? std::min(nextFrontendService, nextStepTime)
        : nextStepTime;

    if (nextWakeTime > idleNow) {
        std::this_thread::sleep_until(nextWakeTime);
    } else if (frontend != nullptr && nextFrontendService > idleNow) {
        // no host sleep for tiny timing deficit; continue spinning cooperatively
    }
}
```

The important behavior change is that tiny timing deficits should now cause a fast loop retry rather than a host sleep.

## 6. Keep frontend servicing independent

Do not make frontend wake cadence depend on instruction threshold.

Frontend cadence should remain independent and periodic. The current `kFrontendServicePeriod` logic is acceptable as a first approximation.

The timing fix should not entangle video/input/audio frontend servicing with the instruction eligibility threshold.

## 7. Add diagnostics to prove the change worked

Extend `TimingStats` with host-sleep diagnostics:

```cpp
std::uint64_t sleepDecisions = 0;
std::uint64_t sleepSkippedForSmallDeficit = 0;
std::chrono::nanoseconds configuredMinSleepQuantum = std::chrono::milliseconds(1);
```

Recommended behavior:
- increment `sleepDecisions` when `nextWakeTime()` returns a future time suitable for sleep
- increment `sleepSkippedForSmallDeficit` when the engine detects a deficit smaller than `minSleepQuantum` and returns `now`

This makes the pacing trade visible.

## 8. Optional improvement: use yield for sub-quantum idle only if profiling says it helps

Do **not** start with `std::this_thread::yield()` unconditionally.

Begin with:
- no sleep for sub-quantum deficits
- let the loop retry naturally

If CPU burn becomes too high in throttled mode, then experiment with:
- yielding only when no frontend service is due
- yielding only after repeated sub-quantum idle loops

But that is a second-stage optimization, not the first fix.

## 9. Optional improvement: add a small execution batch threshold for frontend checks

If wobble remains after the sleep-quantum fix, the next thing to tune is per-instruction frontend cadence checks in the hot loop.

A follow-up patch may:
- check frontend time only every N instructions
- or every retired cycle chunk
- or only when `SteadyClock::now()` has crossed `nextFrontendService`

This is not the first patch. Fix sleep granularity first.

## Patch Checklist

### TimingConfig
- [ ] Add `minSleepQuantum`
- [ ] Set a sane default of `1 ms`

### TimingEngine
- [ ] Keep `minInstructionCycles` semantics unchanged for execution
- [ ] Update `nextWakeTime()` so deficits smaller than `minSleepQuantum` return `now`
- [ ] Optionally add `shouldSleep()` helper
- [ ] Add diagnostics for sleep decisions and skipped tiny deficits

### emulator.cpp
- [ ] Keep outer-loop timing update and inner-loop execution batching
- [ ] Continue to sleep only from the idle branch
- [ ] Let sub-quantum timing deficits retry without `sleep_until()`

### Tests
Add or update smoke coverage for:
- [ ] `nextWakeTime()` returns `now` for sub-quantum deficits
- [ ] `nextWakeTime()` returns future time for scheduler-sized deficits
- [ ] throttled mode preserves execution eligibility behavior
- [ ] unthrottled mode still returns immediate wake
- [ ] paused mode still returns immediate wake

## Expected Outcome

After this patch:
- emulation budget should advance more smoothly
- the host should stop oversleeping for instruction-scale deficits
- catch-up bursts should reduce
- audio should sound more stable because PCM production is less lumpy

This does not make the emulator audio-clock-driven. It simply makes wall-clock pacing respect host scheduler reality.
