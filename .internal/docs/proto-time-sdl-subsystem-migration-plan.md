# Proto-Time SDL Subsystem Migration Plan

## Status

Implemented on 2026-07-15. The coupling analysis below records the pre-migration
state; the phase and completion criteria define the implemented architecture.

Implementation verification:

- `timeEmulator` has no SDL symbols or SDL `DT_NEEDED` entry
- SDL frontend and SDL audio are independent shared modules
- the pure-C audio-output ABI is covered by valid, malformed, invalid-format,
  and callback-after-close fixtures
- the normal build passes 120 CTest tests
- the SDL-disabled build passes 114 applicable CTest tests
- the Game Boy runtime matrix passes with SDL, GLFW, and headless frontends and
  SDL, dummy, file, and no audio outputs

## Objective

Make SDL an optional collection of loadable host adapters rather than a link
dependency of `timeEmulator`, the generic plugin loader, or machine/runtime
libraries.

The completed migration must support all of these combinations without source
changes:

- SDL frontend plus SDL audio
- GLFW frontend plus SDL audio
- SDL frontend plus file or dummy audio
- headless execution plus file, dummy, or no audio
- a core-only build in an environment where SDL headers and libraries are not
  installed

This is a build and ownership migration. It must not move guest state out of
the emulation lane, merge audio work into the UI lane, or weaken lifecycle
barriers.

## Current Coupling

### Link Coupling

The main executable currently reaches SDL through this path:

```text
timeEmulator
  -> time-frontend-loader
  -> time-dynamic-plugin-loader
  -> time-sdl-audio-output
  -> SDL2
```

As a result, `ldd build-working/timeEmulator` reports `libSDL2-2.0.so.0` even
though the selected window frontend is loaded from a shared module.

`time-dynamic-plugin-loader` also links the dummy and file audio backends. The
loader therefore owns backend selection that belongs in the audio-output
subsystem rather than generic module discovery.

### Contract Coupling

The host-internal generic frontend contract still lives in
`machine/plugins/SdlFrontendPlugin.hpp`. Generic types are SDL-prefixed and
then aliased:

- `SdlFrontendConfig` -> `FrontendConfig`
- `SdlFrontendStats` -> `FrontendStats`
- `ISdlFrontendPlugin` -> `IFrontendPlugin`

The same header also combines frontend state, video diagnostics, audio
transport diagnostics, input state, test-only retained snapshots, and
synthetic host-event types. `emulator.cpp` consumes `SdlFrontendStats`
directly when writing diagnostics.

### Ownership Coupling

`CFrontendAdapter` in `DynamicPluginModule.cpp` currently owns all of the
following:

- pure-C frontend instance lifecycle
- video mailbox presentation bridge
- SDL-independent input publication
- audio packet batching
- selection and lifetime of SDL, file, or dummy audio output backends
- aggregation of frontend, video, audio, input, and timing statistics

The frontend module itself does not own SDL audio, but the generic adapter
does, which is why extracting the frontend shared object did not remove SDL
from the executable.

### Duplicate SDL Presentation Paths

There are three SDL presentation implementations:

- `machine/plugins/sdl_frontend/SdlFrontendModule.cpp`, the active pure-C
  frontend module
- `machine/plugins/video/adapters/SdlVideoPresenter.cpp`
- `machine/plugins/video/adapters/HardwareVideoPresenter.cpp`

The latter two are not used by the production host. They are linked only into
`time-smoke-video-transport`, but they keep duplicate SDL lifecycle, window,
renderer, texture, fallback, and diagnostics behavior in the build.

## Target Architecture

```text
timeEmulator
  -> time-emulator-host
  -> time-host-module-loader
  -> time-machine-runtime-common
  -> dl

runtime-loaded modules
  libtime-sdl-frontend-plugin.so
    SDL video + window + event pump + keyboard/controller translation

  libtime-sdl-audio-output-plugin.so
    SDL audio device lifecycle + drain-only callback

host-owned services
  VideoService     latest-frame mailbox, epochs, presentation diagnostics
  AudioService     resampling, prepared audio, reset barriers, transport stats
  InputService     committed logical input state and generation handling
  TimingService    pacing and control actions
```

The SDL frontend and SDL audio output are separate modules. Either may be used
without the other.

