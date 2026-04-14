# Proto-Time Scripting Plugin Subsystem Specification

This document defines the target architecture for the Proto-Time scripting plugin subsystem.

It applies the plugin subsystem blueprint to embedded or attached scripting runtimes used for automation, tooling, debugging, and controlled machine interaction.

## Goal

The scripting subsystem should allow controlled automation and extension without allowing a general-purpose runtime to compromise determinism, thread safety, or hot-path stability.

The subsystem must:

- separate script execution mechanics from lifecycle and policy
- default to paused or offline execution unless a contract explicitly proves live-path safety
- keep interpreter setup and host binding logic out of the machine hot path
- make script failure visible and non-destructive
- allow headless automation and backend-integrated scripting through the same service boundary

## Scope

This subsystem covers:

- automation hooks
- paused inspection and mutation scripts
- tooling-time exporters and analyzers
- debugger-oriented scripting helpers
- optional event-driven callbacks where explicitly safe

This subsystem does not assume that arbitrary scripts are safe to run during machine execution. Live scripting is an opt-in capability, not the default.

## Current Repository Anchor

The current repository does not yet expose a dedicated scripting plugin family. That makes this a forward-looking specification.

Relevant existing anchors:

- `MachineView` already provides a read-oriented inspection surface
- machine events already expose step, ROM, boot, and I/O activity boundaries
- the plugin manager already models attach, detach, disable-on-failure, and category dispatch

The scripting subsystem should reuse those proven ideas while adding explicit lifecycle gating and capability checks.

## Subsystem Responsibilities

### Engine

`ScriptEngine` owns the mechanical execution layer.

Responsibilities:

- manage prevalidated script invocation records
- cache bindings, handles, or compiled script units
- enforce fixed budgets for any live-safe callback path
- stage results and latched errors for service-level handling
- provide generation-aware invalidation across reset and snapshot restore

The engine should not:

- load files opportunistically on the hot path
- decide whether a script may mutate machine state
- own interpreter process or host environment lifecycle policy
- silently swallow script failures

### Service

`ScriptService` owns lifecycle and policy.

Responsibilities:

- load and unload scripts or runtimes
- validate capabilities before script admission
- gate invocation modes based on machine lifecycle state
- manage sandbox, permissions, and mutation authority
- expose diagnostics, active-runtime summaries, and latched errors

The service defines:

- whether scripts may run while the machine is active
- whether scripts may mutate machine state
- whether a runtime may subscribe to machine events
- what timeout or budget rules apply
- what degraded behavior occurs on interpreter or binding failure

### Plugin Contract

The scripting subsystem must use explicit capabilities.

Recommended capabilities:

- `deterministic`: produces machine-visible results independent of host timing
- `nonRealtimeOnly`: may execute only while paused or offline
- `liveCallbackSafe`: safe for bounded live callbacks
- `snapshotAware`: handles reset and snapshot generation boundaries
- `machineMutationAllowed`: may request machine writes through the service
- `hostThreadAffinity`: requires execution on a specific host thread
- `headlessSafe`: can operate without UI or backend presence
- `boundedExecution`: advertises an execution budget the service can enforce

Recommended contract roles:

- `IScriptingPlugin` for runtime adapters
- `IScriptHookPlugin` for event-triggered hooks
- `IScriptAutomationPlugin` for offline automation tasks

No scripting adapter should enter a live callback path unless `ScriptService` validates the required capabilities explicitly.

### Host and Backend Adapters

Adapters bridge to specific runtimes.

Initial adapter targets:

- a minimal embedded scripting runtime for offline automation
- file-based automation entry points for tests or tooling

Later adapter targets may include:

- Lua, Python, or JavaScript runtime adapters
- debugger-integrated script consoles
- editor or IDE automation bridges

Adapters should expose stable subsystem contracts, not raw interpreter internals.

## Preferred Shape

```text
machine/plugins/scripting/
    ScriptEngine.hpp
    ScriptService.hpp
    ScriptPlugin.hpp
    ScriptCapabilities.hpp
    ScriptInvocation.hpp
    adapters/
        EmbeddedScriptRuntime.hpp
        EmbeddedScriptRuntime.cpp
        FileAutomationRuntime.hpp
        FileAutomationRuntime.cpp

tests/
    smoke_scripting_engine.cpp
    smoke_scripting_service.cpp
    smoke_scripting_transport.cpp
    smoke_scripting_fallback.cpp
```

