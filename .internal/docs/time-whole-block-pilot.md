# Whole-block emission pilot, version 1

Status: implemented research prototype, design review pending. This pilot does
not close `acceleration.native` or admit another family.

The existing IR ABI executes one indexed instruction. This experiment adds a
separate internal C++ research contract in `inst_cycle/research/WholeBlock.hpp`.
A build-time emitter copies validated IR into numeric C++ expressions and emits
one function containing sequential instruction bodies. The selected compiler
produces useful arithmetic, loads, stores, and branches for the entire block;
execution performs no opcode dispatch and never returns to an interpreter
between instructions. Canonical helpers and host accesses remain calls through
`InterpreterHost`. The current external indexed ABI and production policies are
unchanged; baseline remains the default.

Compilation and metadata allocation happen during the build and artifact
construction, outside machine execution. Metadata is copied into owned immutable
storage. Code lives in the executable's RX ELF text; no runtime writable code
mapping is created. This is an ahead-of-time emission experiment, not a dynamic
compiler, and compile/invalidation cost measurements remain necessary for JIT.
Cross builds use the selected ARM64 compiler; QEMU only executes the generator
and tests. The generated source is retained in the build tree for inspection.

An invocation checks its bounded instruction/cycle budgets before entering each
instruction, then calls the owner's `before` hook. That hook must check PC,
code bytes, mapping generation, execution state, helper ABI, and lifecycle
binding. After executing an instruction, the emitted function calls `retire`
exactly once. The owner advances hardware and publishes ordered effects before
the next guard. Observer stops, control transfers, interrupts, and budgets end
execution at that boundary. Cycle budgets are soft per-instruction limits.
Exceptions propagate without retry; a future machine owner must fault a session
that fails after mutation. There is no implicit interpreter fallback.

The first artifact set uses current Game Boy and Game Gear lowering for register
compute and RAM-access blocks, plus a shared conditional-transfer fixture.
Emission is limited to 16 instructions, 48 guest bytes, 16 bounded guards,
64 operations and 64 values per instruction, and 8 operands per operation.
Shifts and signed comparisons are explicitly rejected in this pilot. Expand the
emitted operation set only after the design review passes. Guest bytes never
become source text; caller identifiers are validated and operands are emitted
as numeric constants. Compiled entry/metadata pairing is trusted build output,
not an untrusted module-loading boundary or a stable external ABI.

`smoke-research-whole-block` differentially compares the generated functions with
portable IR after every instruction for 256 initial byte values across all five
artifacts. It checks registers, memory, ordered host calls, cycles, and results;
zero/soft budgets; code/mapping/lifecycle/state/helper/PC rejection; observer
stops; publication exceptions; and executable RX protections. The host model is
an IR-interface model. Its helper callbacks do **not** implement full LR35902
or Z80 semantics and its retirement hooks do **not** advance actual devices.
These tests are not the required real-core CPU/device differential evidence.

`time-smoke-research-whole-block --measure` reports nine interleaved raw samples
for 10,000 warmed whole-block invocations, using identical model hooks in both
portable and emitted paths. This isolates dispatch/emission costs; it excludes
real hardware, presentation, audio, and game corpus behavior. It cannot justify a
go or a reviewed no-go. Native ARM64 timing is still required independently of
QEMU throughput.

The next integration must bind artifacts to each real machine's ROM and
execution generation, reuse authoritative CPU helpers and device retirement,
preserve per-instruction feedback/effect order, validate RAM/mapping/lifecycle
invalidation, and compare every instruction against baseline before measuring
the representative corpus against each core/host's fastest validated backend.
No machine execution mode is enabled by this prototype.

Use `python3 tools/measure_acceleration.py --build build-working --experiment
whole-block-model --output build-working/completion-validation/whole-block-pilot/
native-model.json` to bind model samples to the source, executable, compiler,
effective cache, and host details. The compatibility default remains the real
core baseline corpus. ARM64 uses the cross-build directory and the same explicit
QEMU launcher as the baseline harness; model timings remain preliminary, and
QEMU timings remain emulated throughput.
