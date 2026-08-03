# Project T.I.M.E

T.I.M.E (The Infinite Modder's Emulator) is an emulator framework prototype focused on:

- Machine-owned execution with a native host runtime
- Declarative-ish instruction flow (`fetch -> decode -> execute`)
- Memory/register snapshotting for traceability
- Executor-driven orchestration
- Registry-backed machine providers and executor policies
- A versioned pure-C function-table ABI for executor, frontend, audio-output,
  and audio-processor plugins

## Current Status

This repository is still pre-alpha and intentionally incomplete. Since
`c2f961d` (`Add interchangeable GLFW frontend plugin`), the working framework
has expanded substantially:

- Game Boy and work-in-progress Game Gear machines selected through the machine
  registry
- Machine-owned CPU, PPU/VDP, APU/PSG, input, mapper, save-state, and timing
  paths with hardened state and memory validation
- Built-in baseline, block-cache, portable-IR, and experimental native Game Boy
  execution policies, plus dynamically loaded pure-C executor policies
- Interchangeable SDL and GLFW frontends, a headless path, and audio output that
  is selected independently from the window/input/video frontend
- A prepared audio transport, rich PSG voice stems and events, fixed-capacity
  audio-processor plugins, Standard MIDI File export, and optional live ALSA
  MIDI output
- Visual override packs with asynchronous image decoding and capture, optional
  hot reload, and 1x-8x HD texture replacement
- Periodic JSON-lines runtime diagnostics covering timing, video, audio, and
  background-work interference
- Broad CTest smoke, differential, corruption, lifecycle, concurrency, and
  performance coverage

## Build

```bash
cmake -S . -B build-working
cmake --build build-working -j4
```

Zlib is required. SDL2, GLFW 3.3/OpenGL, and ALSA are detected as optional host
dependencies. The corresponding features can also be controlled with
`PROTO_TIME_BUILD_SDL_FRONTEND`, `PROTO_TIME_BUILD_SDL_AUDIO`,
`PROTO_TIME_BUILD_GLFW_FRONTEND`, and `PROTO_TIME_BUILD_ALSA_MIDI`.

The default build produces `timeEmulator` and, when enabled, independent shared
modules for the SDL frontend, GLFW frontend, and SDL audio output:

- `libtime-sdl-frontend-plugin.so`
- `libtime-glfw-frontend-plugin.so`
- `libtime-sdl-audio-output-plugin.so`

The modules report deterministic backend-unavailable errors when their optional
host dependency is unavailable.

`timeEmulator` auto-loads the selected frontend module from the executable
directory by default. Use `--frontend-plugin <path>` to load any compatible
pure-C frontend module, `--frontend <id>` when it exposes multiple frontends, or
`--headless` to skip frontend loading. `--plugin` remains a path alias.

SDL remains the default. Switch window, input, and video presentation to GLFW
with:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb --frontend glfw
```

`--frontend sdl` selects SDL explicitly. Audio output is host-owned and selected
independently with `--audio-backend sdl|dummy|file`; external audio-output
modules use `--audio-plugin <path>`.

Executor policies are a separate extension layer. Built-in policies can be
selected by stable ID, while external modules use the pure-C ABI in
`machine/plugins/abi/TimePluginAbi.h`:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb \
  --executor-plugin path/to/executor-module.so \
  --executor-policy vendor.executor.policy
```

If a module exposes exactly one executor policy, `--executor-policy` may be
omitted. The legacy `--cpu-mode baseline|block|ir|native` options remain aliases
for the built-in policy IDs. Frontends use the same module ABI with a distinct
frontend descriptor and function table.

Audio processors use another descriptor in the same ABI and run in the
fixed-capacity source pipeline before device output:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb \
  --audio-processor-plugin path/to/processor-module.so \
  --audio-processor-config vendor.processor=processor-config.json
```

Processor modules and configuration mappings are repeatable. A processor that
returns an invalid result is permanently disabled for that instance, passes the
original samples through, and exposes its captured ABI error through pipeline
diagnostics.

Run tests:

```bash
(cd build-working && ctest --output-on-failure)
```

Discover the tests registered by the configured build rather than relying on a
hardcoded list:

```bash
(cd build-working && ctest -N)
```

## Runtime Examples

Run either registered machine core:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/game.gb
./build-working/timeEmulator --core gamegear --rom path/to/game.gg
```

Load a visual pack and preserve higher-resolution replacement texels at 4x
output scale:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/game.gb \
  --visual-pack path/to/pack.json --hd-scale 4
```

See `docs/texture-pack/design.md` and
`docs/texture-pack/hd-replacement.md` for the pack and HD sampling contracts.

Translate PSG events to MIDI, or send them to an ALSA sequencer destination
when ALSA MIDI support was built:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/game.gb \
  --midi-file output.mid

./build-working/timeEmulator --core gameboy --rom path/to/game.gb \
  --midi-output 128:0
```

With an active frontend, `F1` writes `quicksave.ptstate` in the current working
directory. Both registered machines implement the machine save-state contract.

## Runtime Diagnostics and Performance Baseline

`timeEmulator` supports periodic runtime diagnostics snapshots without changing
emulation behavior:

```bash
./build-working/timeEmulator \
	--core gameboy \
	--rom path/to/rom.gb \
	--timing-profile balanced \
	--diagnostics-report diagnostics-balanced.jsonl \
	--diagnostics-interval-ms 1000
```

Timing profile examples:

```bash
# balanced
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile balanced \
	--diagnostics-report diagnostics-balanced.jsonl --diagnostics-interval-ms 1000

# low_latency
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile low_latency \
	--diagnostics-report diagnostics-low-latency.jsonl --diagnostics-interval-ms 1000

# deterministic_test
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile deterministic_test \
	--diagnostics-report diagnostics-deterministic.jsonl --diagnostics-interval-ms 1000
```

The diagnostics report is JSON-lines (one object per interval) and includes host
elapsed time, emulated cycles, effective speed, frame submit/present counts,
fresh/fallback presents, mailbox overwrites, publish-to-present age stats,
presenter duration stats, audio underrun/silence counters, audio worker wake
latency stats, frontend tick scheduled/executed/merged, wake jitter buckets,
and active timing profile.

Perf baseline workflow (RelWithDebInfo):

```bash
cmake -S . -B build-working -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-working -j4

perf record -F 999 -g -- ./build-working/timeEmulator \
	--core gameboy \
	--rom path/to/rom.gb \
	--timing-profile balanced \
	--diagnostics-report diagnostics-balanced.jsonl \
	--diagnostics-interval-ms 1000

perf report
```

## Architecture Overview

### 1. Native Machine Host

The machine registry currently provides `GameBoyMachine` and the work-in-progress
`GameGearMachine`. Each owns its guest hardware state and exposes a
`RuntimeContext` that the attached executor policy runs against.

For lab-style experiments, `GameBoyMachine` also provides `loadBootRom(...)`, which accepts a user-supplied Game Boy boot ROM that must be exactly `256` bytes long.

`Machine` is the host-facing contract for:

- `loadRom(...)`
- `step()`
- `save_state(...)` / `load_state(...)`
- `guarantee()`
- `readRegisterPair(...)`
- `runtimeContext()`

Relevant files:

- `cores/gameboy/GameBoyMachine.hpp`
- `cores/gamegear/GameGearMachine.hpp`
- `machine/Machine.hpp`
- `machine/RuntimeContext.hpp`

### 2. Core CPU Contract

`CPU` defines the main execution cycle and feedback channel:

- `fetch() -> fetchBlock`
- `decode(fetchBlock&) -> executionBlock`
- `execute(executionBlock&, fetchBlock&)`
- `getLastFeedback() -> CpuFeedback`

`CpuFeedback` currently exposes:

- `segmentBoundaryHint`
- `isControlFlow`
- `pcBefore`
- `pcAfter`
- `executionPath` (`CanonicalFetchDecodeExecute` vs `CpuOptimizedFastPath`)

Relevant file:

- `machine/CPU.hpp`

### 3. Instruction Data Structures

`fetchBlock` and `fetchBlockData` store fetched instruction bytes with offsets and base address.

Relevant files:

- `inst_cycle/fetch/fetchBlock.hpp`
- `inst_cycle/fetch/templ/fetchBlock.impl.hpp`

`executionBlock` stores executable step functions and the target memory snapshot pointer.

Relevant file:

- `inst_cycle/execute/executionBlock.hpp`

### 4. Executor Layer

#### Classic executor

`inst_cycle/executor/Executor.hpp`:

- Runs one step by default through `RuntimeContext`
- Can record fetched blocks
- Can segment blocks
- Can save/load block scripts
- Exposes `recordedBlocks()` and `recordedSegments()`

Segmentation decisions use both:

- `fetchBlock`
- `CpuFeedback`

#### Plugin-oriented executor

Plugin contracts:

- `inst_cycle/executor/PluginContract.hpp`

Defines:

- `ICpuCoreRuntime`
- `IExecutorPolicyPlugin`
- `PluginMetadata`
- `AbiVersion` + host ABI constants
- compatibility helpers (`isAbiCompatible`, `validateMetadata`)
- `PluginDescriptorV1` C-entrypoint descriptor for dynamic modules
- `DefaultStepPolicy`
- `VisibleStatePreservingStepPolicy`

Execution guarantees are now explicit:

- `BaselineFaithful` — canonical `fetch -> decode -> execute` only
- `VisibleStatePreserving` — may use CPU fast paths but must preserve final visible machine state
- `Experimental` — intentionally looser behavior for advanced policies

Plugin runtime executor:

- `inst_cycle/executor/PluginExecutor.hpp`

Runs a machine-owned runtime context through the same cycle and delegates recording/segmentation behavior to a policy plugin. It now mirrors the classic executor surface for:

- `recordedBlocks()`
- `recordedSegments()`
- save/load block-script playback

### 5. Core Adapters

`LR3592_DMG` implements the CPU contract and produces `CpuFeedback`.

Plugin runtime adapter:

- `cores/gameboy/gameboy_plugin_runtime.hpp`

This wraps `LR3592_DMG` into `ICpuCoreRuntime`, while `GameBoyMachine` hosts the runtime and ROM-backed memory path.

`GameGearMachine` hosts its Z80 interpreter, cartridge/mapper, VDP, PSG, input,
BIOS, and memory-map paths behind the same machine and runtime contracts. It
remains a work in progress.

### 6. Host I/O Plugins

The SDL frontend and SDL audio output are no longer compiled directly into the
emulator executable. The host uses:

- `machine/plugins/abi/TimePluginAbi.h` for the stable pure-C module boundary
- `machine/plugins/DynamicPluginModule.hpp` for validated loading and the host adapter
- `machine/plugins/FrontendPlugin.hpp` for the internal frontend contract
- `machine/plugins/AudioOutput.hpp` for the independent audio-output contract

The implementations live in:

- `machine/plugins/sdl_frontend/SdlFrontendModule.cpp`
- `machine/plugins/sdl_audio_output/SdlAudioOutputModule.cpp`
- `machine/plugins/glfw_frontend/GlfwFrontendModule.cpp`

At runtime the emulator loads `libtime-sdl-frontend-plugin.so`, validates its
`TimeFrontendApiV1` descriptor, wraps it in an `IFrontendPlugin`, and registers
the adapter with `PluginManager`. Video/window/events stay inside the SDL
module; the host owns audio transport, input snapshots, timing controls, and
the video mailbox. The GLFW module uses an OpenGL texture and implements the
same table without a frontend-specific host path. Select it with `--frontend
glfw`, or select any external implementation with `--frontend-plugin` and its
descriptor ID. If loading fails, the emulator logs a warning and continues in
headless mode.

The machine-owned host services keep guest state single-writer while separating
deadline domains: the emulation lane emits immutable audio/video data, the
audio worker prepares device-rate blocks, the backend callback drains prepared
samples, and the main/render lane owns window events and presentation.

PSG-aware audio processors receive mixed PCM plus voice descriptors, voice-major
stems, register-derived events, frame counters, sample positions, and lifecycle
epochs. `PsgMidiPlugin` consumes the same event stream for file or asynchronous
ALSA output.

## Tests

CTest targets are defined authoritatively in `CMakeLists.txt`; use `ctest -N` to
discover the set available for the configured optional dependencies. Coverage
includes:

- Snapshot memory read-through and overlay behavior
- Register snapshot copy/isolation behavior
- Minimal direct CPU `fetch -> decode -> execute` behavior
- Machine-owned ROM-backed execution
- Executor recording/script replay through `RuntimeContext`
- Plugin executor orchestration and feedback-driven policy behavior
- Plugin ABI compatibility, metadata validation, and guarantee labeling
- Host-side I/O plugin lifecycle and failure handling
- Runtime loading and validation of executor, frontend, audio-output, and
  audio-processor pure-C modules
- Independent SDL/GLFW frontend and SDL/dummy/file audio-output behavior
- Game Boy and Game Gear CPU, video, audio, mapper, BIOS/boot, interrupt, input,
  persistence, and save-state paths
- Audio resampling, transport queues, rich PSG stems/events, processor failure
  fallback, MIDI translation, and callback behavior
- Visual manifests, capture, asynchronous decode/reload, HD replacement,
  nearest/linear filtering, palette handling, and mixed replaced/unreplaced
  frames
- Lifecycle epochs, mailbox behavior, background queues, corruption rejection,
  memory bounds, timing, concurrency success gates, and performance baselines
- Baseline-vs-optimized visible-state equivalence for the Game Boy core (`smoke_trace_executor`)
- Game Boy hardware-sensitive behavior such as `STOP` wake-on-input and `LY` / `STAT` write semantics

## Development Hooks

Tracked post-merge and post-rewrite hooks can refresh an existing Graphify
knowledge graph after pulls and rebases. Activate the tracked hook directory in
each checkout:

```bash
git config core.hooksPath .githooks
```

The refresh remains non-fatal, does not bootstrap a missing graph, and checks
that the installed Graphify supports deletion-safe `update --force` first.

## Short-Term Direction

The next practical expansion points are:

- Continue Game Boy and Game Gear compatibility and timing work while
  preserving machine-owned deterministic state
- Measure and reduce audio callback tail latency, FIFO starvation, host pacing
  jitter, render age, and snapshot-copy interference
- Keep slimming immutable realtime packets and strengthening lifecycle/epoch
  barriers before introducing more cross-thread execution
- Mature block-cache and IR coverage through differential testing; keep native
  execution, JIT, and DBT experimental until host deadline domains are stable