The SDL window, renderer, event pump, and window-derived input remain in one
frontend module during this migration. SDL exposes one process-wide event
queue, and window/event APIs are host-main-thread-affine. Splitting those into
independently loaded modules would require a new event broker and would add a
cross-module ordering problem without removing build coupling. Source-level
classes inside the module may still be separated for testability.

## Architectural Decisions

### 1. Add an Audio-Output Module Contract

Extend the pure-C module protocol with an audio-output plugin kind and a
versioned `TimeAudioOutputApiV1`. Do not expose `AudioEngine`, `AudioService`,
STL types, or C++ object layout across the module boundary.

The module owns:

- host audio API initialization
- device open, pause/start, service, and close
- obtained device format reporting
- the backend callback registered with SDL
- backend-specific errors and device diagnostics

The host owns:

- source PCM ingestion
- stateful resampling and mixing
- prepared device-rate audio storage
- generation/reset barriers
- underrun accounting and deterministic silence fill

The audio callback calls a host function pointer that fills a caller-provided
fixed-capacity buffer. The callback must not allocate, log, lock a blocking
mutex, perform device reconfiguration, or call guest/machine code.

Proposed contract shape:

```c
enum TimePluginKindV1 {
    /* existing kinds */
    TIME_PLUGIN_KIND_AUDIO_OUTPUT_V1 = 3u
};

struct TimeAudioOutputHostApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* host_context;
    uint32_t (*drain_ready_audio)(void* host_context,
                                  int16_t* output,
                                  uint32_t requested_samples);
};

struct TimeAudioOutputConfigV1 {
    uint32_t struct_size;
    uint32_t requested_sample_rate;
    uint32_t requested_channels;
    uint32_t callback_samples;
};

struct TimeAudioOutputDeviceInfoV1 {
    uint32_t struct_size;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t callback_samples;
};

struct TimeAudioOutputApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* (*create)(const struct TimeAudioOutputHostApiV1* host_api);
    void (*destroy)(void* instance);
    int32_t (*open)(void* instance,
                    const struct TimeAudioOutputConfigV1* config,
                    struct TimeAudioOutputDeviceInfoV1* obtained);
    int32_t (*start)(void* instance);
    void (*pause)(void* instance);
    int32_t (*service)(void* instance);
    void (*close)(void* instance);
    const char* (*backend_name)(const void* instance);
    const char* (*last_error)(const void* instance);
    int32_t (*query_stats)(const void* instance,
                           struct TimeAudioOutputStatsV1* stats);
};
```

Exact field names may change during implementation, but the ownership and
callback direction are fixed. `open` returns the obtained device format before
`start`, allowing the host to configure device-rate preparation while the
backend is paused.

Adding a plugin kind and new tables is additive because existing V1 structure
layouts do not change. An older host may reject the new kind cleanly. If ABI
validation work shows that existing V1 rules forbid new kinds, introduce a V2
module entrypoint rather than weakening exact-version checks.

### 2. Keep Audio Selection Outside the Frontend Adapter

The host creates frontend and audio-output instances independently. The
frontend adapter no longer contains `makeAudioOutput()`, `openAudioOutput()`,
or `std::unique_ptr<IAudioOutputBackend>`.

CLI compatibility remains:

- `--audio-backend sdl` resolves the default SDL audio module
- `--audio-backend dummy` selects the built-in dummy adapter initially
- `--audio-backend file` selects the built-in file adapter initially
- `--no-audio` creates no audio-output instance

Add `--audio-plugin <path>` for explicit module selection. A later cleanup may
move dummy and file output behind the same C ABI after the SDL path is stable;
that is not required to remove SDL from the executable.

### 3. Make the C++ Frontend Contract Generic

Create `machine/plugins/FrontendPlugin.hpp` containing `FrontendConfig`,
`FrontendStats`, `FrontendHostEvent`, `FrontendHostKey`, retained-frame helper
types, and `IFrontendPlugin`.

Move subsystem diagnostics to their owning services:

- frontend/window/present callback fields remain in `FrontendStats`
- video queue, frame age, fallback, and presenter fields come from
  `VideoServiceDiagnostics`
- audio queue, resampler, worker, callback, and underrun fields come from
  `AudioService` and the selected audio-output adapter
- input publication and fallback fields come from `InputServiceDiagnostics`
- timing fields continue to come from `TimingServiceStats`

