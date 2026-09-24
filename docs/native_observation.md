# Native module observation v1

A native module can additionally export `time_get_mod_observer_v1`, returning a
`TimeModObserverV1` descriptor. The existing `time_get_mod_v1` ABI is unchanged;
modules without the optional symbol continue to load. Malformed descriptors fail
at module load. The observer uses the same instance as the normal module API.

The emulator calls `observe` between execution slices on the emulation thread,
at most once per 100 ms. Its borrowed `TimeModObservationV1` includes the current
ROM SHA-1, unique frontend app ID, lifecycle generation, and a bounded read-only
memory callback. Window-owning third parties resolve the unique app ID against
the desktop's compositor identifiers; T.I.M.E. does not depend on a desktop API.
The current frontend native-mod path supports the Game Boy address space.

Read requests are limited to 8192 bytes, cannot wrap the 16-bit address space,
and are valid only during the observation callback on its owning thread. Copy
what you need into preallocated storage. Never retain host pointers or perform
network, file, media, or blocking work in this callback. Native modules remain
explicitly loaded trusted local code; this is not an isolation boundary.

Game Boy ROM load/reset and successful state restoration advance a monotonic
machine generation. Invalid restoration does not advance it. Workers must discard
old-generation state and actions. No snapshot or module work is sent to an audio
callback. The emulator destroys observation-only modules on the emulation lane,
after stepping stops and before machine destruction. A module's destroy callback
must stop and join its workers before the module library is unloaded.

The former built-in River/Pokémon integration and simulated telemetry option are
removed. A third-party module owns ROM recognition, RAM interpretation, assets,
desktop protocol clients, and any game-specific behavior. It can load through the
existing `--mod <manifest-directory>` flow without installing guest trampolines.

Validation: `smoke-native-observer` checks optional ABI compatibility, callback
thread ownership, read observations, and generation changes. Existing native-mod
and Game Boy save-state tests continue to exercise the original lifecycle.
