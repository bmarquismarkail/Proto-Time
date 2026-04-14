# Proto-Time Input Plugin Subsystem Specification

This document defines the target architecture for the Proto-Time input plugin subsystem.

It applies the plugin subsystem blueprint to non-audio input handling and should be read together with the existing machine input abstraction design note.

## Goal

The input subsystem should make host input collection, machine-owned logical input state, and core-specific input translation separate concerns.

The subsystem must:

- keep machine-visible input deterministic at machine step boundaries
- support both digital and analog input without forcing them through one convenience API
- let host adapters translate keyboard, controller, replay, or tooling input into machine-owned logical inputs
- degrade predictably when polling or event ingestion fails
- keep input ownership out of frontend-specific plugins

## Current Repository Anchor

The current codebase already exposes:

- `PluginCategory::DigitalInput`
- `PluginCategory::AnalogInput`
- `IDigitalInputPlugin::sampleDigitalInput(...)`
- `IAnalogInputPlugin::sampleAnalogInput(...)`
- machine-owned `InputButton` usage in the SDL frontend contract

`GameBoyMachine::pollInputPlugins()` currently samples digital input once per machine step and applies the sampled mask through `setJoypadState(...)`. That is the correct deterministic boundary to preserve.

The SDL frontend should stop being the long-term owner of button-state policy. It may remain an adapter, but the subsystem service should own admission, mutation rules, diagnostics, and fallback behavior.

## Subsystem Responsibilities

### Engine

`InputEngine` owns hot-path mechanics only.

Responsibilities:

- fixed-capacity staging of digital button masks
- fixed-capacity staging of analog channel samples
- event-to-snapshot handoff between host adapters and machine polling
- generation-aware reset of queued input samples
- cheap counters for dropped events, stale samples, and fallback use

The engine should not:

- know about SDL, evdev, replay files, or netplay protocols
- decide whether runtime remapping is legal
- perform heap allocation on the machine polling path
- decide whether previous-state or neutral fallback is required

### Service

`InputService` owns lifecycle and policy.

Responsibilities:

- attach and detach input adapters
- validate plugin capabilities before live admission
- own mapping profiles from host controls to machine-owned logical inputs
- gate configuration and remapping while adapters are active
- define reset behavior across ROM load, machine reset, and snapshot restore
- expose diagnostics and active-adapter summaries

The service should be the only layer that answers:

- when remapping is allowed
- when adapters may be replaced
- whether analog input is enabled for a machine
- what fallback state is applied on polling failure

### Plugin Contract

The subsystem should introduce an explicit input contract rather than relying only on `sampleDigitalInput(...)` and `sampleAnalogInput(...)`.

Recommended capabilities:

- `pollingSafe`: safe to query from the machine loop without blocking
- `eventPumpSafe`: safe to ingest host events while active
- `deterministic`: does not depend on wall-clock timing for visible machine state
- `supportsDigital`: can provide digital button state
- `supportsAnalog`: can provide analog channels
- `fixedLogicalLayout`: uses stable machine-owned logical controls
- `hotSwapSafe`: safe to replace or rebind input devices at runtime without restarting the machine
- `liveSeek`: supports live seeking or rewinding of input state while the machine is running
- `nonRealtimeOnly`: requires paused or offline operation
- `headlessSafe`: works without a windowing backend

Recommended contract shape:

- `IInputPlugin` as the subsystem-facing root contract
- optional specialized roles such as `IDigitalInputSourcePlugin`, `IAnalogInputSourcePlugin`, and `IInputReplayPlugin`
- capabilities returned by value and validated by `InputService`

### Host and Backend Adapters

Adapters translate host behavior into subsystem behavior.

Initial adapter targets:

- SDL keyboard and controller input
- replay-file input for deterministic testing
- tooling or CLI input injection for smoke coverage

Later adapters may include:

- netplay or remote input bridges
- platform-native controller backends

Adapters must not mutate machine state directly. They publish logical input into the subsystem engine, and the machine consumes the committed state at the step boundary.

## Preferred Shape

```text
machine/plugins/input/
    InputEngine.hpp
    InputService.hpp
    InputPlugin.hpp
    InputCapabilities.hpp
    InputMapping.hpp
    adapters/
        SdlInputAdapter.hpp
        SdlInputAdapter.cpp
        ReplayInputAdapter.hpp
        ReplayInputAdapter.cpp

tests/
    smoke_input_engine.cpp
    smoke_input_service.cpp
    smoke_input_transport.cpp
    smoke_input_fallback.cpp
```

## Live-Path and Offline API Split

The input subsystem should expose two API families.

Live-safe APIs:

- publish or commit prevalidated input snapshots into fixed-capacity staging
- sample current committed digital mask
- sample current committed analog state
- acknowledge generation boundaries without reallocating storage