`emulator.cpp` should assemble the diagnostics document from those generic
sources. It must not accept `SdlFrontendStats*`.

Keep `SdlFrontendPlugin.hpp` as a temporary forwarding compatibility header
with deprecated aliases. Delete it after in-tree callers and documented
embedding examples have migrated.

### 4. Split the Generic Dynamic Loader by Adapter Role

Retain one shared-library state object and one descriptor-validation path, but
split `DynamicPluginModule.cpp` into role-focused adapters:

```text
machine/plugins/module/
    DynamicModuleState.cpp
    ModuleDescriptorValidation.cpp
    CExecutorPolicyAdapter.cpp
    CFrontendAdapter.cpp
    CAudioOutputAdapter.cpp
```

The generic loader target links `dl` and generic runtime contracts only. It
must not include SDL headers, construct an SDL class, or link any backend
implementation.

### 5. Retire Duplicate SDL Video Adapters

Delete `SdlVideoPresenter` and `HardwareVideoPresenter` after their generic
transport and fallback assertions have been moved to backend-neutral tests or
the pure-C SDL frontend module tests.

Do not create a fourth SDL presenter abstraction as an intermediate step. The
production presentation path is the pure-C frontend module.

## Migration Phases

### Phase 0: Freeze Baselines and Add Build-Coupling Gates

Deliverables:

- record the current target graph and runtime dependency list
- add a CMake option for an SDL-free build, initially allowed to fail until
  Phase 4, so the intended boundary is explicit
- add a Linux CI/CTest dependency check that fails if `timeEmulator` has an
  SDL `DT_NEEDED` entry
- record runtime KPI baselines for SDL frontend plus SDL audio

Required baseline metrics:

- effective emulation speed
- audio callback p99/p999 duration
- ready queue depth and underruns
- video publish-to-present p99/high-water age
- presenter p99/p999 duration
- input-to-frame response p99

Exit gate: the current coupling is captured by an expected-failure dependency
test and the runtime baseline is reproducible.

### Phase 1: Genericize Frontend Types and Diagnostics

Deliverables:

- add `FrontendPlugin.hpp`
- rename generic C++ types and constants without changing the pure-C frontend
  ABI
- split diagnostics assembly by service ownership
- update `FrontendPluginLoader` and `DynamicPluginModule` public signatures to
  use generic names
- retain source aliases in `SdlFrontendPlugin.hpp`
- migrate in-tree tests away from SDL-prefixed generic types

Exit gates:

- SDL and GLFW frontend tests use the same generic host contract
- `emulator.cpp` contains no `SdlFrontendStats`, `SdlFrontendConfig`, or
  SDL-specific lifecycle comment
- existing frontend module ABI fixture still loads unchanged
- diagnostics JSON schema is preserved unless a separately reviewed schema
  version is introduced

### Phase 2: Introduce the Audio-Output Module ABI

Deliverables:

- add audio-output descriptor validation and malformed-module coverage
- add `CAudioOutputAdapter` implementing the host-internal
  `IAudioOutputBackend`
- add a pure-C test audio-output module compiled as C
- implement open-paused, obtained-format, configure, and start sequencing
- preserve drain-only callback behavior and generation barriers
- add module lifetime tests proving the shared library remains loaded until the
  backend instance and callback are fully closed

Exit gates:

- a C fixture drains prepared audio without SDL or C++ ABI exposure
- malformed sizes, missing callbacks, invalid formats, and callback-after-close
  are rejected deterministically
- callback tests show no allocation, blocking work, logging, or reset/config
  calls on the callback lane
- TSAN coverage passes for open/start/pause/close and lifecycle epoch changes

### Phase 3: Move SDL Audio Into Its Own Shared Module

Deliverables:

- create `libtime-sdl-audio-output-plugin.so`
- move `SdlAudioOutput.cpp/.hpp` under the SDL audio module directory
- make the SDL callback call only `drain_ready_audio`
- remove SDL construction and audio backend selection from `CFrontendAdapter`
- resolve the default SDL audio module next to the executable, matching the
  existing frontend module path policy
- add `--audio-plugin <path>` and configuration-file support

Exit gates:

- SDL frontend plus file/dummy audio runs without loading the SDL audio module
- GLFW frontend plus SDL audio works
- headless plus SDL audio works when explicitly selected
- SDL audio open failure degrades according to host policy without taking down
  video/input
