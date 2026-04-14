# Proto-Time Video Plugin Subsystem Specification

This document defines the target architecture for the Proto-Time video plugin subsystem.

It applies the plugin subsystem blueprint to machine video inspection, frame preparation, and backend presentation.

## Goal

The video subsystem should separate machine-side frame and state extraction from host-side presentation.

The subsystem must:

- preserve deterministic machine execution regardless of renderer performance
- make presentation fallback explicit when a backend cannot keep up
- allow headless and backend-available operation through the same service boundary
- keep host API details out of frame-building and state-sampling code
- make thread ownership between machine and presentation paths obvious

## Current Repository Anchor

The current codebase already exposes:

- `PluginCategory::Video`
- `MachineEventType::VBlank`
- `MachineEventType::MemoryWriteObserved` for video-mapped regions
- `MachineView::videoState()`
- an SDL frontend plugin that both observes video events and presents frames

That SDL plugin is useful as a reference adapter, but it currently mixes subsystem responsibilities that should be separated into engine, service, contract, and adapter layers.

## Subsystem Responsibilities

### Engine

`VideoEngine` owns hot-path mechanics.

Responsibilities:

- capture or assemble immutable frame packets from machine-visible video state
- track the last valid frame for fallback presentation
- manage fixed-capacity queues between machine production and backend consumption
- support generation-aware frame handoff on reset and ROM load
- collect cheap counters for dropped packets and compatibility fallback

The engine should not:

- create windows or textures
- decide whether backend reconfiguration is currently legal
- block on the presentation backend
- hide dropped-frame behavior

### Service

`VideoService` owns lifecycle and policy.

Responsibilities:

- attach and detach presentation backends
- validate video processors or overlays before live admission
- configure format, scale, palette, and debug-view policy
- gate reset and reconfiguration behind paused or closed backend state
- surface diagnostics, active backend details, and degraded-mode state

The service defines:

- what presentation modes are legal while active
- whether overlays, filters, or frame processors may be added live
- whether debug frame generation is enabled
- what fallback frame is used when presentation fails

### Plugin Contract

The subsystem should grow beyond a raw `IVideoPlugin` event sink and define explicit capabilities.

Recommended capabilities:

- `realtimeSafe`: safe on the live video path
- `frameSizePreserving`: preserves the input frame dimensions
- `snapshotAware`: understands machine snapshot and reset boundaries
- `deterministic`: does not inject wall-clock-visible machine differences
- `headlessSafe`: can operate without a presentation backend
- `requiresHostThreadAffinity`: must run on a specific UI thread
- `nonRealtimeOnly`: requires paused or offline execution

Recommended contract roles:

- `IVideoPresenterPlugin` for backend presentation
- `IVideoFrameProcessorPlugin` for overlays, filters, or capture transforms
- `IVideoCapturePlugin` for screenshot or recording sinks

`VideoService` should validate these capabilities before a plugin enters the live frame path.

### Host and Backend Adapters

Adapters bridge video frames to host environments.

Initial adapter targets:

- SDL window and texture presentation
- headless frame dumper for testing
- file or image capture adapter for tooling

Later adapter targets may include:

- streaming or remote inspection backends
- debugger-oriented tiled VRAM or OAM viewers

Adapters should consume prepared frame packets. They should not reach back into machine memory from presentation callbacks.

## Preferred Shape

```text
machine/plugins/video/
    VideoEngine.hpp
    VideoService.hpp
    VideoPlugin.hpp
    VideoCapabilities.hpp
    VideoFrame.hpp
    adapters/
        SdlVideoPresenter.hpp
        SdlVideoPresenter.cpp
        HeadlessFrameDumper.hpp
        HeadlessFrameDumper.cpp

tests/
    smoke_video_engine.cpp
    smoke_video_service.cpp
    smoke_video_transport.cpp
    smoke_video_fallback.cpp
```

## Live-Path and Offline API Split

Live-safe APIs:

