# Project T.I.M.E

T.I.M.E (The Infinite Modder's Emulator) is an emulator framework prototype focused on:

- Machine-owned execution with a native host runtime
- Declarative-ish instruction flow (`fetch -> decode -> execute`)
- Memory/register snapshotting for traceability
- Executor-driven orchestration
- Plugin-oriented extension points for core runtimes and executor policies

## Current Status

This repository is still pre-alpha and intentionally incomplete in several areas, but it now has:

- A minimal Game Boy reference machine shell
- A minimal runnable instruction-cycle slice
- CPU feedback hooks from core to executor
- A plugin contract layer
- Smoke tests covering snapshots, instruction cycle, machine-owned execution, and plugin executor flow

## Build

```bash
cmake -S . -B build-working
cmake --build build-working -j4
```

This build now produces both the host executable `timeEmulator` and the runtime-loaded SDL frontend shared object `libtime-sdl-frontend-plugin.so`.

`timeEmulator` will auto-load that shared object from the executable directory by default. Use `--plugin <path>` to override the plugin path or `--headless` to skip frontend loading entirely.

Run tests:

```bash
ctest --test-dir build-working --output-on-failure
```

## Runtime Diagnostics + Perf Baseline (Phase 42)

`timeEmulator` supports periodic runtime diagnostics snapshots without changing
emulation behavior:

```bash
timeEmulator \
	--core gameboy \
	--rom path/to/rom.gb \
	--timing-profile balanced \
	--diagnostics-report diagnostics-balanced.jsonl \
	--diagnostics-interval-ms 1000
```

Timing profile examples:

```bash
# balanced
timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile balanced \
	--diagnostics-report diagnostics-balanced.jsonl --diagnostics-interval-ms 1000

# low_latency
timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile low_latency \
	--diagnostics-report diagnostics-low-latency.jsonl --diagnostics-interval-ms 1000

# deterministic_test
timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile deterministic_test \
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

`GameBoyMachine` is the first reference machine shell. It owns the CPU plugin instance and exposes a `RuntimeContext` that executors run against.

For lab-style experiments, `GameBoyMachine` also provides `loadBootRom(...)`, which accepts a user-supplied Game Boy boot ROM that must be exactly `256` bytes long.

`Machine` is the host-facing contract for:

- `loadRom(...)`
- `step()`
- `guarantee()`
- `readRegisterPair(...)`
- `runtimeContext()`

Relevant files:

- `cores/gameboy/GameBoyMachine.hpp`
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
- `PluginDescriptorV1` C-entrypoint descriptor for future dynamic loading
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

### 5. Game Boy Core Adapter

`LR3592_DMG` implements the CPU contract and produces `CpuFeedback`.

Plugin runtime adapter:

- `cores/gameboy/gameboy_plugin_runtime.hpp`

This wraps `LR3592_DMG` into `ICpuCoreRuntime`, while `GameBoyMachine` hosts the runtime and ROM-backed memory path.

### 6. SDL Frontend Plugin

The SDL frontend is no longer compiled directly into the emulator executable. The host uses:

- `machine/plugins/SdlFrontendPlugin.hpp` for the shared frontend interface and factory ABI
- `machine/plugins/SdlFrontendPluginLoader.hpp`
- `machine/plugins/SdlFrontendPluginLoader.cpp`

The plugin implementation lives in:

- `machine/plugins/sdl_frontend/SdlFrontendPlugin.cpp`

At runtime the emulator loads `libtime-sdl-frontend-plugin.so` with `dlopen`, creates an `ISdlFrontendPlugin` instance through the exported factory table, and registers that instance with `PluginManager` like any other host-side I/O plugin. If loading fails, the emulator logs a warning and continues in headless mode.

## Tests

Current smoke tests:

- `tests/smoke_snapshot.cpp`
- `tests/smoke_register_snapshot.cpp`
- `tests/smoke_instruction_cycle.cpp`
- `tests/smoke_machine_boot.cpp`
- `tests/smoke_executor.cpp`
- `tests/smoke_plugin_executor.cpp`
- `tests/smoke_plugin_abi.cpp`
- `tests/smoke_plugin_io.cpp`
- `tests/smoke_sdl_frontend_plugin.cpp`
- `tests/smoke_apu_audio.cpp`
- `tests/smoke_trace_executor.cpp`

These verify:

- Snapshot memory read-through and overlay behavior
- Register snapshot copy/isolation behavior
- Minimal direct CPU `fetch -> decode -> execute` behavior
- Machine-owned ROM-backed execution
- Executor recording/script replay through `RuntimeContext`
- Plugin executor orchestration and feedback-driven policy behavior
- Plugin ABI compatibility, metadata validation, and guarantee labeling
- Host-side I/O plugin lifecycle and failure handling
- Runtime loading of the SDL frontend shared object without losing video, audio, input, or diagnostics behavior
- Baseline-vs-optimized visible-state equivalence for the Game Boy core (`smoke_trace_executor`)
- Game Boy hardware-sensitive behavior such as `STOP` wake-on-input and `LY` / `STAT` write semantics

## Short-Term Direction

The next practical expansion points are:

- Extend the machine host beyond the initial Game Boy shell
- Grow `RuntimeContext` from a narrow baseline API into a capability-based boundary
- Add more opcode coverage while preserving executor/plugin interfaces