- audio KPI gates meet or improve the Phase 0 baseline

### Phase 4: Remove SDL From the Host Link Graph

Deliverables:

- remove `time-sdl-audio-output` from
  `time-dynamic-plugin-loader` link libraries
- move `find_package(SDL2)` and SDL compile definitions into SDL module-only
  CMake branches
- add independent options such as:
  - `PROTO_TIME_BUILD_SDL_FRONTEND`
  - `PROTO_TIME_BUILD_SDL_AUDIO`
  - `PROTO_TIME_BUILD_GLFW_FRONTEND`
- ensure disabling both SDL options does not evaluate SDL include/library
  variables or compile SDL source files
- make install/package rules place optional modules beside the executable or in
  a configured module directory

Exit gates:

- `timeEmulator` has no SDL symbols and no SDL `DT_NEEDED` entry
- `cmake -S . -B build-no-sdl -DPROTO_TIME_BUILD_SDL_FRONTEND=OFF
  -DPROTO_TIME_BUILD_SDL_AUDIO=OFF` configures, builds, and runs the core smoke
  suite on a host without SDL development files
- building only `timeEmulator` does not build an SDL target
- SDL frontend and SDL audio can be rebuilt independently without relinking
  `timeEmulator`

### Phase 5: Remove Duplicate Presenters and SDL Compatibility Surfaces

Deliverables:

- move backend-neutral assertions out of `smoke_video_transport`
- delete `SdlVideoPresenter` and `HardwareVideoPresenter`
- remove `time-sdl-video-adapter` and `time-hardware-video-adapter`
- delete `time-sdl-frontend-loader` after all callers use
  `time-frontend-loader`
- delete deprecated SDL-prefixed generic aliases after one compatibility window
- rename remaining generic tests so SDL appears only when the test loads an SDL
  module or uses an SDL device

Exit gates:

- `rg "SDL|Sdl" emulator machine` returns only SDL module implementation,
  module discovery defaults, compatibility documentation, and intentionally
  SDL-specific tests
- no production target contains two implementations of SDL presentation
- the full frontend/audio matrix passes

### Phase 6: Internal SDL Frontend Decomposition

This phase improves maintainability inside the already isolated SDL frontend
module. It is not required for host link decoupling and must come last.

Suggested private components:

- `SdlRuntime`: balanced subsystem init/quit ownership
- `SdlWindowPresenter`: window, renderer, texture, upload, present, visibility
- `SdlEventSource`: main-thread event pumping and normalized input/control
  publication

Keep these in one shared module so SDL event and window ownership remains on
one UI lane.

Exit gates:

- each private component has focused lifecycle/error tests
- no component calls machine APIs or retains frame pointers
- the module still presents and pumps events only from the host UI thread

## Test Matrix

Every phase that changes runtime composition must run this matrix:

| Frontend | Audio output | Expected result |
| --- | --- | --- |
| SDL | SDL module | normal interactive runtime |
| SDL | dummy | video/input with deterministic dummy drain |
| SDL | file | video/input with captured audio |
| GLFW | SDL module | GLFW video/input with SDL device audio |
| GLFW | dummy | no SDL loaded at runtime |
| headless | file | deterministic offline capture |
| headless | none | no frontend or audio module loaded |

Additional required checks:

- missing optional module
- malformed module descriptor and API table
- audio device open failure
- frontend initialization failure while audio remains available, and vice versa
- reset, ROM load, and save-state restore while output is active
- repeated attach/detach and process shutdown ordering
- stale lifecycle epoch rejection
- no callback after module unload

## Performance and Concurrency Gates

The migration is complete only if modularity does not regress deadline health.

Audio gates:

- callback p99 and p999 do not regress beyond the documented tolerance
- callback remains drain-only
- no additional underruns or silence fill at sustained `1.0x`
- ready queue depth and worker wake latency remain bounded

Video gates:

- latest-frame mailbox depth remains one or two
- publish-to-present age does not regress beyond the documented tolerance
- no emulation-thread wait on window, texture, upload, or present calls

Input gates:

- SDL events publish complete logical snapshots
- machine-visible input changes only at the existing deterministic sampling
  boundary
- input-to-frame response p99 remains within the concurrency success gate

