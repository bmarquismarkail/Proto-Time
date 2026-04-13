# Stage 5 Audio Service Design

Date: 2026-04-12

## Goal

Introduce a machine-level audio service that exposes a shared `AudioEngine` to plugins and frontends while keeping device I/O in backend-specific code. The service must be swappable during controlled lifecycle windows, and plugins must be allowed to reset the engine.

## Non-Goals

- No new audio processing chain beyond the existing `AudioEngine` and `IAudioOutputBackend`.
- No SDL plugin ABI changes.
- No resampler changes beyond what Stage 2 introduced.

## Current State

- `AudioEngine` lives in the SDL frontend plugin.
- `IAudioOutputBackend` and the SDL audio backend handle device open/close and callbacks.
- Additional backends now exist: dummy (in-memory render loop) and file (raw PCM output).
- The SDL frontend pulls audio stats directly from its local engine.

## Proposed API

### New Header

`machine/AudioService.hpp`:

- `class AudioService` that owns `AudioEngine`.
- `AudioEngine& engine()` and `const AudioEngine& engine() const`.
- `void resetStream()` and `void resetStats()` non-real-time reset helpers.
- `void configureEngine(const AudioEngineConfig&)` non-real-time reconfiguration helper.
- `bool canPerformReset() const noexcept` safe-query method for callers to check whether reset/configure is currently allowed relative to callback activity.
- `void setBackendPausedOrClosed(bool) noexcept` backend lifecycle hook used by frontend/backend code to publish whether callback execution is considered active.
- Constructor overload taking `AudioEngineConfig` for callers that want explicit defaults.

Semantics and thread-safety for these methods:

- `AudioEngine::appendRecentPcm(...)` and `AudioEngine::render(...)` remain the real-time data path and are unchanged.
- `AudioService::resetStream()`, `resetStats()`, and `configureEngine(...)` are not real-time safe and are only valid when `canPerformReset()` is `true`.
- In debug builds, unsafe reset/configure calls should assert and return early.
- Non-real-time reset/configure critical sections should be serialized (for example with an internal mutex) to prevent races between control-thread operations.

### Machine / MachineView

- `Machine::audioService()` returns `AudioService&` and `const AudioService&`.
- `Machine::setAudioService(std::unique_ptr<AudioService>) -> bool` swaps the service, returning `true` on success and `false` if the swap is disallowed by the contract.
- `MachineView::audioService()` returns `AudioService&` and `const AudioService&`. The non-const overload uses a documented `const_cast` escape to allow plugin-side resets without changing `MachineView` storage. The intent is to mutate only the audio service, not other machine state. **Precondition:** this overload is only valid when the underlying `Machine` is non-const; if a `MachineView` is derived from a `const Machine`, only the `const` overload may be used (non-const use is undefined behavior).
  - For this const-cast precondition specifically, no runtime guard is required; misuse remains undefined behavior and must be avoided by callers.

## Ownership and Lifetime

- `Machine` owns `std::unique_ptr<AudioService> audioService_`.
- `Machine` constructs a default `AudioService` in its constructor; `audioService_` is never null.
- Default `AudioService` uses `AudioEngineConfig` defaults (48 kHz source/device, 2048 ring buffer samples, 256 frame chunk samples).
- `MachineView` does **not** store a new member. It exposes `audioService()` as an inline accessor that reaches `machine.audioService()` to avoid ABI/layout changes.
- **Swap contract:** `Machine::setAudioService(...)` is only legal when `pluginManager().initialized()` is `false`. If called while the plugin manager is initialized it must return `false`, even if no plugins are loaded. The caller must also ensure no `MachineView` instances outlive the swap (views are ephemeral and invalidated by a successful swap). The plugin manager gate is the only runtime enforcement; all other safety is caller responsibility.
- **Swap safety:** If a `MachineView` outlives a successful swap, behavior is undefined. Callers must only swap before any `MachineView` is handed out or after all views are known to be destroyed. No runtime checks are required.
- `Machine::setAudioService(nullptr)` is rejected and returns `false` (service is never null).
- Swapping the service updates the `Machine` and is visible through new `MachineView` instances on subsequent plugin calls.
- **Thread-safety:** `AudioEngine::appendRecentPcm(...)` and `AudioEngine::render(...)` are safe to call concurrently from the emulation thread and audio callback. `AudioService::resetStream()`, `resetStats()`, and `configureEngine(...)` are **not** real-time safe and must only be called when the backend is closed or paused (`canPerformReset() == true`). Runtime debug-guard behavior and header documentation are part of the contract for these non-real-time methods.

