# Input boundaries and prospective adapters

`InputEngine` and `InputService` retain their existing logical mask, analog,
recording and replay contracts. `IInputSnapshotSourceV1` is a separate interface:
a host source supplies one owned digital/analog snapshot at a machine boundary.
Host sources do not advertise deterministic replay. Record the committed logical
input through the core's existing recording/analysis path before replaying it.
Existing deterministic adapters and external plugin interfaces remain loadable.

## Linux native controllers

Launch either core with `timeEmulator --input-evdev /dev/input/eventN` or an INI
`[input]` section containing `evdev=/dev/input/eventN`. Explicit controller
selection takes precedence over frontend keyboard input. No device discovery,
permission changes, grab, force feedback or system configuration is performed.
An unavailable selected device is retried every 100 ms; execution receives
neutral input until it connects. Diagnostics report the missing device.

`LinuxController` has a host worker for opening, reading, calibration ioctls,
hotplug retry and shutdown. The machine samples a fixed 32-packet SPSC queue,
with no device reads, waits or allocation inside sampling. A single snapshot
contains eight logical digital bits and four signed analog channels. Defaults
map d-pad/hat, south/east buttons, Select/Start and X/Y/RX/RY sticks. The API
accepts an immutable explicit `LinuxControllerProfile`. Calibration uses evdev
minimum/maximum/flat values; values are clamped and normalized, with a dead zone.

Only `SYN_REPORT` commits event changes. On `SYN_DROPPED`, the worker ignores the
remaining packet and queries keys/axes after the next report, following the
[Linux input event protocol](https://docs.kernel.org/input/event-codes.html).
Disconnect invalidates the connection epoch immediately. Generation-tagged
snapshots from before a machine reset are rejected; the worker republishes its
current physical state for the requested generation. Queue overflow, malformed
input and failed resynchronization latch neutral input until pause/resume or
reopen. Loss is visible through fixed diagnostics and `lastError()`.

`smoke-linux-controller` feeds an explicitly duplicated nonblocking event stream,
checks Game Boy JOYP and Game Gear ports on real execution paths, and exercises
loss, lifecycle and generation behavior. This surrogate does not establish
physical controller/hotplug acceptance. Actual evdev device and audible/UI
acceptance remain **not run**.

## Frame-indexed remote input

`Netplay::LockstepEngine` has separate fixed 128-frame reorder and completed-history windows and exactly
two peers. Each peer owns a disjoint subset of the one logical controller mask;
the subsets must cover all eight bits. Input is combined by OR, with out-of-owner
bits rejected. A spectator may own zero bits, but must still acknowledge state.
Every packet binds the core, exact loaded ROM SHA-256, configuration digest,
session digest, protocol generation, frame number and complete pre-frame machine
fingerprint (wrapped in SHA-256 without changing either core's existing fingerprint format). Duplicate packets must be identical. Conflicting duplicate input,
state mismatch, generation changes, malformed packets and exhausted windows
pause explicitly; no speculative execution or old-frame input fallback exists.

`LockstepService` belongs exclusively to the machine lane. It reuses the existing
exclusive instruction-control lease with trace collection disabled, and rejects
other debugger/capture owners. Admission requires baseline execution, detached
host input, no plugins, and no native-mod/host-region state. These incompatible
combinations are explicit; loading/resetting guest state invalidates the session.
The network worker never has a machine pointer. Input is published exactly once
per acknowledged frame; each instruction retires through the ordinary core path.
A caller instruction budget yields an explicit pause and may resume the same
frame without republishing input. Direct `Machine::step()` is rejected while the
session owns execution.

A protocol frame is a fixed emulated clock interval from session attachment:
Game Boy `456 * 154` cycles; Game Gear `228 * 262` cycles. These are the current
cores' frame periods. LCD-off execution still advances protocol frames. Input
changes only at instruction retirement; overshoot carries into the next interval.
An instruction spanning more than one additional whole frame pauses with an
explicit overrun, rather than retroactively applying inputs inside it. The
complete CPU/device/mapper/audio/video fingerprint is compared before the next
frame executes. Protocol frames need not align with the initial display phase.

`RemoteTransport` uses explicit numeric IPv4 endpoints and UDP on a host worker,
with 64-packet queues, a 128-packet retransmission history, duplicate coalescing,
a fixed per-wake work quantum and configurable 100 ms–60 s disconnect timeout.
It rejects packets from other endpoints. Malformed trusted-peer packets, queue
loss and disconnect latch a visible failure. It tolerates reordering and loss by
retransmitting every 100 ms. Endpoint/session/digest checks and the SHA-256 wire
trailer provide binding and corruption detection, **not authentication**. Use
trusted configured peers. Stop/restart only with the machine producer/consumer
quiescent; restart begins an empty transport. Worker shutdown joins outside the
machine instruction path.

`time-netplay` is a standalone headless host for reproducible sessions. Start two
processes with the same core, ROM and 64-hex-digit session, exchanged bind/peer
ports, and peer indexes 0 and 1. Peer 0 owns all bits and may use `--input 17`;
peer 1 owns none. For example:

```
time-netplay --core gamegear --rom game.gg --bind-port 40001 --peer-port 40002 --peer 0 --session <64 hex digits> --frames 60 --input 17
time-netplay --core gamegear --rom game.gg --bind-port 40002 --peer-port 40001 --peer 1 --session <same 64 hex digits> --frames 60
```

Default addresses are loopback. Explicit `--bind-address`, `--peer-address`,
`--timeout-ms` and `--configuration` are supported. Configuration binds the
compiled source digest, ROM, core, clock, baseline backend and ownership. An
extra configuration string also binds both peers. The CLI bounds ROMs to 16 MiB
and reports to 4096 frames. Its final packet acknowledges the final complete
fingerprint without executing another frame. JSON reports contain source/ROM/
configuration identities and per-frame instruction/cycle/fingerprint accounting.
Use a fresh session digest for a new run; there is no unattended reconnection or
rewind into an old session.

`smoke-netplay` compares both cores after each complete protocol interval with
independent baseline machines, including cycles and fingerprints; tests packet
binding, reordering, duplicate conflicts, stale history, missing peers, edits,
reset invalidation and direct-step rejection. `smoke-netplay-transport` covers
real UDP handoff, disconnect, malformed input and overload. `smoke-netplay-cli`
runs two independent processes for each core and checks final acknowledgments.
These are automated loopback checks. Physical controllers, cross-machine live
network sessions and browser integration remain separate acceptance requirements.
