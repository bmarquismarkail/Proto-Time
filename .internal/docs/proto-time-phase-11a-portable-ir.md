# Proto-Time Phase 11A: Portable Guarded IR

## Scope

Phase 11A establishes a correctness-first boundary between the Phase 10
threaded block cache and any future native backend. It does not generate or
execute host machine code, allocate executable memory, change page permissions,
or introduce another guest-state writer.

The emulation lane remains the sole owner of CPU, memory, interrupt, DMA, PPU,
APU, and timing state. A cached IR entry executes exactly one guest instruction
per `RuntimeContext::step()` and retires it immediately before another guest
instruction may begin.

## Versioned execution ABI

`GB::IRExecution::ExecutionAbiV1` is a size- and version-checked table. Its
register identifiers and helper identifiers are stable numeric ABI values, not
host object offsets. The table provides:

- 8- and 16-bit register reads and writes;
- mapped 8-bit memory reads and writes through the normal Game Boy helpers;
- explicit flag/ALU helpers;
- CPU-internal cycle retirement; and
- an execution-boundary state query.

The execution-state bits describe STOP, HALT, DMA fetch restriction, a pending
enabled interrupt while IME is active, HALT-bug PC adjustment, and an outstanding
cycle charge. IR never runs across any of those unsafe states.

## Operation semantics

IR integer results are truncated to their declared width. Game Boy memory
operations are I8 and use the current mapped address space. Register F writes
discard its low nibble. PC changes are explicit: sequential instructions write
the next 16-bit PC and unconditional JR writes its wrapped 16-bit target.

The Phase 11A lowerer supports this conservative subset:

| LR3592 operation | Semantics | Cycles |
|---|---|---:|
| `NOP` | No visible state change except PC | 4 |
| `LD r,r` | Includes `(HL)` mapped reads/writes | 4, or 8 with `(HL)` |
| `LD r,d8` | Includes `LD (HL),d8` | 8, or 12 for `(HL)` |
| `INC r` | Z/H updated, N cleared, C preserved | 4, or 12 for `(HL)` |
| `DEC r` | Z/H updated, N set, C preserved | 4, or 12 for `(HL)` |
| `ADD/ADC/SUB/SBC/AND/XOR/OR/CP A,r` | Exact LR3592 Z/N/H/C behavior; includes `(HL)` | 4, or 8 with `(HL)` |
| `JR r8` | Signed displacement from the following instruction | 12 |

Unsupported instructions terminate the lowered prefix. If the current PC is
outside that prefix, execution falls back to the existing guarded byte fast
path and ultimately to canonical fetch/decode/execute if needed.

## Guards and invalidation

Every lowered block carries all of the following guards:

1. block-cache mapping generation;
2. Game Boy helper ABI version;
3. masked execution-boundary state; and
4. the exact bytes covered by the lowered prefix.

A guard failure invalidates the owning Phase 10 block before fallback. Normal
mapped writes, ROM-bank changes, reset/load, and mode changes retain the Phase
10 invalidation authority. Enabling or disabling portable IR invalidates cached
entries so a block cannot retain the wrong representation policy.

## Retirement and host pacing

`Machine::runSlice` composes two synchronous retirement stages:

1. the machine hook advances device state and emits machine-owned effects;
2. the optional host observer charges `TimingEngine`, updates step/cycle totals,
   and may request a slice exit.

The production loop requests at most 256 instructions per machine call and also
sets a remaining wake-cycle soft ceiling. Timing slice completion, loss of
timing budget, wake-cycle limits, user stop, and step limits are checked after
each retirement. Instructions remain atomic.

## Verification contract

Differential smoke coverage compares canonical and portable-IR Game Boy runs
after every instruction, including registers, mapped memory, retired cycles,
audio state, and video timing state. Additional coverage verifies portable-IR
execution diagnostics, unsupported fallback, IR-mode cache invalidation, host
retirement observer ordering/early exit, and `ir` configuration validation.

Phase 11B or later may add a native backend only behind the same ABI, guards,
per-instruction retirement contract, deterministic differential tests, and
explicit executable-memory security design.