## Invocation Modes

The scripting subsystem should separate three invocation modes.

### 1. Offline automation

Examples:

- export a report
- inspect a ROM header
- run a scripted validation pass

This mode may allocate, block, and use richer host integrations.

### 2. Paused inspection and mutation

Examples:

- read registers and memory
- patch state intentionally while paused
- run debugger helper scripts

This mode is powerful, but must remain paused-only unless the service explicitly provides a safe deferred command mechanism.

### 3. Live callbacks

Examples:

- lightweight event hooks
- bounded telemetry counters
- prevalidated trigger handlers

This mode is exceptional. It must be fixed-budget, capability-gated, and mechanically limited.

## Fallback Behavior

Scripting failures must preserve machine stability.

Required degraded behavior:

- if a script invocation fails, reject the call, preserve machine state, and latch the error
- if a live callback exceeds its budget or violates a capability rule, bypass it and increment a rejection counter
- if a runtime becomes unavailable, disable that runtime while keeping the machine stable
- if a script requests unauthorized mutation, reject the request and report it explicitly

The service should expose at least:

- `invocationFailureCount`
- `rejectedLiveCallbackCount`
- `permissionDeniedCount`
- `disabledRuntimeCount`
- `lastRuntimeError`
- `activeRuntimeSummary`
- `lastObservedGeneration`

## Lifecycle Rules

The scripting subsystem must define explicit state for:

- `Detached`
- `Paused`
- `Active`
- `Faulted`

Rules:

- runtime load, unload, and binding changes are always allowed while `Detached`
- paused inspection and mutation are allowed while `Paused`
- live callbacks are rejected while `Active` unless the runtime advertises `liveCallbackSafe` and `boundedExecution`
- snapshot restore and ROM load invalidate stale script handles and cached machine references
- scripts must never hold raw pointers into machine state across generation boundaries

## Thread Ownership

- machine thread: may service only prevalidated, bounded live callbacks and deferred script requests approved by the service
- runtime thread or host thread: executes heavier interpreter work, file I/O, and host integration
- tooling thread: inspects diagnostics, logs, or offline results

Blocking rules:

- the machine thread must not block on interpreter execution, file I/O, or host runtime setup
- runtime adapters may block on their own host integrations, but only outside the live machine path
- permission checks and mutation gating must happen in `ScriptService`, not ad hoc inside runtime bindings

## Required Core Extensions

This subsystem likely requires explicit API growth.

Recommended additions:

- a dedicated scripting plugin category or service-owned dispatch path
- safe deferred machine command submission for paused-only or boundary-safe mutations
- stable inspection snapshots for scripts that outlive one callback
- generation tokens for invalidating stale handles

Without those pieces, scripting becomes an unsafe convenience layer rather than a subsystem.

## Test Requirements

### Engine smoke test

Verify:

- invocation records execute through the engine mechanically
- generation invalidation drops stale cached handles
- bounded live callbacks enforce budget and produce visible rejection counts

### Service smoke test

Verify:

- paused-only mutation is enforced
- unauthorized mutation requests are rejected visibly
- incompatible runtimes are denied live admission
- runtime faults disable the runtime without destabilizing the machine

### Transport smoke test

Verify:

- embedded or file-backed runtimes load and unload cleanly
- offline automation paths work in headless mode
- debugger-integrated scripting can consume service-managed inspection APIs

### Fallback smoke test

Verify:

- invocation failure preserves machine state and latches an error
- over-budget live callbacks are bypassed deterministically
- runtime loss degrades to disabled-runtime state rather than hidden failure

## Acceptance Criteria

The scripting subsystem is ready when:

- script execution mechanics are separated from lifecycle and permission policy
- live scripting is explicit, bounded, and capability-gated
- paused-only mutation rules are enforced by `ScriptService`
- runtime failures degrade to visible rejected or disabled states
- scripting can be used for headless automation without coupling it to a frontend backend
