# Current architecture

[Documentation index](README.md) · [Project philosophy](philosophy.md)

Status: current implementation overview, consolidated from the project README.
This is a map of the pre-alpha implementation, not a claim of full hardware
compatibility. Source contracts and tests decide what the checkout supports.

## 1. Native Machine Host

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

## 2. Core CPU Contract

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
- `retiredCycles`
- `executionPath` (`Unknown`, `CanonicalFetchDecodeExecute`,
  `CpuOptimizedFastPath`, `PortableIr`, or `NativeIr`)

Relevant file:

- `machine/CPU.hpp`

## 3. Instruction Data Structures

`fetchBlock` and `fetchBlockData` store fetched instruction bytes with offsets and base address.

Relevant files:

- `inst_cycle/fetch/fetchBlock.hpp`
- `inst_cycle/fetch/templ/fetchBlock.impl.hpp`

`executionBlock` stores executable step functions and the target memory snapshot pointer.

Relevant file:

- `inst_cycle/execute/executionBlock.hpp`

## 4. Executor Layer

### Classic executor

`inst_cycle/executor/Executor.hpp`:

- Runs one step by default through `RuntimeContext`
- Can record fetched blocks
- Can segment blocks
- Can save/load block scripts
- Exposes `recordedBlocks()` and `recordedSegments()`

Segmentation decisions use both:

- `fetchBlock`
- `CpuFeedback`

### Plugin-oriented executor

Plugin contracts:

- `inst_cycle/executor/PluginContract.hpp`

Defines:

- `ICpuCoreRuntime`
- `IExecutorPolicyPlugin`
- `PluginMetadata`
- `AbiVersion` + host ABI constants
- compatibility helpers (`isAbiCompatible`, `validateMetadata`)
- `DefaultStepPolicy`
- `VisibleStatePreservingStepPolicy`
- `PortableIrStepPolicy`
- `NativeExperimentalStepPolicy`

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

These are host-side C++ contracts. `DynamicPluginModule` validates external
pure-C tables and adapts them to the internal executor and IR interfaces; no
C++ object crosses the shared-library boundary.

## 5. Multi-Core IR Boundary

Portable IR is split into three host-side contracts:

- `IIrCoreAdapter` identifies the guest architecture and IR ABI, lowers copied
  guest instruction bytes, and validates core-specific block and execution
  state
- `IIrExecutionBackend` declares architecture/ABI support, compiles a validated
  block into an immutable artifact, and executes one indexed guest instruction
- `IrExecutionService` enforces compatibility, bounded input limits,
  architecture-neutral and core-specific validation, fresh state guards, and
  an observed instruction-retirement marker

The service owns no guest state. A rejection before
`IIrExecutionBackend::execute` begins, including compilation rejection, is a
safe canonical-interpreter fallback. Once `execute` starts, an error,
exception, or missing retirement marker terminates the emulator run and is not
retried through a second execution path. Guest CPU, memory, interrupt, DMA,
video, audio, and timing state therefore remain machine-owned and single-writer
on the emulation lane.

Relevant files:

- `inst_cycle/IrExecutionService.hpp` — service and C++ adapter/backend contracts
- `inst_cycle/IntermediateRepresentation.hpp` — IR data model and shared validation
- `cores/gameboy/GameBoyIrExecution.hpp`
- `cores/gamegear/GameGearIrExecution.hpp`
- `machine/plugins/abi/TimePluginAbi.h`

## 6. Core Adapters

`LR3592_DMG` implements the CPU contract and produces `CpuFeedback`.

Plugin runtime adapter:

- `cores/gameboy/gameboy_plugin_runtime.hpp`

This wraps `LR3592_DMG` into `ICpuCoreRuntime`, while `GameBoyMachine` hosts the
runtime and ROM-backed memory path. Its existing guarded block IR now uses the
shared core-adapter/backend service in portable mode.

`GameGearMachine` hosts its Z80 interpreter, cartridge/mapper, VDP, PSG, input,
BIOS, and memory-map paths behind the same machine and runtime contracts. It
remains a work in progress. Its built-in IR adapter conservatively lowers NOP,
8-bit register and immediate loads, INC/DEC, 8-bit ALU operations, and
unconditional relative jumps. Unsupported or pre-dispatch guard-rejected
instructions fall back to the canonical Z80 interpreter. See
`cores/gamegear/GameGearIrExecution.cpp` for the exact opcode and addressing
matrix.

## 7. Host I/O Plugins

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

By default the emulator loads `libtime-sdl-frontend-plugin.so`, validates its
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

## Deadline domains

| Lane | Owns |
| --- | --- |
| Emulation | Guest CPU, memory, devices, timing budget, instruction retirement, and event emission. |
| Audio worker | Stateful resampling, processing, and preparation of device-rate audio. |
| Device callback | Bounded draining/copying of prepared audio and underrun silence. |
| UI/render (process main thread) | Window lifecycle, host events, upload, and presentation. |
| Background workers | Bounded non-real-time jobs using owned inputs, such as decode, capture writes, and save flushes. |

Immutable handoffs separate these domains. Latest-frame delivery bounds video
age; three ownership slots do not mean a queue of three frames to present.
Lifecycle epochs and quiescence rules prevent stale output across reset, ROM
load, and state restore. These transitions are control-plane work.

Accelerated blocks retain instruction-by-instruction retirement: device
advancement, machine effects, and host observation follow each guest instruction
before another begins. They do not give a worker shared ownership of guest state.

See [success gates](../.internal/docs/concurrency-success-gates.md) for criteria
and dated measurements; those recorded test totals are not a fresh validation
of the current checkout.

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
- Runtime loading and validation of executor, IR core-adapter, IR
  execution-backend, frontend, audio-output, and audio-processor pure-C modules
- Shared IR service ordering and failure semantics, host limits, malformed and
  incompatible module rejection, and module/artifact lifetime
- Game Boy canonical/portable parity plus Game Gear lowering, differential,
  guard, invalidation, fallback, and portable-dispatch behavior
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

## Further contracts

- [Plugin boundaries and ABI decisions](../.internal/docs/proto-time-plugin-architecture.md)
- [PSG processor contract](../.internal/docs/psg-audio-processor-plugin-contract.md)
- [Mod manifests and native hooks](../machine/modding/README.md)
- [Concurrency gates and recorded measurements](../.internal/docs/concurrency-success-gates.md)

## Short-Term Direction

The next practical expansion points are:

- Continue Game Boy and Game Gear compatibility and timing work while
  preserving machine-owned deterministic state
- Measure and reduce audio callback tail latency, FIFO starvation, host pacing
  jitter, render age, and snapshot-copy interference
- Keep slimming immutable realtime packets and strengthening lifecycle/epoch
  barriers before introducing more cross-thread execution
- Evaluate Game Gear IR expansion through differential tests and the measured
  gates in its continuation plan. Game Boy IR opcode expansion and the current
  native backend remain frozen under the Phase 11D/11E decisions. A new
  native/JIT design requires separate review and evidence against the Phase 10
  baseline; stable host deadlines alone do not authorize expansion.
