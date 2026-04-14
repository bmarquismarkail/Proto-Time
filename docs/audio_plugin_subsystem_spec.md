# Proto-Time Audio Plugin Subsystem Specification

This document defines the target and current-reference architecture for the Proto-Time audio plugin subsystem.

Unlike the other non-audio subsystem specs, this document describes a subsystem that already exists in meaningful form in the repository. It should therefore be treated as both a design reference and a normalization target for future audio changes.

## Goal

The audio subsystem should deliver machine audio to host backends without letting backend behavior, processor behavior, or reset timing compromise machine stability.

The subsystem must:

- preserve deterministic machine-owned audio production
- separate hot-path buffering and resampling from lifecycle and policy
- reject incompatible processors from the live callback path
- handle reset and generation boundaries safely relative to active callbacks
- degrade predictably on underrun, overrun, or processor-capacity mismatch
- expose cheap diagnostics that explain degraded behavior

## Current Repository Anchor

The current codebase already implements the core blueprint layers.

Existing primary pieces:

- `machine/plugins/AudioEngine.hpp`
- `machine/AudioService.hpp`
- `machine/AudioPipeline.hpp`
- `machine/plugins/AudioOutput.hpp`
- backend adapters in `machine/plugins/audio_output/` and `machine/plugins/sdl_frontend/`

Key existing behaviors already enforced in code:

- `AudioService::canPerformReset()` gates reset and configure operations
- `AudioService::setBackendPausedOrClosed(bool)` is the lifecycle contract between the service and audio backends
- `AudioService::addProcessor(...)` accepts only processors with `AudioProcessorCapabilities { realtimeSafe=true, fixedCapacityOutput=true }`
- `AudioEngine` handles deferred reset publication at the callback boundary instead of destructive producer-side reset
- callback-capacity mismatch in the processor pipeline preserves dry output and increments `pipelineCapacitySkipCount`
- underrun fills silence and increments underrun diagnostics

This subsystem is the reference implementation behind the plugin blueprint.

## Subsystem Responsibilities

### Engine

`AudioEngine` owns hot-path mechanics.

Responsibilities:

- ring-buffer storage for source PCM samples
- source-to-device sample-rate translation through the resampler
- deferred reset handoff at the callback boundary
- overrun drop accounting and buffered high-water tracking
- real-time producer and consumer entry points

The engine should remain:

- lock-free on the producer and callback paths
- policy-light
- independent of SDL, files, or dummy backends

The engine should not:

- decide when reset is legal
- own processor admission policy
- open or close output devices
- allocate memory on the callback path
- silently hide safety violations

### Service

`AudioService` owns lifecycle and policy.

Responsibilities:

- gate reset, configure, and processor mutation through `canPerformReset()`
- own the audio processor pipeline and its fixed-capacity scratch storage
- validate live callback compatibility before processor admission
- coordinate backend paused or closed state
- expose the audio engine and pipeline through a stable machine-owned boundary
- define degraded behavior for pipeline incompatibility and backend transitions

The service is the authority for:

- when reset and configure are legal
- when processors may be added or removed
- how callback capacity is configured
- what happens when a processor cannot safely participate on the live path

### Plugin Contract

The audio subsystem uses explicit processor and backend contracts.

Existing processor contract:

- `IAudioProcessor`
- `AudioProcessorCapabilities`

Existing mandatory live-path capabilities:

- `realtimeSafe`
- `fixedCapacityOutput`

Existing backend contract:

- `IAudioOutputBackend`

Backend adapters must provide:

- stable backend naming
- open and close lifecycle
- readiness reporting
- latched error string and error code
- device information reporting

Recommended audio contract guidance going forward:

- keep `realtimeSafe` and `fixedCapacityOutput` mandatory for live processors
- add capability fields only when they reflect correctness constraints rather than convenience
- keep host output backends distinct from audio processors

### Host and Backend Adapters

Adapters bridge the audio subsystem to host environments.

Current adapters include:

- SDL audio output
- file audio output
- dummy audio output

Adapter responsibilities:

- open or close the host backend
- report effective sample-rate and callback chunk configuration
- call `setBackendPausedOrClosed(true)` before close or non-real-time reconfiguration
- call `setBackendPausedOrClosed(false)` only after callback-capable open succeeds
- expose stable backend errors and device information

Adapters must not:

- own processor admission rules
- reset engine state while callbacks may still be active
- hide device-open or write failures

## Preferred Shape

