# Proto-Time Phase 11B: IR Hardening and Measurement

## Scope

Phase 11B hardens the portable Phase 11A execution boundary before any native
code generation or broader opcode lowering. It keeps guest execution on the
emulation lane, preserves one guest-instruction retirement per runtime step,
and does not allocate executable memory or change page permissions.

This phase answers two questions: whether the portable IR contract is precise
enough to be a backend boundary, and whether the current per-instruction
interpreter is fast enough to justify expanding its opcode subset.

## Guard and dispatch contract

Code-byte guards use a side-effect-free executable-byte peek. The Game Boy
implementation accepts only stable ROM, work RAM, and high RAM addresses. It
does not guard by reading VRAM, cartridge RAM, echo RAM, OAM, MMIO, or the
interrupt-enable register, because those reads may be device-visible or may not
describe stable executable storage. A translated block containing code outside
the accepted regions remains on the Phase 10 path.

Guard failures are classified as mapping generation, helper ABI, execution
state, code bytes, or ineligible code. A successful cache lookup supplies the
translated instruction index directly to IR dispatch; execution no longer
linearly searches the block for the current PC.

The execution-state guard continues to reject STOP, HALT, DMA-restricted fetch,
pending enabled interrupts, the HALT bug, and pending CPU-internal cycle charge.
All guard checks happen before an IR instruction mutates guest state.

## Portable operation semantics

The architecture-neutral interpreter now defines shifts without relying on
implementation-defined signed C++ behavior. Logical and arithmetic shifts have
explicit width truncation, sign fill, and over-shift behavior. Signed comparison
uses the declared IR width rather than host integer representation.

`Exit` stops normal operations but must flow to the instruction's final
`RetireInstruction`. The validator requires at most one `Exit`, immediately
before that retirement marker. Execution reports branch, exit, and retirement
separately. A backend result that does not reach retirement is a contract error;
the runtime does not retry an instruction after partial execution.

An exit is a translated-backend boundary. It does not weaken the machine slice
contract: device advancement, emitted effects, and the host timing observer
still run synchronously after the retired instruction.

## Measurement

The Game Boy block-cache diagnostics now report:

- lowered and ineligible instruction counts;
- guard-check count and elapsed time;
- IR-lowering elapsed time; and
- portable-IR execution elapsed time.

The retained synthetic block-cache benchmark compares baseline, Phase 10 block,
and portable IR modes over five runs. On the development host used for this
phase, its medians were:

| Mode | Median | Relative to baseline |
|---|---:|---:|
| Baseline | 93.02 ms | 1.00x |
| Phase 10 block | 42.83 ms | 2.17x |
| Portable IR | 95.30 ms | 0.98x |

The fixture reached 66.7% portable-IR dispatch coverage. Average measured guard
work was 79.5 ns per guard check and portable execution was 253.4 ns per IR
instruction. These numbers are host- and build-dependent; the benchmark keeps
the existing Phase 10 two-times throughput gate, but records rather than gates
portable-IR speed while that backend remains experimental.

## Verification

Phase 11B adds or extends coverage for:

- every currently lowered LR3592 opcode across randomized register and memory
  states, compared instruction-by-instruction with canonical execution;
- mixed supported/unsupported instruction corpus execution;
- mapped-memory operands and unsafe executable-address fallback;
- all guard failure classes, including every execution-boundary state bit;
- interrupt, EI, HALT, and HALT-bug boundaries;
- explicit exit/retirement validation; and
- architecture-neutral signed comparison and shift edge cases.

Existing DMA smoke coverage remains authoritative for live DMA behavior, while
the portable-IR guard test forces and verifies DMA-restricted rejection.

## Decision and next step

Phase 11B does not broaden the lowered opcode set. The measurement shows that
adding opcode coverage to the current per-instruction IR interpreter would
increase complexity without recovering the Phase 10 throughput gain.

The next phase should first define a block-level backend entry that validates
guards once, dispatches by indexed instruction entry, and may execute a bounded
sequence while still returning to synchronous machine/device retirement after
every guest instruction. Only after differential and latency measurements show
a benefit should the project select a native backend. Any native backend also
requires a separate executable-memory design covering W^X transitions, bounds,
cache flushes, untrusted-ROM inputs, and architecture-specific support.
