# Proto-Time Phase 11E: Bounded Native Backend Spike

## Outcome

The recommended native-backend spike is complete. It validates the executable
memory and backend-lifecycle contract, but it does not outperform the Phase 10
threaded byte backend. The current design is therefore a **no-go for opcode or
architecture expansion**. It remains useful as a guarded reference boundary
and an explicit `--cpu-mode native` experiment.

## Scope

The spike deliberately keeps the Phase 11D coverage freeze:

- x86-64 on POSIX hosts only;
- only the already lowered LR3592 subset;
- the existing versioned Game Boy execution ABI;
- the existing mapping, helper ABI, execution-state, and code-byte guards;
- the existing block cursor and Phase 10 fallback;
- synchronous machine/device and host-observer retirement after each guest
  instruction;
- no guest opcode bytes emitted as host instructions.

Each lowered instruction is converted to a bounded, allocation-free execution
plan. A fixed 25-byte x86-64 thunk supplies that plan and the ABI to a trusted
executor. The artifact is owned by its translated cache entry, so invalidation
or eviction retires code and plan together on the emulation lane.

## Executable-memory contract

Native pages are allocated `PROT_READ | PROT_WRITE`, populated only with a
fixed instruction template and trusted process pointers, synchronized with
`__builtin___clear_cache`, and sealed `PROT_READ | PROT_EXEC` with `mprotect`.
They are never simultaneously writable and executable and are never changed
back to writable. Compilation is rejected for malformed IR, more than 16 guest
instructions, more than 64 operations per instruction, more than 8 operands per
operation, more than 64 local values, or non-I8 Game Boy memory operations.

`smoke-gameboy-native-ir` checks the actual Linux `/proc/self/maps` permissions,
bounded rejection, execution, machine-level state agreement, and cache
invalidation. The randomized opcode differential test now runs both portable
and native backends against canonical execution for every currently lowered
opcode.

## Measurements

The release-style synthetic benchmark used five runs of 250,000 measured guest
instructions after warm-up:

| Backend | Median | Speedup vs baseline | Relative to Phase 10 |
|---|---:|---:|---:|
| Baseline | 93.17 ms | 1.00x | 0.495x |
| Phase 10 block | 46.12 ms | 2.02x | 1.00x |
| Portable IR | 72.36 ms | 1.29x | 0.637x |
| Native spike | 73.80 ms | 1.26x | 0.625x |

The deterministic external-ROM run used the Phase 11D stable 20-ROM sample,
100,000 instructions, and two repetitions across baseline, block, IR, and
native modes. Eighteen ROMs ran in all modes with no retired-instruction,
cycle, or full-machine fingerprint mismatch. Two selected GBC images were
rejected identically by every mode by the existing `ROM too large` core limit;
they provide no backend comparison and are excluded from performance totals.

| Backend | Runnable cases | Geometric mean vs baseline | Median vs baseline |
|---|---:|---:|---:|
| Phase 10 block | 18 | 1.073x | 1.053x |
| Portable IR | 18 | 1.045x | 1.041x |
| Native spike | 18 | 1.021x | 1.040x |

Both IR backends had 8.97% aggregate dispatch coverage and 0.113 continuations
per block entry in this sample. The native boundary cannot overcome low
coverage, and its thunk still enters a generic operation executor once per
guest instruction.

## Decision and next steps

The continuation criterion required a material improvement over Phase 10. The
native spike is about 37.5% slower than Phase 10 synthetically and trails it on
the representative corpus, so the criterion is not met.

Do not broaden this backend's opcode coverage or port it to ARM64. If native CPU
acceleration is reconsidered, begin with a new design review that emits useful
host work for a whole basic block while preserving an explicit retirement
side-exit after every guest instruction. Compare that design against the Phase
10 backend before adding coverage. Until then, Phase 10 is the production CPU
acceleration path; portable IR and this native spike remain differential and
architecture-contract references.
