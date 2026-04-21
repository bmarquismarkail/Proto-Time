# Proto-Time Timing Service Blueprint

This document defines the reference design for emulation speed regulation in Proto-Time.

Proto-Time already has a working wall-clock pacing loop in `emulator.cpp`. The next step is to promote that logic into a reusable subsystem so timing policy is explicit, testable, and extensible.

The goal is not merely to slow the emulator down. The goal is to make emulation time a first-class subsystem with clear ownership, visible diagnostics, and predictable behavior across normal-speed play, turbo mode, pause, single-step, and headless benchmarking.

## Goals

A Timing Service should:

- regulate emulation speed against a host clock when throttling is enabled
- support unthrottled execution for benchmarking and debugging
- support speed multipliers such as 0.5x, 1x, 2x, and 4x
- support pause and single-step semantics without polluting the main loop
- keep timing policy separate from machine stepping mechanics
- preserve deterministic execution when operating in non-throttled modes
- make drift, catch-up, and sleep behavior visible through diagnostics

## Reference Layering

The Timing Service should follow the same subsystem pattern used elsewhere in Proto-Time.

### 1. Timing Engine

The engine owns timing mechanics but not host sleeping or CLI policy.

Responsibilities:
- track target emulated clock rate
- track effective speed multiplier
- accumulate host elapsed time into emulated cycle budget
- clamp catch-up windows
- track cycle debt or surplus
- calculate when the next wake should occur

The timing engine should not:
- call `sleep_until()` directly
- know about SDL or frontend event pumping
- own pause or turbo UI behavior
- perform machine steps directly

### 2. Timing Service

The service owns lifecycle and policy.

Responsibilities:
- expose runtime modes such as throttled, unthrottled, turbo, paused, and stepping
- update timing state from host timestamps
- answer whether execution may proceed
- charge retired cycles after each machine step
- provide next-wake recommendations to the host loop
- expose diagnostics and current timing state

### 3. Policy Contract

Timing policy should be explicit and swappable in concept, even if Proto-Time initially ships only a single default policy.

Possible policies:
- wall-clock paced
- unthrottled
- turbo wall-clock paced
- deterministic offline stepping
- audio-led pacing in the future
- video-led pacing in the future

The initial implementation does not need all of these, but the service should be shaped so they can be added without rewriting the main loop.

### 4. Host Adapter

The host loop or frontend adapter remains responsible for:
- reading `steady_clock::now()`
- sleeping until the recommended wake time
- pumping frontend events
- integrating user-facing controls that change timing mode

This keeps platform sleep behavior outside the timing engine.

## Initial Scope

The first Timing Service implementation should support the following modes:

### Normal throttled mode

Emulation runs at the machine's target clock rate against wall-clock time.

### Unthrottled mode

Emulation runs as fast as possible without host-clock pacing.

### Turbo mode

Emulation runs against wall-clock time with a speed multiplier greater than 1.0.

### Paused mode

Execution stops advancing until resume or single-step is requested.

### Single-step mode

A single instruction or cycle-bounded unit of execution is allowed to run while otherwise paused.

## Recommended API Shape

A first-pass interface can look like this:

```cpp
struct TimingConfig {
    double baseClockHz = 0.0;
    double speedMultiplier = 1.0;
    double minInstructionCycles = 4.0;
    std::chrono::nanoseconds maxCatchUp = std::chrono::milliseconds(8);
    bool throttled = true;
};

struct TimingStats {
    double cycleBudget = 0.0;
    double cycleDebt = 0.0;
    double effectiveClockHz = 0.0;
    std::uint64_t wakeSleeps = 0;
    std::uint64_t catchUpClampCount = 0;
    std::uint64_t idleLoops = 0;
    std::uint64_t singleStepsGranted = 0;
    bool paused = false;
    bool throttled = true;
    double speedMultiplier = 1.0;
};

class TimingService {
public:
    void configure(const TimingConfig& config);

    void setThrottled(bool throttled) noexcept;
    void setPaused(bool paused) noexcept;
    void setSpeedMultiplier(double multiplier) noexcept;
    void requestSingleStep() noexcept;

    void start(std::chrono::steady_clock::time_point now) noexcept;
    void update(std::chrono::steady_clock::time_point now) noexcept;

    [[nodiscard]] bool canExecute() const noexcept;
    void charge(double retiredCycles) noexcept;

    [[nodiscard]] std::chrono::steady_clock::time_point nextWakeTime(
        std::chrono::steady_clock::time_point now) const noexcept;

    [[nodiscard]] const TimingConfig& config() const noexcept;
    [[nodiscard]] TimingStats stats() const noexcept;
};
```

The concrete names can change, but the behavioral split should stay the same.

## Timing Model

The initial pacing model should continue to use:

- host elapsed wall time
- machine target clock rate
- retired cycles reported by the machine after each instruction

Recommended calculation:

```cpp
effectiveClockHz = baseClockHz * speedMultiplier;
cycleBudget += elapsedSeconds * effectiveClockHz;
cycleBudget = std::min(cycleBudget, maxCycleBudget);
```

After a step completes:

```cpp
chargedCycles = std::max(minInstructionCycles, retiredCycles);
cycleBudget = std::max(0.0, cycleBudget - chargedCycles);
```

This preserves the current execution model while moving it into a dedicated subsystem.

## Why Retired Cycles Should Stay in the Loop

Timing should continue to charge execution based on retired cycles rather than raw instruction count.

Reasons:
- different instructions consume different machine time
- host-side speed regulation should follow emulated hardware timing, not opcode count
- this keeps the timing service aligned with the machine's own execution feedback

If retired cycle accounting is wrong, the timing service will pace the wrong reality. Because of that, the timing subsystem should expose enough diagnostics to make bad cycle accounting visible.

