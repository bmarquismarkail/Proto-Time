# Manifest directories

`loadModDirectories(directories, originalRom, "gameboy")` prepares explicitly
selected packages. Success returns a `PreparedMods` containing the patched ROM,
populated host, and ordered mod metadata including region handles. Failure
returns an error with the package path and no prepared state. The input ROM and
existing machines are never modified. Call this before loading the returned ROM
into a machine. Preparation does not install guest hooks; the emulator's
`--mod` activation path performs the explicit installation described below.

Each directory contains `manifest.json`:

```json
{
  "schemaVersion": 1,
  "id": "example",
  "version": "1.0",
  "priority": 0,
  "target": "gameboy",
  "romSha256": "<64 hex digits of the original ROM SHA-256>",
  "symbols": "pokered.sym",
  "regions": [
    {"name": "species", "size": 65536, "file": "species.bin"}
  ],
  "patches": [
    {"symbol": "LookupSpecies", "expected": "010203", "replacement": "040506"}
  ]
}
```

`symbols`, `regions`, `patches`, and `priority` are optional. Patch targets use
either a symbol or integer `bank` and `address`. Byte strings are contiguous
hexadecimal, with equal nonzero lengths. Patches cannot cross a 16 KiB ROM bank.
Game Boy ROM0 uses bank 0 and addresses below 16384; ROMX uses banks 1–255 and
addresses 16384–32767. Other cores are rejected until their offset adapter exists.

Region `guestBase` and `bank` are optional integer metadata; preparation does not
map the region. Files initialize the region and any remaining bytes are zero.
The current ModHost reset clears region data. Region names are local to each mod;
host names use `mod-id:region-name`. The returned metadata supplies the handles.

Every package must match the original ROM SHA-256, including when stacked.
Ascending priority, then ID, determines order. Duplicate IDs, duplicate imported
symbols, overlapping patches, and mismatched expected bytes reject the entire
stack. Symbol files must correspond to the actual ROM revision; a branch name
alone does not establish that correspondence. Signature fallback, dependencies,
and unknown fields are rejected in schema 1.

## Native modules

An optional `"nativeModule": "module.so"` names a contained regular file. Directory
preparation resolves its path without loading code. Explicitly call
`NativeMod::load(metadata, sharedHost)` to execute trusted local native code.
The exported `time_get_mod_v1` function returns the descriptor in `TimeModAbi.h`.
The descriptor's ID/version must exactly match the manifest. The library must
provide all lifecycle, invoke, and state callbacks, even when stateless. Export
the entrypoint with `TIME_MOD_EXPORT` and C linkage. Native loading uses the same
POSIX `dlopen` mechanism as the existing plugin loader.

The module receives a stable versioned host table with region lookup by local
name, bounded region reads/writes, and bank/address symbol resolution. Handles
are restricted to the module's declared regions. It receives no raw C++ objects.
Native code has process privileges; these API checks do not sandbox a module.

An optional `trampolines` array declares fixed-bank symbol names and numeric
hook IDs. The emulator CLI loads the trusted module on the emulation lane and
installs one instance for each declaration; a native module without declarations
is still loaded for lifecycle use.

`NativeMod` pins the shared host and library through instance destruction. Use
and destroy it on its creating emulation thread, with no concurrent calls. Host
callbacks are only valid during a module callback. Reentrant/wrong-thread public
calls throw. C callbacks must not throw, retain borrowed buffers, or call the host
asynchronously. Destruction calls the module's destroy callback before unloading.
Failed creation also destroys a non-null partial instance.

`invoke` forwards a numeric hook ID, PC, and argument, publishing only the result
field on success. Hook IDs belong to the module. The Game Boy adapter below
bridges guest calls and the `HL` register; this generic module API does not expose
arbitrary guest memory or register access. No guest hooks install themselves
when a module is loaded. A failed call does not roll back module/region mutations.

`save` returns native bytes with mod ID, version, and state schema. `restore`
rejects mismatched identities, schemas, or lengths before calling the module.
State size is fixed per instance and limited to 16 MiB; stateless modules use zero.
Modules must reject invalid payloads without partial mutation. Host region state
is serialized separately by ModHost; combined machine save-state integration is
still pending. Reset calls only the module reset callback.

## Game Boy trampoline

`GB::GameBoyMachine::installNativeTrampoline(address, module, hookId)` registers
one native entry over a `RET` opcode in fixed ROM0 ($0150..$3fff). A guest `CALL`
must place its return address on the LR35902 stack. After an actual instruction
fetch (not a DMA/HALT stall or interrupt entry), a matching entry receives
`hook_id`, the PC, and `HL` as `argument`. Its successful result must fit in
16 bits and is published to `HL`. The normal interpreter then executes the
guest RET, including stack changes, 16 guest cycles, and retirement observation.
Hooks work inside multi-instruction slices. Active hooks force baseline
execution so optimized blocks cannot bypass an entry point.

Trampoline addresses are unique per machine. Installing a null module or duplicate
address fails. Native invocation failure throws and leaves the guest at the
trampoline PC for deterministic diagnosis. `clearNativeTrampolines` restores
ordinary execution; clearing also destroys the owned module instances. Loading
another ROM clears the entries. Machine save/load states reject active native
hooks until combined module/pool state serialization is implemented.

## Pokémon species selection

`PokemonSpeciesRegistry` is the first extended species consumer. Register
existing game IDs with `registerVanilla(uint8_t, record)` and add mod records
with `addExtended(record)`. Extended IDs are allocated from `0x0100` upward and
returned as 16-bit opaque values. `select` validates either form and `resolve`
returns the record. A native trampoline receives the selected value through
`HL`, so values above `0x00FF` survive the guest-to-host boundary. Vanilla ROM
fields remain byte-sized; callers use a registry handle when an extended record
is selected. This standalone registry is not the storage format of the title
patch below.

The revision-pinned [title species demo](../../mods/pokered_species/README.md)
now patches the real title selector and its header call site. It selects ID 256
from an external 16-bit table and copies its external header into guest WRAM.
It is a bounded title-screen proof, not game-wide species expansion.

Paths must be relative regular files contained in the canonical package root;
parent traversal and escaping symlinks are rejected. Limits: 128 packages, 1 MiB
per manifest, 16 MiB per symbol file, 64 MiB total region data, 64 MiB ROM,
1024 regions and 4096 patches per package, JSON nesting below 32. Packages must
remain unchanged during preparation. This is not a sandbox for concurrent
hostile filesystem modification.

Building requires the CMake packages `nlohmann_json` and OpenSSL Crypto in addition
to existing dependencies. No files are downloaded during configuration.
