# Proto-Time Plugin Subsystem Blueprint

This document captures the reference architecture for plugin subsystem development in Proto-Time.

The audio subsystem is the first subsystem to fully exercise this pattern. Future plugin families should use the same shape unless there is a strong, explicit reason not to.

## Goals

A Proto-Time plugin subsystem should:

- preserve machine determinism where required
- make live-path safety rules explicit
- separate hot-path mechanics from lifecycle policy
- support degraded operation with visible diagnostics
- be testable in isolation and at the host boundary
- make threading and mutation ownership obvious

This is not just a style preference. It is the difference between a plugin architecture and a collection of host-specific shortcuts.

## Reference Layering

Each plugin subsystem should be split into four responsibilities.

### 1. Engine

The engine owns the hot-path mechanics.

Examples:
- ring buffers
- resampling
- frame queues
- event staging
- snapshot application
- low-level data transport

The engine should be small, mechanical, and policy-light.

The engine should not:
- own host API setup
- decide when mutation is allowed
- perform non-essential allocation on a live callback path
- silently hide safety violations

### 2. Service

The service owns lifecycle, policy, validation, and mutation gates.

Examples:
- attach/detach coordination
- configure/reset eligibility
- live-path compatibility checks
- fallback accounting
- diagnostic surfacing
- safe admission/removal of processors or handlers

The service is where the subsystem says:
- what is legal while the backend is active
- what must only happen while paused/closed
- what degraded behavior looks like

### 3. Plugin Contract

The plugin contract defines what third-party or swappable components must provide.

Contracts must make capabilities explicit instead of implied.

Examples:
- realtimeSafe
- fixedCapacityOutput
- frameSizePreserving
- pollingSafe
- snapshotAware
- deterministic
- nonRealtimeOnly

If a capability matters to correctness, it should be in the interface.

### 4. Host or Backend Adapter

The adapter bridges the subsystem to SDL, file output, sockets, tooling, scripting runtimes, or other host environments.

Adapters should:
- translate host behavior into subsystem behavior
- keep host-specific details out of the engine
- keep backend setup and teardown out of plugin processors
- expose errors and device information in a stable form

## Standard Design Rules

### Rule 1: Separate live-path APIs from offline APIs

Every subsystem should distinguish between:

- live-safe operations used during callbacks, frame presents, or machine-run loops
- non-real-time operations used in tests, tooling, offline transforms, or editors

Do not share a single convenient API across both worlds unless it is truly safe for both.

If an API resizes vectors, allocates memory, blocks, or performs non-trivial host I/O, it is not a live-path API.

### Rule 2: Capabilities are mandatory for admission

A subsystem service should validate plugin capabilities before admitting a plugin into the live path.

Examples:
- reject processors that are not realtimeSafe
- reject plugins that require variable-size output if the subsystem requires fixed-capacity operation
- reject mutation while the backend is active unless the contract explicitly allows it

Do not rely on comments or convention for safety-critical assumptions.

### Rule 3: Lifecycle state must be explicit

Each subsystem must define:

- when configuration is allowed
- when reset is allowed
- when mutation is allowed
- when live callbacks or host loops may observe state changes
- how attach/detach transitions are handled

Recommended pattern:
- engine handles mechanics
- service enforces lifecycle gates
- backend or adapter reports active/paused/closed state

### Rule 4: Failure must degrade predictably

Every subsystem must define degraded behavior for live-path failures.

Examples:
- audio: preserve dry output or emit silence, and count the fallback
- video: present the last valid frame or a blank frame, and count dropped presents
- input: preserve neutral or prior state, and count polling failures
- scripting: reject the call, preserve machine stability, and latch an error

A subsystem must never fail invisibly.

### Rule 5: Diagnostics must be cheap and visible

Each subsystem should expose enough diagnostics to explain degraded behavior without requiring a debugger.

Recommended diagnostics include:
- skip counts
- underrun/overrun counts
- fallback counts
- last backend error
- active backend summary
- high-water marks
- whether resampling, snapshot fallback, or compatibility fallback is active

Diagnostics should be cheap to query and safe to read from non-hot paths.

### Rule 6: Thread ownership must be written down

Every subsystem design should document:

- which thread or path produces data
- which thread or path consumes data
- which state may be shared
- which thread may block
- which thread must not block
- how reset or generation boundaries are handed off

This must be written both in the code comments and in the subsystem design note.

If ownership is fuzzy, bugs will eventually become folklore.