## Main Loop Integration Pattern

The main loop should become simpler after extraction.

Instead of embedding timing math directly in `main()`, it should:

1. update timing service with the current host time
2. step the machine while timing service allows execution
3. charge retired cycles back into timing service after each step
4. service frontend work
5. sleep until the earliest relevant wake point

Conceptually:

```cpp
while (!stopRequested) {
    timing.update(now);

    while (timing.canExecute() && !stopRequested) {
        machine.step();
        timing.charge(machine.runtimeContext().getLastFeedback().retiredCycles);
    }

    frontend.service();
    sleep_until(min(timing.nextWakeTime(now), frontend.nextWakeTime(now)));
}
```

The host loop should remain the orchestrator. The Timing Service should be the clock brain.

## Preferred Behavior by Mode

### Throttled mode

- accumulate wall-clock time into cycle budget
- clamp catch-up to a bounded window
- sleep when no instruction can yet be charged
- keep frontend service cadence independent from execution budget

### Unthrottled mode

- allow execution without host-clock sleep pacing
- continue collecting stats
- optionally preserve frontend servicing cadence so the window remains responsive

### Turbo mode

- use wall-clock pacing with a multiplier greater than 1.0
- keep catch-up clamping active
- expose the active multiplier in diagnostics

### Paused mode

- stop granting execution budget
- continue servicing frontend and user controls
- allow explicit reset/configure actions if other subsystem lifecycle rules permit them

### Single-step mode

- while paused, allow one execution unit through
- clear the single-step request after execution is granted
- record a diagnostic count for granted single steps

## Frontend controls (SDL)

The SDL frontend exposes simple keyboard controls for timing during interactive runs:
- `p`: toggle pause/resume
- `o`: toggle throttled (wall-clock) vs unthrottled execution
- `n`: request a single-step while paused
- `]`: multiply speed by 2
- `[`: divide speed by 2 (clamped at 1/8x)

These keys are implemented in the SDL frontend plugin and operate on the `TimingService` attached to the `Machine`.

## CLI flags

The emulator executable supports a few command-line options for initial timing configuration (useful for headless runs and benchmarks):
- `--unthrottled` : start the emulator in unthrottled mode (no wall-clock pacing)
- `--speed <mult>` : start with an initial speed multiplier (e.g. `--speed 2.0`)
- `--pause`       : start the emulator in paused state (use `n` to single-step)


## Catch-Up and Sleep Policy

The Timing Service should always bound host drift correction.

Recommended rules:
- define a maximum catch-up window, such as 8 ms
- clamp accumulated cycle budget to avoid runaway burst execution after host stalls
- expose how often the clamp occurred through diagnostics

The host loop should sleep only when:
- throttling is enabled
- execution cannot proceed yet
- the next wake point is in the future

Timing should never depend on exact host sleep precision. It should tolerate oversleep and recover gracefully within the catch-up window.

## Diagnostics

Timing diagnostics are not optional. They are how timing stops being mysterious.

The Timing Service should expose at least:

- target base clock rate
- effective clock rate after multiplier
- current cycle budget
- current cycle debt if tracked separately
- current speed multiplier
- paused/throttled state
- number of sleep decisions
- number of catch-up clamps
- number of idle loops
- number of granted single steps

Optional but useful:
- total emulated cycles
- total host runtime
- observed emulation ratio
- oversleep estimate
- last wake duration estimate

## Failure and Degraded Behavior

Timing should degrade deterministically.

Examples:
- if throttling is disabled, continue running unthrottled with clear diagnostic state
- if speed multiplier is invalid, clamp to a sane minimum and maximum
- if host time jumps unexpectedly, clamp catch-up rather than bursting indefinitely
- if the frontend is unavailable, keep timing service behavior unchanged in headless mode

The Timing Service should never silently alter speed policy without leaving a visible trace in its stats or status.

## Testing Requirements

The Timing Service should ship with smoke coverage similar to other subsystems.

### Engine smoke test

Verifies:
- cycle budget accumulation
- retired-cycle charging
- catch-up clamping
- multiplier effect
- paused mode blocking execution
- single-step allowance while paused

### Service smoke test

Verifies:
- runtime mode transitions
- invalid multiplier clamping or rejection
- next-wake calculations
- throttled vs unthrottled behavior
- stats updates

### Main-loop integration smoke test

Verifies:
- timing service cooperates with frontend service cadence
- sleep decisions happen only when expected
- headless mode remains stable
- execution resumes correctly after pause or turbo changes

## Blueprint Checklist

Before merging the Timing Service, confirm:

- [ ] timing logic is no longer embedded directly in `main()` beyond orchestration
- [ ] wall-clock pacing is separated from host sleeping
- [ ] speed multiplier is explicit and configurable
- [ ] paused and single-step modes are supported
- [ ] throttled and unthrottled modes are both supported
- [ ] catch-up clamping is bounded and observable
- [ ] timing stats are exposed for diagnostics
- [ ] smoke tests cover pacing, pause, turbo, and wake behavior

## Recommended Rollout Plan

### Phase 1

Extract the current wall-clock pacing logic into `TimingService` with no behavior change.

### Phase 2

Add explicit speed multiplier support.

### Phase 3

Add paused and single-step semantics.

### Phase 4

Add unthrottled and turbo mode controls to the emulator executable and frontend control path.

### Phase 5

Consider advanced pacing policies such as audio-led pacing only if wall-clock pacing proves insufficient.

## Final Principle

Emulation speed should not be an accidental property of how fast the host happens to be today.

Proto-Time should treat time the way it now treats audio plugin safety: as a designed subsystem with clear boundaries, explicit policy, and visible failure modes.
