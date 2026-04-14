# Proto-Time Debugger and Instrumentation Plugin Subsystem Specification

This document defines the target architecture for the Proto-Time debugger and instrumentation plugin subsystem.

It applies the plugin subsystem blueprint to tracing, breakpoints, watchpoints, machine inspection, and developer-facing observability.

## Goal

The debugger and instrumentation subsystem should let tools observe machine execution, request safe control actions, and collect traces without letting tooling shortcuts erode machine determinism.

The subsystem must:

- separate trace collection from policy and UI tooling
- support live machine inspection with explicit safety limits
- make breakpoint, watchpoint, and pause semantics visible and deterministic
- expose degraded behavior when trace buffers overflow or tool backends fail
- support headless tracing and host-driven interactive debugging from the same subsystem shape

## Current Repository Anchor

The current codebase already exposes useful debugger-oriented signals even though it does not yet have a dedicated debugger plugin family.

Relevant existing anchors:

- `MachineEventType::StepCompleted`
- `MachineEventType::MemoryWriteObserved`
- `MachineEventType::BootRomVisibilityChanged`
- `MachineEventType::RomLoaded`
- `MachineView` register and memory accessors
- logging plugins that show event fan-out and failure disabling behavior

Those primitives are enough to define a debugger subsystem plan, but not enough to leave the debugger architecture implicit.

## Subsystem Responsibilities

### Engine

`DebugEngine` owns hot-path mechanics.

Responsibilities:

- fixed-capacity trace buffering for step summaries, memory events, and latched watch hits
- compact register or snapshot capture for debugger-visible state
- generation-aware handoff across reset, ROM load, and snapshot restore
- cheap counters for dropped trace records and deferred debugger actions

The engine should not:

- decide when breakpoints may be edited
- block on a socket, UI, or script runtime
- perform expensive symbolization or formatting on the machine thread
- directly pause the machine in arbitrary callback locations

### Service

`DebugService` owns lifecycle and policy.

Responsibilities:

- install and remove breakpoints and watchpoints
- gate configuration changes while the machine is active
- evaluate capability checks for debugger backends or sinks
- arbitrate pause, single-step, continue, and inspect actions at safe boundaries
- expose diagnostics, backend status, and trace retention state

The service is where the subsystem says:

- when breakpoint mutation is legal
- which debugger requests are serviced immediately versus deferred
- whether expression evaluation is allowed while running or paused only
- what happens when tracing capacity is exceeded

### Plugin Contract

The subsystem should define explicit debugger contracts instead of overloading generic logging plugins.

Recommended capabilities:

- `realtimeSafe`: safe to observe the live machine path
- `snapshotAware`: understands generation boundaries and snapshot restore
- `canRequestPause`: may request pause at safe boundaries
- `supportsBreakpoints`: can own breakpoint definitions
- `supportsWatchpoints`: can own memory or register watch rules
- `deterministic`: does not make machine-visible behavior depend on host timing
- `nonRealtimeOnly`: inspection or evaluation allowed only while paused
- `hostThreadAffinity`: requires a specific UI or transport thread

Recommended contract roles:

- `IDebuggerPlugin` for interactive control surfaces
- `ITraceSinkPlugin` for logging, trace capture, or profiling sinks
- `IWatchpointPlugin` for watch-rule providers

`DebugService` should be the only authority that admits these roles into the live observation path.

### Host and Backend Adapters

Adapters bridge debugger state to host tools.

Initial adapter targets:

- console or CLI debugger
- file trace sink
- simple remote debugging transport

Later adapter targets may include:

- IDE-integrated debugger frontends
- timeline or profiler viewers
- symbol-aware trace browsers

Adapters should consume prepared trace records and service-managed inspection APIs rather than reaching directly into arbitrary machine state from uncontrolled threads.

## Preferred Shape