## Preferred Subsystem Shape

A new plugin subsystem should usually follow a structure similar to:

```text
machine/plugins/<subsystem>/
    <Subsystem>Engine.hpp
    <Subsystem>Service.hpp
    <Subsystem>Pipeline.hpp        # optional, if chaining makes sense
    <Subsystem>Plugin.hpp
    <Subsystem>Capabilities.hpp    # optional
    adapters/
        <BackendA>.hpp
        <BackendA>.cpp
        <BackendB>.hpp
        <BackendB>.cpp

tests/
    smoke_<subsystem>_engine.cpp
    smoke_<subsystem>_service.cpp
    smoke_<subsystem>_transport.cpp
    smoke_<subsystem>_fallback.cpp
```

Not every subsystem needs every file, but the architectural intent should remain recognizable.

## Mutation and Callback Safety Pattern

For live subsystems, use this pattern by default:

- preallocate buffers during non-real-time configuration
- keep live processing fixed-capacity where practical
- reject live-path plugins that cannot honor subsystem constraints
- gate reset/configuration behind an explicit paused/closed condition
- make callback-time fallback behavior deterministic
- surface callback-time fallback via counters or latched diagnostics

## Reset and Generation Boundary Pattern

For subsystems that cross machine generations, rollback boundaries, or snapshot resets:

- do not let producer-side code destructively reset callback-consumed state during active callbacks
- move reset observation to the consumer boundary where practical
- preserve new-generation data explicitly across reset boundaries
- keep fallback behavior deterministic when handoff cannot complete immediately

If a subsystem cannot guarantee immediate handoff, it should prefer:
- visible degraded behavior
- preserved correctness
- retry on the next safe boundary

over hidden or unsafe mutation.

## Processor and Extension Contract Guidance

A plugin contract should answer these questions up front:

- Is this plugin allowed on the live path?
- Does it require fixed-capacity output?
- Can it mutate machine state?
- Is it deterministic?
- Does it require host thread affinity?
- Can it run headless?
- Does it preserve input/output shape?
- What is its fallback behavior on failure?

If those answers are not clear, the contract is incomplete.

## Test Requirements

A plugin subsystem is not complete because it compiles. It is complete when it has smoke coverage for the behaviors that matter.

Each subsystem should have, at minimum:

### Engine smoke test

Verifies core mechanics independent of the host adapter.

Examples:
- queueing
- reset behavior
- generation boundaries
- data preservation across handoff
- no-crash fallback on empty input

### Service smoke test

Verifies lifecycle gates and admission rules.

Examples:
- allowed vs rejected plugin types
- configure/reset while paused
- rejection while active
- fallback counters incrementing

### Transport or adapter smoke test

Verifies host-facing integration.

Examples:
- backend opens and closes cleanly
- device or host configuration gets propagated
- headless fallback remains stable
- host translation into subsystem events works

### Fallback smoke test

Verifies degraded behavior is visible and deterministic.

Examples:
- scratch-capacity mismatch
- backend unavailable
- dropped present
- failed host poll
- rejected script call

## Blueprint Checklist for New Plugin Work

Before merging a new plugin subsystem, confirm the following:

- [ ] Engine responsibilities are clearly separated from service responsibilities
- [ ] Live-path and offline APIs are distinct
- [ ] Plugin capabilities are explicit in the contract
- [ ] Lifecycle mutation rules are enforced, not implied
- [ ] Degraded behavior is deterministic
- [ ] Diagnostics are exposed for live-path fallbacks
- [ ] Thread ownership is documented
- [ ] Hot-path allocation and blocking rules are documented
- [ ] Smoke tests exist for engine, service, transport, and fallback behavior
- [ ] The subsystem works in both backend-available and degraded/headless modes when applicable

## Applying the Blueprint

Recommended order for future subsystem adoption:

1. Input subsystem
2. Video subsystem
3. Debugger and instrumentation subsystem
4. Scripting subsystem

Input is a good next target because it is simpler than audio while still exercising host translation, lifecycle gates, and deterministic fallback behavior.

## Final Principle

A Proto-Time plugin subsystem should be understandable under stress.

When the machine is live, the backend is imperfect, or the host environment is hostile, the subsystem should still answer:

- what path is active
- what degraded behavior is happening
- why that behavior is legal
- what state transitions are allowed next

If those answers are visible, the subsystem is ready to be a framework pattern instead of a one-off success.
