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

Run tests:

```bash
ctest --test-dir build-working --output-on-failure
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

Relevant file:

- `CPU.hpp`

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

## Tests

Current smoke tests:

- `tests/smoke_snapshot.cpp`
- `tests/smoke_register_snapshot.cpp`
- `tests/smoke_instruction_cycle.cpp`
- `tests/smoke_machine_boot.cpp`
- `tests/smoke_executor.cpp`
- `tests/smoke_plugin_executor.cpp`
- `tests/smoke_plugin_abi.cpp`

These verify:

- Snapshot memory read-through and overlay behavior
- Register snapshot copy/isolation behavior
- Minimal direct CPU `fetch -> decode -> execute` behavior
- Machine-owned ROM-backed execution
- Executor recording/script replay through `RuntimeContext`
- Plugin executor orchestration and feedback-driven policy behavior
- Plugin ABI compatibility, metadata validation, and guarantee labeling

## Short-Term Direction

The next practical expansion points are:

- Extend the machine host beyond the initial Game Boy shell
- Grow `RuntimeContext` from a narrow baseline API into a capability-based boundary
- Add more opcode coverage while preserving executor/plugin interfaces