- submit prepared frame packets
- present or skip one frame packet
- query cheap counters and the last committed frame generation

Offline APIs:

- change scale or debug-view mode
- reconfigure palettes or capture settings
- export screenshots or frame dumps
- inspect intermediate frame-build diagnostics

Do not let the presentation callback build frames from scratch or read machine memory opportunistically.

## Frame Production Model

The recommended production model is:

1. machine thread observes `VBlank` or another explicit frame-complete boundary
2. `VideoEngine` samples machine-owned video state into an immutable frame packet
3. the frame packet is queued for a presenter backend or capture sink
4. the presenter consumes the packet without reaching into live machine state

Optional support for `MemoryWriteObserved` may remain for debug views and incremental invalidation, but user-visible frame presentation should continue to commit at explicit frame boundaries.

## Fallback Behavior

Video failures must remain visible and deterministic.

Required degraded behavior:

- if presentation fails, preserve the last valid frame or present a blank frame when none exists
- if the frame queue is full, drop according to a documented policy and increment a drop counter
- if a frame processor is incompatible with live-path constraints, bypass it and increment a compatibility fallback counter
- if no video backend is available, run headless while preserving capture and diagnostic state where possible

The service should expose at least:

- `presentFailureCount`
- `droppedFrameCount`
- `compatibilityFallbackCount`
- `headlessModeActive`
- `lastBackendError`
- `frameQueueHighWaterMark`
- `lastPresentedGeneration`

## Lifecycle Rules

The video subsystem must define explicit state for:

- `Detached`
- `Paused`
- `Active`
- `Headless`
- `Faulted`

Rules:

- backend reconfiguration is allowed while `Detached` or `Paused`
- live addition or removal of frame processors is rejected unless the processor advertises hot-swap compatibility
- reset and snapshot restore advance a frame generation boundary
- present callbacks must not observe partially reset state
- headless mode is a valid operational state, not an implicit failure

## Thread Ownership

- machine thread: produces committed frame packets from machine-visible state
- UI or presenter thread: consumes frame packets and interacts with SDL or other host APIs
- tooling thread: queries diagnostics, capture state, or offline frame exports

Blocking rules:

- the machine thread must not block on windowing or GPU operations
- the presenter thread may block on host presentation APIs, but only on data already handed off by the engine
- service-level reconfiguration must not mutate presenter-owned state concurrently with live presentation

## Integration With Existing API

This spec keeps the current event vocabulary as the near-term integration point.

Short-term alignment:

- continue using `PluginCategory::Video`
- continue using `MachineEventType::VBlank`
- keep `MachineView::videoState()` as the machine-facing snapshot source

Planned evolution:

- move frame queueing, fallback counters, and backend lifecycle rules into `VideoService`
- make the SDL frontend depend on the video subsystem as an adapter rather than owning the whole path
- reserve `MemoryWriteObserved`-driven updates for diagnostics and debug viewers rather than primary presentation

## Test Requirements

### Engine smoke test

Verify:

- frame packets are built at explicit frame boundaries
- reset advances frame generation safely
- the last valid frame is preserved for fallback
- queue capacity limits are deterministic

### Service smoke test

Verify:

- paused-only configuration rules are enforced
- incompatible processors are rejected from the live path
- headless mode reports correctly
- backend fault state is visible through diagnostics

### Transport smoke test

Verify:

- SDL or headless presenters consume prepared frame packets cleanly
- backend open and close transitions are stable
- capture sinks receive frames without mutating the live machine path

### Fallback smoke test

Verify:

- present failure yields the last valid frame or blank fallback deterministically
- full queue behavior increments counters and remains predictable
- incompatible live processors are bypassed visibly

## Acceptance Criteria

The video subsystem is ready when:

- frame building and backend presentation are separate responsibilities
- no live presentation path reaches back into machine memory opportunistically
- lifecycle gates are explicit and enforced by `VideoService`
- headless and backend-present modes are both supported deliberately
- dropped-frame and fallback behavior are observable through cheap diagnostics