```text
machine/
    AudioService.hpp
    AudioPipeline.hpp

machine/plugins/
    AudioEngine.hpp
    AudioOutput.hpp
    SdlAudioResampler.hpp
    audio_output/
        DummyAudioOutput.hpp
        DummyAudioOutput.cpp
        FileAudioOutput.hpp
        FileAudioOutput.cpp
    sdl_frontend/
        SdlAudioOutput.hpp
        SdlAudioOutput.cpp

tests/
    smoke_audio_engine.cpp
    smoke_audio_service.cpp
    smoke_audio_pipeline.cpp
    smoke_sdl_audio_output_backend.cpp
    smoke_dummy_audio_output.cpp
    smoke_file_audio_output.cpp
    smoke_sdl_audio_transport.cpp
```

This shape already exists in substance, even if the files are not nested under one audio-only directory.

## Live-Path and Offline API Split

Live-safe APIs:

- `AudioEngine::appendRecentPcm(...)`
- `AudioEngine::render(...)`
- `AudioService::renderForOutput(...)`
- `IAudioProcessor::process(...)` against caller-provided fixed-capacity buffers

Offline or paused-only APIs:

- `AudioService::addProcessor(...)`
- `AudioService::clearProcessors()`
- `AudioService::configureFixedCallbackCapacity(...)`
- `AudioService::configureEngine(...)`
- `AudioService::resetStream()`
- `AudioService::resetStats()`
- backend open and close operations

This split must remain strict. If an operation resizes vectors, swaps processors, reconfigures the engine, or changes backend lifecycle state, it is not a live callback API.

## Audio Flow Model

The current recommended flow is:

1. the machine produces recent PCM and a frame counter
2. `AudioEngine::appendRecentPcm(...)` stages source samples into the ring buffer
3. the backend callback asks `AudioService::renderForOutput(...)` for one output chunk
4. the service asks the engine to render into the output buffer
5. the service optionally applies the processor pipeline using preallocated fixed-capacity scratch buffers
6. the backend writes the final chunk to SDL, file output, or another host transport

This keeps machine-side production and host-side delivery decoupled.

## Reset and Generation Boundary Rules

The audio subsystem crosses a live producer-consumer boundary and therefore must preserve explicit reset rules.

Required rules:

- producer-side code must not destructively reset callback-consumed state while a backend may still be active
- reset observation occurs at the callback boundary through the engine's deferred reset handoff
- post-reset audio should not be mixed into the old generation before the callback boundary has been flushed
- reset, stats reset, and engine reconfiguration are legal only when `canPerformReset()` is true
- backend lifecycle code is responsible for marking paused or closed state correctly before and after open or close transitions

This is one of the most important subsystem contracts in the repository and should not be weakened for convenience.

## Fallback Behavior

Audio failures must degrade predictably.

Current and required degraded behaviors:

- on underrun, emit silence and increment `underrunCount` and `silenceSamplesFilled`
- on ring-buffer pressure, drop according to engine policy and increment `overrunDropCount` and `droppedSamples`
- on processor callback-capacity mismatch, preserve dry output and increment `pipelineCapacitySkipCount`
- on processor failure to produce output within fixed capacity, output silence for the affected chunk rather than exposing undefined audio
- on backend unavailability, report stable backend errors and permit degraded backend selection or headless operation where applicable

The subsystem must never fail invisibly.

## Diagnostics

The engine already exposes most of the required diagnostics through `AudioEngineStats`.

Required diagnostics include:

- `bufferedHighWaterSamples`
- `callbackCount`
- `samplesDelivered`
- `underrunCount`
- `silenceSamplesFilled`
- `overrunDropCount`
- `droppedSamples`
- `sourceSamplesPushed`
- `sourceSamplesConsumed`
- `outputSamplesProduced`
- `pipelineCapacitySkipCount`
- `resamplingActive`
- `resampleRatio`

Backend-visible diagnostics should also include:

- last backend error string
- backend error code
- active backend name
- device sample rate
- callback chunk size

These diagnostics must stay cheap to read from non-hot paths.

## Lifecycle Rules

The audio subsystem must define explicit backend-relative states for:

- `Detached`
- `PausedOrClosed`
- `Active`
- `Faulted`

`AudioService::setBackendPausedOrClosed(bool)` is a safety gate, not the full lifecycle enum. It answers one question only: may non-real-time reset/configure work run without racing a live callback?

State mapping:

| Backend-relative state | `setBackendPausedOrClosed(...)` value | Meaning | `canPerformReset()` |
| --- | --- | --- | --- |
| `Detached` | `true` | no backend is attached or no callback-capable device/thread is live | `true` |
| `PausedOrClosed` | `true` | backend exists but callbacks are intentionally quiesced for close, pause, reset, or reconfiguration | `true` |
| `Active` | `false` | backend open succeeded and callbacks or backend worker activity may consume audio | `false` |
| `Faulted` | transition through `true` before control returns to non-real-time code | backend hit a runtime failure, latched error state, and must quiesce callbacks before reset/configure becomes legal again | `true` after quiesce |

Operational rule: `canPerformReset()` returns `true` if and only if the backend has been driven into a reset-safe quiesced state. In the four-state model above that means `Detached` or `PausedOrClosed`, and it also applies to `Faulted` only after the backend has stopped live callback activity and published the equivalent paused-or-closed gate.

Simple transition model:

```text
Detached --attach/open begin--> PausedOrClosed --setBackendPausedOrClosed(false)--> Active
Active --pause/close/reconfigure begin--> PausedOrClosed --detach--> Detached
Active --runtime failure--> Faulted --quiesce callbacks + setBackendPausedOrClosed(true)--> PausedOrClosed
Faulted --detach--> Detached
```

Allowed operations by state:

| Operation | Detached | PausedOrClosed | Active | Faulted |
| --- | --- | --- | --- | --- |
| processor mutation | allowed | allowed | forbidden | allowed only after callbacks are quiesced |
| reset/configure/stats reset | allowed | allowed | forbidden | allowed only after callbacks are quiesced |
| attach/open backend | allowed | allowed | forbidden | allowed after fault cleanup |
| detach/close backend | no-op or allowed | allowed | allowed, but must transition through `PausedOrClosed` first | allowed |
| active callback or backend worker consumption | forbidden | forbidden | allowed | forbidden once fault is latched |

Rules:

- processor mutation is allowed only while `PausedOrClosed`
- reset and configure operations are allowed only while `PausedOrClosed`
- backend adapters are responsible for updating service state on lifecycle transitions
- backends must call `setBackendPausedOrClosed(true)` before close, pause, reset-safe reconfiguration, or fault handoff to non-real-time code
- backends must call `setBackendPausedOrClosed(false)` only after open succeeds and callback-capable activity is actually live
- active callbacks must only observe preallocated buffers and prevalidated processors
- backend attach and detach churn must preserve service invariants and reset safety guarantees

## Thread Ownership

- machine thread: produces recent PCM and frame counters
- audio callback or backend thread: consumes buffered samples through `renderForOutput(...)`
- non-real-time control thread: configures the engine, processors, callback capacity, and backend lifecycle

Blocking rules:

- the machine thread must not block on backend APIs
- the callback thread must not allocate or depend on non-real-time configuration changes
- reset and configuration must not run concurrently with live callback activity
- processor installation and removal must remain outside the callback path

## Integration With The Frontend Plugin

The SDL frontend currently integrates with the audio subsystem but should be understood as an adapter user, not the owner of audio policy.

Current integration behavior:

- the frontend samples machine audio state from `MachineView`
- it appends recent PCM into the shared `AudioService`
- it resets stream or stats only when `canPerformReset()` is true
- it exposes frontend-level copies of global engine diagnostics

That arrangement is acceptable so long as subsystem ownership remains with `AudioService`, `AudioEngine`, the pipeline, and output backends.

## Test Requirements

### Engine smoke test

Verify:

- ring-buffer append and render behavior
- deferred reset publication across callback boundaries
- resampling behavior under source and device sample-rate mismatch
- overrun and underrun accounting

### Service smoke test

Verify:

- paused-only reset and configure gating
- rejection of processors lacking `realtimeSafe`
- rejection of processors lacking `fixedCapacityOutput`
- dry-output preservation on callback-capacity fallback

### Transport smoke test

Verify:

- SDL backend open and close lifecycle
- file and dummy backends behave stably
- effective sample-rate and callback chunk configuration propagate correctly
- lifecycle churn preserves service invariants

### Fallback smoke test

Verify:

- underrun emits silence and increments counters
- overrun accounting is visible
- callback-capacity mismatch preserves dry output and records fallback
- backend unavailability produces a stable error state rather than silent failure

## Acceptance Criteria

The audio subsystem is ready when:

- engine, service, processor contract, and backend adapter boundaries remain explicit
- all mutation and reset rules are enforced through `AudioService`
- live processors are capability-gated rather than admitted by convention
- callback-time degraded behavior is deterministic and visible through counters
- backend lifecycle churn does not compromise reset safety or callback correctness
