# Stage 5 Audio Service Design

Date: 2026-04-12

## Goal

Introduce a machine-level audio service that exposes a shared `AudioEngine` to plugins and frontends while keeping device I/O in backend-specific code. The service must be swappable at runtime, and plugins must be allowed to reset the engine.

## Non-Goals

- No new audio processing chain beyond the existing `AudioEngine` and `IAudioOutputBackend`.
- No SDL plugin ABI changes.
- No resampler changes beyond what Stage 2 introduced.

## Current State

- `AudioEngine` lives in the SDL frontend plugin.
- `IAudioOutputBackend` and the SDL audio backend handle device open/close and callbacks.
- The SDL frontend pulls audio stats directly from its local engine.

## Proposed API

### New Header

`machine/AudioService.hpp`:

- `class AudioService` that owns `AudioEngine`.
- `AudioEngine& engine()` and `const AudioEngine& engine() const`.
- `void resetStream()` and `void resetStats()` pass-throughs.
- Optional constructor overload taking `AudioEngineConfig` (if needed by future callers).

### Machine / MachineView

- `Machine::audioService()` returns `AudioService&`.
- `Machine::setAudioService(std::unique_ptr<AudioService>)` swaps the service.
- `MachineView::audioService()` returns `AudioService&`.

## Ownership and Lifetime

- `Machine` owns `std::unique_ptr<AudioService> audioService_`.
- `MachineView` holds a reference to the current `AudioService`.
- Swapping the service updates the `Machine` and must be visible through new `MachineView` instances on subsequent plugin calls.
- Plugins may call `resetStream()` and `resetStats()` at their discretion.

## SDL Frontend Changes

- Remove local `AudioEngine` ownership.
- Use `view.audioService().engine()` for appends and stats.
- Call `view.audioService().resetStream()` / `resetStats()` when needed.
- Keep `IAudioOutputBackend` handling and device open logic in the SDL frontend.
- Continue populating `SdlFrontendStats` from the shared engine.

## Tests

- Update existing SDL smoke tests to operate with the shared engine without changing the SDL plugin ABI.
- Add `smoke_audio_service`:
  - `MachineView::audioService()` exists and is mutable.
  - Swapping the service returns a new instance through `MachineView`.
  - `resetStream()` / `resetStats()` are callable from plugin/test code.

## Risks and Mitigations

- **Risk:** Plugins hold stale `AudioService` references if the service is swapped.
  - **Mitigation:** Encourage swaps during controlled lifecycle points; use new `MachineView` instances after swaps.
- **Risk:** Expanded `MachineView` surface.
  - **Mitigation:** Keep the API minimal and focused on `AudioService`.

## Follow-ups

- Evaluate a host-level audio graph after additional frontends exist.
- Consider exposing additional shared audio metrics if multiple backends rely on them.
