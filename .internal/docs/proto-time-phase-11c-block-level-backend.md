# Proto-Time Phase 11C: Block-Level Portable Backend

## Scope

Phase 11C adds a block-level entry and continuation path for the portable Game
Boy IR. It amortizes cache dispatch and full code-byte guard validation across a
bounded sequential run without batching guest retirement. It does not generate
native code, allocate executable memory, broaden the lowered opcode subset, or
move guest state off the emulation lane.

## Runtime slice contract

`RuntimeContext::runSlice` exposes three protected backend hooks: slice begin,
one instruction within the slice, and slice end. The default implementation is
still `step()` for every instruction. Backend state is scoped to one call to
`runSlice` and is released on every normal or exceptional exit.

Every backend step returns one `CpuFeedback` before the runtime:

1. updates instruction and cycle progress;
2. synchronously calls the machine retirement hook;
3. synchronously calls the optional host timing observer; and
4. checks retirement, segment, instruction, and cycle exit conditions.

No second guest instruction can begin before those four actions complete.

## Game Boy block cursor

At a portable block entry, the emulation lane:

- performs the direct-PC Phase 10 lookup;
- validates mapping generation, helper ABI, execution state, and all guarded
  code bytes;
- records the cache-provided instruction index; and
- executes exactly that instruction through the versioned portable ABI.

After retirement returns control to the backend, a continuation checks:

- the owning Phase 10 block is still valid;
- mapping generation and helper ABI still match;
- STOP, HALT, DMA restriction, interrupt entry, HALT-bug adjustment, and pending
  cycle-charge state still match the guarded boundary;
- the guest PC equals the next indexed IR instruction; and
- the next instruction remains within the lowered block.

Code bytes are not reread on continuation. Stable executable-memory writes use
the existing overlapping-range invalidation authority, and mapping changes
invalidate the owning block generation. A failed continuation returns to the
ordinary step path before another instruction executes. Transient execution
state rejects continuation without invalidating otherwise reusable code.

The cursor never crosses a control-flow/IR exit, the end of the lowered prefix,
the end of the host slice, an observer stop, a machine boundary, or a budget
boundary. Cache and IR mode changes clear the cursor before they can destroy
cached storage.

## Diagnostics

The block-cache diagnostics add:

- `ir_block_entries`;
- `ir_block_continuations`; and
- `ir_block_continuation_rejects`.

Together with guard checks, execution counts, and elapsed times, these expose
the average number of IR instructions served by each guarded entry and whether
device or memory activity is frequently breaking continuation.

## Verification and measurement

Dedicated coverage verifies:

- one full guard check serves four sequential IR retirements;
- the machine and observer retirement callbacks see each instruction in order;
- an observer write that replaces the next WRAM instruction invalidates the
  block and prevents stale continuation;
- a 512-instruction block-slice run matches canonical register, audio, video,
  PC, cycle, control-flow, and segment-boundary results; and
- generic runtime slice begin/step/end hooks are balanced when a retirement
  observer stops a slice early.

The benchmark now uses the same multi-instruction `RuntimeContext::runSlice`
contract in all modes. A representative five-run development-host result was:

| Mode | Median | Relative to baseline |
|---|---:|---:|
| Baseline | 95.12 ms | 1.00x |
| Phase 10 block | 45.94 ms | 2.07x |
| Portable block IR | 82.92 ms | 1.15x |

Portable coverage remained 66.7%. Across the five IR runs, 211,750 guarded
entries served 635,235 direct continuations: three continuations per entry for
the supported prefix in the fixture. This recovers measurable throughput over
baseline, but remains well below the Phase 10 byte backend.

## Decision

The block-level contract is suitable as the entry boundary for a future backend,
but the portable interpreter is not yet a reason to expand opcode coverage.
The next CPU-acceleration work should measure and reduce interpreter/helper and
always-on timing-instrumentation overhead, or define the separate W^X-safe native
backend design. Opcode expansion should follow only when the selected backend
shows a durable advantage on realistic ROM corpora as well as this synthetic
loop.
