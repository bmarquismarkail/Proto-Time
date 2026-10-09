# Dedicated debugger

`time-debugger --core gameboy|gamegear --rom ROM --trace TRACE.jsonl`
is a standalone headless DAP process. An editor may launch it with stdin/stdout
transport; diagnostics go to stderr. Baseline execution remains the default.
Only the selected existing machine family is constructed. No host service or
listening network socket is installed.

`DebugEngine` records fixed-size accesses and retirements in a 1024-record SPSC
ring. A full ring drops new records and counts loss; watchpoint decisions still
apply. Read watchpoints include instruction fetches. Game Gear port records carry
the complete effective 16-bit port, separate from memory addresses. Register
snapshots contain all 20 supported Z80 architectural fields, including shadows,
interrupt mode, refresh, HALT and deferred EI state.

`DebugService` owns paused/running/faulted/detached policy. The machine lane alone
sets rules, checks bank-specific instruction breakpoints, retires instructions,
and publishes edits. Up to 64 breakpoints, 64 watchpoints and 64 edited RAM bytes
are accepted. Requests and replies have separate 32-entry SPSC queues; there is
one transport producer/consumer. Full request queues reject admission. Full reply
queues hold execution at a boundary until the consumer catches up. Configuration,
inspection and edits require pause and matching generation/pause identity.
Every edit validates all registers and addresses before any publication. RAM
commits bypass device/mapper ports and host callbacks; the generation advances
after commit. Reset/restore/ROM-load generations pause the service and invalidate
stale requests. Earlier trace generations remain identifiable in retained data.

Create and destroy the service on its machine lane, with a machine that outlives
it. While attached, all execution must go through `DebugService::run`; direct
machine steps are rejected. Disconnect detaches control and allows the ordinary
machine runner to resume. The standalone process ends on disconnect or EOF.
Interpreter exceptions become `Faulted`, with no further retirement. Debugger
capture requires exclusive baseline execution; simultaneous S.P.A.C.E. capture
and accelerated executors are explicitly rejected.

Paused memory reads use inspection peeks. Game Boy exposes its side-effect-free
mapped snapshot. Game Gear rejects compatibility device windows and ports that
cannot be peeked safely. RAM editing accepts Game Boy WRAM/mirrors/HRAM and Game
Gear ordinary RAM/mirrors, excluding mapper control and compatibility devices.
ROM, video, audio, cartridge RAM and I/O edits are rejected by this interface.

The DAP adapter implements initialization/configuration, one CPU thread,
instruction breakpoints, read/write memory or port data breakpoints, instruction
step/continue/pause, register scopes/evaluation/editing, bounded memory reads and
RAM writes, and disconnect. It advertises supported capabilities only. Expression
evaluation accepts register names; conditional breakpoints, arbitrary expressions,
source stepping and reconstructed call stacks are unsupported. The stack view
contains the current architectural frame. Scope/frame references expire on
resume, edits and generation changes. One machine request may be outstanding;
another receives an explicit busy response. DAP bodies are limited to 1 MiB,
reads to 256 bytes and writes to 64 bytes. Framing follows the official
[DAP base protocol](https://microsoft.github.io/debug-adapter-protocol/overview.html).

`--symbols IMPORT.json` accepts the existing ROM-bound S.P.A.C.E. symbol/source
format and validates core, ROM, source hashes and physical locations before
starting the machine. Exact physical matches label the current frame with source
position; labels do not establish gameplay meaning. File traces carry ROM/core
bindings, generations, instruction indices, physical backing, ordered accesses,
complete registers and cycle retirements. The transport lane formats and writes
them. A 100 MiB file budget or sink failure stops file output and reports lost
evidence. Profile rows aggregate retained instruction/cycle counts by physical
location; they are complete only after explicit disconnect with zero ring loss.
An interrupted file or a trace containing gaps cannot establish complete coverage.

These targets do not certify the completion matrix. Actual editor acceptance,
current-source checks on both hosts
remain separate obligations.


`DebugBackendService` admits separately versioned debugger, trace-sink and
watch-rule provider roles on the tooling lane. All receive owned snapshots or
records; none receives a machine reference. Capability checks reject callbacks
that require the live machine lane, or lack deterministic generation-aware
behavior. There are eight backend slots and 32 pending actions. Capability or
stale-action rejection, action loss and backend failures have explicit counters.
A failed sink is disabled and closed while other sinks continue. Control actions
are revalidated by `DebugService` at the machine boundary. Prepared tooling
callbacks may block on their own host APIs; interpreter state never waits on them.