Lifecycle gates:

- UI/render work stays on the host main thread
- guest CPU/APU/PPU state remains emulation-thread-owned
- audio reset/configuration occurs only while the backend is paused or closed
- module unload occurs only after callbacks, service references, and adapters
  are destroyed

## Risks and Mitigations

### Process-Global SDL State

The frontend and audio modules load separate adapters but share SDL's
process-global subsystem bookkeeping.

Mitigation:

- the frontend module initializes and quits only `SDL_INIT_VIDEO` and
  `SDL_INIT_EVENTS`
- the audio module initializes and quits only `SDL_INIT_AUDIO`
- neither module calls global `SDL_Quit`
- repeated open/close tests cover both module destruction orders

### Callback During Module Unload

An SDL audio callback executing after `dlclose` is a process crash, not a
recoverable backend error.

Mitigation:

- pause the device
- wait for SDL's close operation to retire callbacks
- clear the host callback context
- destroy the module instance
- release the shared-library state last

Encode and test this order in `CAudioOutputAdapter`; do not rely on callers to
repeat it correctly.

### Obtained Device Format Race

Audio preparation cannot target the obtained device rate until SDL has opened
the device, but SDL must not start draining before host configuration is
complete.

Mitigation: require the `open-paused -> report obtained format -> configure
AudioService -> start` sequence in the ABI and adapter state machine. Reject
start before successful host configuration.

### Diagnostics Drift

Moving counters out of `SdlFrontendStats` can accidentally change field
meaning, reset behavior, or cumulative scope.

Mitigation: add golden diagnostics JSON tests before moving fields, preserve
the current schema through the migration, and review any schema version change
separately.

### Optional Module Packaging

A build can succeed but produce a runtime that silently falls back to headless
or no audio because a default module was not installed.

Mitigation: install a manifest or deterministic module directory alongside the
host, print the resolved frontend and audio module paths in startup
diagnostics, and distinguish `not installed`, `failed to load`, and `failed to
initialize` errors. A missing SDL audio module must never reactivate the
temporary in-process SDL backend.

### Timing-Test Noise

Moving code across shared-library boundaries can expose existing flaky timing
gates without changing actual tail latency.

Mitigation: preserve raw counters and histograms, run KPI tests on an otherwise
idle host, and investigate repeated failures rather than weakening thresholds
as part of the migration.

## Compatibility and Rollout

Keep these user-facing behaviors during the migration:

- `--frontend sdl`, `--frontend glfw`, and `--frontend headless`
- `--audio-backend sdl`, `dummy`, and `file`
- current default module filenames
- current input bindings and control actions
- current diagnostics JSON field meanings

Land phases as independently revertible commits. Do not combine ABI
introduction, SDL backend movement, legacy deletion, and diagnostics schema
changes in one patch.

Recommended commit sequence:

1. generic frontend names and service-owned diagnostics
2. audio-output ABI plus C fixture
3. generic dynamic audio adapter
4. SDL audio shared module
5. host/CMake link decoupling
6. duplicate presenter removal
7. compatibility alias removal
8. optional SDL module internal decomposition

During Phases 2-4, retain the existing in-process SDL audio backend behind a
temporary build option for one comparison window. Remove it immediately after
the module path passes the full matrix and KPI gates; it must not become a
permanent fallback that preserves the coupling.

## Completion Criteria

The migration is complete when all of the following are true:

- `timeEmulator` and generic host libraries do not link SDL
- SDL frontend and SDL audio are separately loadable and independently
  selectable
- a no-SDL machine/runtime build configures and passes its applicable tests
- generic contracts, diagnostics, and loader code contain no SDL-owned types
- duplicate SDL presenters and the SDL-only loader facade are removed
- audio callback, video mailbox, input sampling, and lifecycle epoch rules are
  unchanged or stronger
- the runtime composition matrix and full CTest suite pass
- sustained ROM diagnostics remain at realtime with no new audio underruns,
  video latency growth, or input response regression

## Deferred Work

The following are intentionally outside this migration:

- a standalone SDL input module separate from the SDL window/event module
- dynamic machine-core ABI design
- renderer APIs beyond the current ARGB8888 frontend frame contract
- controller remapping UI
- replacing SDL2 with SDL3
- moving file and dummy audio to shared modules before the SDL audio module is
  proven stable