Offline APIs:

- remap host bindings
- attach or detach adapters
- load replay scripts or recorded traces
- inspect buffered host events in detail
- clear diagnostics and counters

Do not expose one convenience API that both mutates mappings and samples current input while the machine is running.

## Determinism and Sampling Rules

The machine-visible input state must change only at explicit sampling boundaries.

Required rules:

- host adapters may stage many events between machine steps
- `InputEngine` collapses staged host events into a committed logical input snapshot
- `GameBoyMachine` or future machines consume only the committed snapshot
- replay adapters must not depend on host wall-clock time; they advance from machine ticks or recorded event indices

The subsystem should prefer the last fully committed logical state rather than a partially updated state.

## Fallback Behavior

Input failures must be visible and deterministic.

Required degraded behavior:

- on transient adapter polling failure, preserve the last committed logical input state and increment a polling failure counter
- on adapter detach or hard backend loss, transition to neutral input state and latch the backend error
- on queue overflow, either drop the newest host event batch or coalesce to the latest complete snapshot, using the overflow policy guidance below; increment the overflow counter in either case
- on unsupported analog input, expose zeroed analog channels rather than undefined data

### Queue Overflow Policy

The "on queue overflow" clause intentionally allows two strategies, but the implementation must choose one policy per adapter path and document it:

- `drop newest host event batch`: prefer lower latency when stale-but-complete committed state is better than spending extra work preserving every incoming event burst
- `coalesce to the latest complete snapshot`: prefer state fidelity when the adapter can cheaply collapse many host events into one deterministic logical snapshot without exposing partial updates

Decision guidance:

- use `drop newest host event batch` for high-frequency live host input where responsiveness matters more than preserving every intermediate burst
- use `coalesce to the latest complete snapshot` for adapters that already build snapshot-style logical state and can replace many pending event batches with one complete state image

In both strategies, the overflow counter must be incremented once per overflow incident, and the resulting committed machine-visible state must remain deterministic.

The service should expose at least:

- `pollFailureCount`
- `eventOverflowCount`
- `neutralFallbackCount`
- `activeAdapterSummary`
- `lastBackendError`
- `lastCommittedGeneration`

## Lifecycle Rules

The input subsystem must define explicit state for:

- `Detached`
- `Paused`
- `Active`
- `Faulted`

Rules:

- configuration and remapping are always allowed while `Detached`
- configuration is allowed while `Paused`
- live adapter replacement is rejected while `Active` unless the adapter explicitly advertises hot-swap safety
- machine reset and snapshot restore must advance an input generation boundary so stale host events cannot leak into the new machine generation
- replay source seek or rewind is paused-only unless the replay contract explicitly supports live seek

## Thread Ownership

Thread ownership must be explicit.

- host thread: polls SDL or other host APIs and stages events into fixed-capacity buffers
- machine thread: commits staged input and samples one deterministic snapshot per step
- tooling thread: reads diagnostics and offline state only

Blocking rules:

- the machine thread must not block on host input APIs
- the host thread may block on backend APIs, but not while holding subsystem state needed by the machine thread
- remapping and adapter mutation must happen off the machine thread or while the subsystem is paused

## Integration With Existing API

This spec keeps the current plugin categories but re-centers ownership.

Short-term alignment:

- keep `PluginCategory::DigitalInput` and `PluginCategory::AnalogInput`
- keep `MachineEventType::DigitalInputChanged` and `MachineEventType::AnalogInputChanged`
- keep machine-owned `InputButton`

Planned evolution:

- move SDL input state management behind `InputService`
- make `IDigitalInputPlugin` and `IAnalogInputPlugin` thin compatibility layers or adapters to the new subsystem contract
- centralize queueing, diagnostics, and generation handling in `machine/plugins/input/`

## Test Requirements

### Engine smoke test

Verify:

- staged host events collapse into one committed snapshot
- generation reset drops stale staged input
- queue overflow increments counters and preserves deterministic output
- analog and digital state remain independent

### Service smoke test

Verify:

- unsafe adapters are rejected while active
- remapping is allowed while paused and rejected while active when unsupported
- diagnostics surface adapter fault state
- detach transitions input to neutral state

### Transport smoke test

Verify:

- SDL or replay adapters translate host input into machine-owned logical buttons
- machine polling observes the committed snapshot only
- analog input sampling remains stable when no analog hardware is present

### Fallback smoke test

Verify:

- polling failure preserves prior committed state
- hard backend loss transitions to neutral input and latches the error
- queue overflow remains deterministic and visible

## Acceptance Criteria

The input subsystem is ready when:

- frontend adapters no longer own durable machine input policy
- live-path and offline APIs are separate
- capability checks are explicit and enforced by `InputService`
- generation-boundary behavior is deterministic and tested
- diagnostics explain degraded input behavior without a debugger
