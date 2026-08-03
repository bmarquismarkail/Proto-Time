# Proto-Time Phase 12: Multi-Core IR/IL

## Purpose

Phase 12 turns the Phase 11 Game Boy intermediate representation into a
versioned, multi-core execution boundary. Game Boy remains the regression
reference and Game Gear supplies the second architecture proof. The feature is
correctness-first: the existing Game Boy x86-64 native backend remains a frozen
experiment and is not broadened or ported.

The emulation lane remains the sole writer of guest CPU, memory, interrupt,
DMA, PPU, APU, and timing state. A backend returns control after exactly one
guest instruction retirement so machine hooks, host observers, and budget
checks keep their existing order.

## C++ boundary

`BMMQ::IR::IIrCoreAdapter` identifies a guest architecture and IR ABI, lowers a
bounded span of copied guest instruction bytes, performs architecture-specific
block validation, and checks current execution state before dispatch.

`BMMQ::IR::IIrExecutionBackend` declares whether it supports an architecture
and IR ABI, compiles a validated block into an immutable artifact, and executes
one indexed instruction through `InterpreterHost`.

`BMMQ::IR::IrExecutionService` owns no guest state. It enforces these steps in
order:

1. adapter IR ABI and backend compatibility;
2. host size limits and architecture-neutral `BMMQ::IR::validate`;
3. adapter validation and execution-state guard;
4. backend compilation;
5. a fresh execution-state check before each backend dispatch; and
6. successful backend return with an observed retirement marker.

Any rejection through step 5 is a safe interpreter fallback because no backend
guest mutation has started. Once step 6 begins, a backend error, exception, or
missing retirement is fatal and is never retried through another execution
path.

Host limits are 16 guest instructions, 48 guest bytes, 64 operations per
instruction, 8 operands per operation, 64 instruction-local values, and one
million retirement cycles per instruction.

## Core adapters

The built-in Game Boy adapter preserves the current Phase 11 subset and guards:
mapping generation, exact code bytes, helper ABI, and execution state. Existing
portable and frozen native modes keep their current observable behavior.

The built-in Game Gear adapter begins with a conservative Z80 subset: NOP,
8-bit register and immediate loads, INC/DEC, 8-bit ALU operations, and
unconditional relative jump. HALT, interrupt entry, deferred interrupt-enable
state, mapping changes, helper-ABI changes, and code-byte changes reject entry
or continuation before another guest instruction executes. Unsupported
instructions use the canonical interpreter.

## Dynamic module ABI v1

The existing `time_get_plugin_module_v1` module table gains optional IR core
adapter and IR execution backend plugin kinds. Compatibility requirements are:

- pure C tables and fixed-width integer fields;
- `struct_size` and ABI-version checks before reading optional fields;
- no C++ objects, STL types, exceptions, RTTI, or allocator ownership crossing
  the module boundary;
- host-owned lowering builders and read-only validated block views;
- explicit plugin shared-library path plus plugin ID selection;
- architecture ID and IR ABI agreement before compile;
- immutable backend artifacts released by their creating module; and
- the loaded module outliving every interface and artifact obtained from it.

Dynamic plugins are trusted native process extensions. Guest bytes and every
IR field they produce remain untrusted input and receive the same host bounds
and validation as built-ins.

The command line selects dynamic components independently with
`--ir-adapter-plugin`, `--ir-adapter-id`, `--ir-backend-plugin`, and
`--ir-backend-id`. Omitting all four uses built-ins. A path without an ID, an ID
without a path, an unknown ID, an incompatible pair, or a malformed table is a
configuration error rather than a silent substitution.

## Verification

Required smoke coverage includes shared service ordering/failure semantics,
Game Boy canonical/portable parity, Game Gear canonical/IR differential runs,
guard and invalidation behavior, dynamic adapter/backend fixtures, malformed
IR and incompatible ABI rejection, explicit CLI selection, and module/artifact
lifetime. The full CTest suite and `git diff --check` must pass.

Game Gear performance coverage records dispatch coverage, guard rejects,
fallbacks, and elapsed execution time. Phase 12 has no speedup gate; a measured
regression is reported and used to guide later backend work rather than hidden
by broadening the frozen native experiment.
