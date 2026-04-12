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
- Constructor overload taking `AudioEngineConfig` for callers that want explicit defaults.

### Machine / MachineView

- `Machine::audioService()` returns `AudioService&`.
- `Machine::setAudioService(std::unique_ptr<AudioService>) -> bool` swaps the service, returning `true` on success and `false` if the swap is disallowed by the contract.
- `MachineView::audioService()` returns `AudioService&`.

## Ownership and Lifetime

- `Machine` owns `std::unique_ptr<AudioService> audioService_`.
- `Machine` constructs a default `AudioService` in its constructor; `audioService_` is never null.
- `MachineView` does **not** store a new member. It exposes `audioService()` as an inline accessor that reaches `machine.audioService()` to avoid ABI/layout changes.
- **Swap contract:** `Machine::setAudioService(...)` is only legal when `pluginManager().initialized()` is `false`. If called while the plugin manager is initialized it must return `false`. The caller must also ensure no `MachineView` instances outlive the swap (views are ephemeral and invalidated by a successful swap). This is a documented rule; no runtime check is required beyond the plugin manager gate.
- `Machine::setAudioService(nullptr)` is rejected and returns `false` (service is never null).
- Swapping the service updates the `Machine` and is visible through new `MachineView` instances on subsequent plugin calls.
- **Thread-safety:** `AudioEngine::appendRecentPcm(...)` and `AudioEngine::render(...)` are safe to call concurrently from the emulation thread and audio callback. `resetStream()`, `resetStats()`, and `configure(...)` are **not** real-time safe and must only be called when the backend is closed or paused. This is a documented rule; no runtime guard is required.

## SDL Frontend Changes

- Remove local `AudioEngine` ownership.
- Use `view.audioService().engine()` for appends and stats.
- Call `view.audioService().resetStream()` / `resetStats()` when needed.
- Keep `IAudioOutputBackend` handling and device open logic in the SDL frontend.
- Continue populating `SdlFrontendStats` from the shared engine.
- If the backend is open, SDL should avoid calling `resetStream()`/`resetStats()` from the emulation thread to prevent races with the audio callback.

## Tests

- Update existing SDL smoke tests to operate with the shared engine without changing the SDL plugin ABI.
- Add `smoke_audio_service`:
  - Default `AudioService` exists immediately after `Machine` construction (non-null invariant).
  - `MachineView::audioService()` exists and is mutable.
  - Swapping the service before plugin init returns the new instance through `MachineView`.
  - Swapping the service after plugin initialization is rejected (`setAudioService` returns `false`).
  - `resetStream()` / `resetStats()` are callable from plugin/test code.

## Risks and Mitigations

- **Risk:** Plugins hold stale `AudioService` references if the service is swapped.
  - **Mitigation:** Encourage swaps during controlled lifecycle points; use new `MachineView` instances after swaps.
- **Risk:** Expanded `MachineView` surface.
  - **Mitigation:** Keep the API minimal and focused on `AudioService`.

## Follow-ups

- Evaluate a host-level audio graph after additional frontends exist.
- Consider exposing additional shared audio metrics if multiple backends rely on them.
