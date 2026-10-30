# Module-owned machine runtime ABI, version 2

This is the independent CPU/device/runtime table required by the selected plugin
architecture. A module owns a complete machine behind an opaque context; the host
constructs a generic adapter, without receiving any C++ object or using a native
core cast. `time_get_machine_runtime_module_v2` is separate from the version-1
construction entry and from existing executor, IR, frontend and native-mod ABIs.
A module exporting version 2 is validated as version 2; a rejected table never
falls back to version 1. Only Game Boy and Game Gear are supported by this adapter.
The completion matrix and third-family gate remain authoritative and closed.

The reference `time-native-machine-runtime` library contains both complete current
cores. `runtime-gb` and `runtime-gg` instantiate them inside the library. It uses no
host create-family callback. A genuine C-only test double separately exercises
invalid metadata, declined factories, failed preparations, invalid retirements,
event overflow, output budgets and cleanup; it is not a new console implementation.

## Execution and ownership

All execution, reads, edits, input and transitions are synchronous on the machine
or paused control lane. A step retires exactly one instruction and advances all
CPU/device state before returning. The host retains per-instruction observers,
instruction/cycle/segment budgets, device interrupt boundaries and baseline-default behavior. No instruction
is retried on callback failure. A failed retirement or exhausted event handoff
faults the adapter and requires creating a fresh instance; no hidden fallback
or partial success is published.

Events carry copied categories, ticks, addresses, values, complete optional CPU
feedback and bounded diagnostic text. At most 256 events are accepted per
instruction. The host publishes them in module order after retirement; plugins
inspect the completed instruction state. A plugin requiring an intermediate
execution-point view needs a separately versioned extension and cannot assume
that the post-retirement view is that intermediate state. Diagnostic text is
limited to 95 bytes; this does not truncate instruction identity. This ABI does
not expose instruction identity or claim S.P.A.C.E. capture support.

Architectural registers are named and width checked. The reference module uses
the current core models and validated debugger register commits: all 20 Z80
fields, shadow pairs, byte aliases, refresh, interrupts and deferred-enable state
are available. Inspection reads use peek semantics separately from execution
reads. The module remains responsible for mapper/backing identities and actual
port/device effects executed by its CPU.

Input is a bounded logical byte mask. Owned input generations are advanced on
ROM load and restore; checkpointed masks are republished before another step.
The module stages the logical mask and performs ordinary core input sampling at
a slice boundary, before its first retirement. Hardware state is sampled after
retirement; callbacks do not poll a host controller per guest instruction.
Video copies a complete 160 by 144 ARGB frame into host storage. Audio performs a
stable size/query copy into at most 65,536 host-owned samples, with validated
sample rate, channel count and frame metadata. Consumers never borrow module
buffers, and audio callbacks still drain prepared host samples. GPU work stays
on the presentation lane. Alternate extents and unsupported executor/IR/visual
pack/capture capabilities are explicitly rejected.

## Transactions and checkpoints

ROM, BIOS and state preparation never changes the live CPU/device state. A
successful preparation returns an instance-bound transaction; discard releases
it. Commit must be nonthrowing and allocation-free. The reference module builds
and validates a replacement complete core, then swaps it during publication.
It retains the old core until the next preparation or destruction, avoiding
core cleanup in the publication boundary. At most one transaction is pending.

BIOS configuration is construction-only and bounded to 32 KiB, with the ordinary
core enforcing stricter constraints. It survives ROM resets. Game Boy FF50 disable
remains one-way. ROM and opaque checkpoint payloads are limited to 32 MiB.
Reference checkpoint serialization uses a private temporary directory on the
paused control lane; paths and native core types never cross the ABI.

The host checkpoint container validates its core, provider ID, ABI version and
exact ROM SHA-256 binding, then prepares the module state and coordinated host
regions/native-module state before publication. Duplicate/missing chunks,
corrupted module payloads and incompatible ROM/provider bindings reject without
changing the live machine. The module must validate its own implementation's
opaque state format. Host services pause/resume around coordinated publication.
Module code stays loaded through context, plugin and host-service destruction,
including when the registry has already been destroyed.

## Validation and remaining acceptance

`smoke-machine-runtime` differentially compares 1,000 retirements per core,
registers, memory, complete feedback, ordered events, fingerprints, input,
owned video/audio, BIOS, save/restore, host regions, corruption, ROM binding,
reset, budget/observer boundaries, unsupported capabilities and concurrent
independent instances. `smoke-machine-runtime-rejection` uses the pure-C failure
fixture. `smoke-machine-runtime-cli` runs both module cores through CLI and INI.

Required evidence includes the full discovered suite, native TSAN, Linux ARM64
QEMU correctness and guest-kernel TSAN, with source/build/log bindings. QEMU
throughput is not native ARM64 performance evidence. Live presentation/audio,
physical devices, independently authored third-party implementations and optional
runtime extensions have no acceptance claim from these headless tests. Existing
native cores retain their S.P.A.C.E., debugger, recording, visual and IR contracts;
exposing those optional contracts through this external table requires separate
versioned extensions. This document does not mark the whole completion program
complete or replace missing live evidence with a research no-go.