## SDL Frontend Changes

- Remove local `AudioEngine` ownership.
- Use `view.audioService().engine()` for appends and stats.
- Call `view.audioService().resetStream()` / `resetStats()` when needed.
- Keep `IAudioOutputBackend` handling and device open logic in the SDL frontend.
- Continue populating `SdlFrontendStats` from the shared engine.
- If the backend is open, SDL should avoid calling `resetStream()`/`resetStats()` from the emulation thread to prevent races with the audio callback.
 - `AudioOutputOpenConfig` includes `filePath` for file-backed output (raw `int16_t` PCM).

## Tests

- Update existing SDL smoke tests to operate with the shared engine without changing the SDL plugin ABI.
- Add `smoke_audio_service`:
  - Default `AudioService` exists immediately after `Machine` construction (non-null invariant).
  - `MachineView::audioService()` exists and is mutable.
  - Swapping the service before plugin init returns the new instance through `MachineView`.
  - Swapping the service after plugin initialization is rejected (`setAudioService` returns `false`).
  - `resetStream()` / `resetStats()` are callable from plugin/test code.
  - Add the executable and test registration in `CMakeLists.txt`.
  - `setAudioService(nullptr)` returns `false`.
  - Failed swap leaves the original service intact (validate identity/state).

## Risks and Mitigations

- **Risk: Concurrency violations.** Plugins/frontends may call `AudioService::resetStream()`, `resetStats()`, or configure paths while the backend callback thread can still execute `AudioEngine::render(...)`, creating races.
  - **Mitigation:** Treat reset/configure paths as non-real-time-only operations, enforce debug assertions for unsafe calls, provide and use a safe-query method (for example `AudioService::canPerformReset()` / `isResetSafe()`), and protect non-real-time critical sections with a mutex or equivalent guard.
- **Risk: Const-cast undefined behavior.** `MachineView::audioService()` non-const access from a view derived from `const Machine` can become undefined behavior because of the `const_cast` escape hatch.
  - **Mitigation:** Prefer redesign that avoids `const_cast` in the long term; until then, document the precondition explicitly, add debug-only assertions where practical, and treat this as an accepted risk only for well-scoped plugin call paths.
- **Risk: Stale view crashes.** Long-lived `MachineView` instances or cached `AudioService*` references can outlive `Machine::setAudioService(...)` swaps and dereference stale service state.
  - **Mitigation:** Strengthen lifetime documentation for `MachineView`/`Machine`/`AudioService` relationships, keep views ephemeral, and consider generation counters or debug fencing to detect stale-view use during development.
- **Risk: Failed swap cleanup confusion.** Callers may mis-handle ownership transfer around `Machine::setAudioService(std::unique_ptr<AudioService>)`, especially when swaps fail, causing leak/double-free concerns.
  - **Mitigation:** Document ownership rules and cleanup semantics explicitly: ownership is transferred into the call by value, successful swaps replace `Machine`'s owned service, and failed swaps destroy the passed replacement object while leaving `Machine::audioService()` unchanged.
- **Risk:** Expanded `MachineView` surface.
  - **Mitigation:** Keep the API minimal and focused on `AudioService`.

## Follow-ups

- Evaluate a host-level audio graph after additional frontends exist.
- Consider exposing additional shared audio metrics if multiple backends rely on them.