```text
machine/plugins/debug/
    DebugEngine.hpp
    DebugService.hpp
    DebugPlugin.hpp
    DebugCapabilities.hpp
    TraceRecord.hpp
    BreakpointSet.hpp
    adapters/
        CliDebuggerAdapter.hpp
        CliDebuggerAdapter.cpp
        FileTraceSink.hpp
        FileTraceSink.cpp

tests/
    smoke_debug_engine.cpp
    smoke_debug_service.cpp
    smoke_debug_transport.cpp
    smoke_debug_fallback.cpp
```

## Event and Control Model

The debugger subsystem should distinguish between observation and control.

Observation path:

- step summaries
- memory-write observations
- boot ROM visibility changes
- ROM load and reset boundaries
- optional sampled register snapshots

Control path:

- pause requests
- resume requests
- single-step or bounded-step requests
- breakpoint and watchpoint mutation
- paused-only memory or register edits

Control requests must be latched and applied only at safe machine boundaries. Debugger adapters must not mutate machine state from arbitrary host callbacks.

## Fallback Behavior

Debugger failures must not destabilize the machine.

Required degraded behavior:

- if a trace buffer is full, drop according to a documented policy and increment an overflow counter
- if a debugger backend disconnects, preserve machine execution and latch the backend error
- if an expression-evaluation or paused-only action is requested while running, reject it and increment a rejected-action counter
- if a live sink is incompatible with realtime-safe tracing, bypass it from the live path and surface compatibility fallback

The service should expose at least:

- `traceOverflowCount`
- `rejectedActionCount`
- `compatibilityBypassCount`
- `lastBackendError`
- `activeBackendSummary`
- `lastObservedGeneration`
- `pendingControlRequestCount`

## Lifecycle Rules

The debugger subsystem must define explicit state for:

- `Detached`
- `Paused`
- `Running`
- `Faulted`

Rules:

- breakpoint and watchpoint mutation are always allowed while `Paused`
- live mutation while `Running` is allowed only for contracts that explicitly advertise safe deferred installation
- pause requests are observed at safe boundaries, not in the middle of arbitrary machine mutation
- snapshot restore and ROM load advance a debugger generation boundary and invalidate stale watch hits
- paused-only evaluation or edit actions are rejected while `Running`

## Thread Ownership

- machine thread: emits trace records and services control requests at safe boundaries
- debugger transport or UI thread: sends control requests and consumes prepared trace data
- tooling thread: may serialize traces, symbols, or reports offline

Blocking rules:

- the machine thread must not block on transports, file I/O, or UI state
- debugger backends may block on their own host APIs, but only on service-owned copies or prepared trace buffers
- expensive formatting, symbolization, and report generation must happen off the machine thread

## Required Core Extensions

This subsystem likely requires explicit API growth beyond current plugin categories.

Recommended additions:

- a dedicated debugger or instrumentation plugin category
- debugger-specific machine events or trace record types
- a service-managed pause and control boundary in the machine loop
- stable inspection snapshots for paused evaluation

Until those exist, logging plugins remain a stopgap rather than the architecture.

## Test Requirements

### Engine smoke test

Verify:

- step and memory events are buffered deterministically
- generation boundaries invalidate stale trace records correctly
- overflow counters increment under capacity pressure
- watch hits are captured without blocking the machine thread

### Service smoke test

Verify:

- paused-only edits are enforced
- breakpoint mutation rules are explicit and honored
- deferred control requests apply at safe boundaries
- incompatible sinks are rejected or bypassed visibly

### Transport smoke test

Verify:

- CLI, file, or remote adapters consume trace data correctly
- disconnects do not destabilize machine execution
- pause and single-step flows behave predictably

### Fallback smoke test

Verify:

- trace overflow is visible and deterministic
- backend disconnects latch errors while execution continues safely
- illegal running-time evaluation requests are rejected cleanly

## Acceptance Criteria

The debugger and instrumentation subsystem is ready when:

- trace collection is separated from debugger policy and UI transport
- all control actions are serviced at explicit safe boundaries
- live observation and paused-only mutation rules are explicit
- tooling failures degrade to visible lost-observability states rather than machine instability
- trace retention and backend health are diagnosable without a debugger-specific debugger
