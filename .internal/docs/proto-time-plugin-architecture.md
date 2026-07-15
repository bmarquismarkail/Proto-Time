# Proto-Time Plugin Architecture

## Status

Active design. This document supersedes the plugin-boundary portions of the
March 30, 2026 `time-space` design and implementation plan. Those archived
documents remain the historical record of the machine-owned runtime milestone.

## Decisions

Proto-Time uses four deliberately separate extension layers:

1. **Machine providers** construct complete machine implementations. Built-in
   Game Boy and Game Gear providers use an owned registry. Dynamic machine
   providers are deferred until the host can expose a complete pure-C machine
   table without leaking C++ object layout.
2. **Executor policies** select an explicit execution backend, declare a state
   guarantee and required runtime capabilities, and control recording and
   segmentation. Machines own installed policies.
3. **I/O extensions** remain internal C++ interfaces owned by the existing
   machine-driven `PluginManager`. They are not part of the stable module ABI.
4. **Host modules** are shared libraries discovered through a generic loader.
   Their public boundary is a versioned pure-C function table.

CPU runtimes remain replaceable components inside a machine, but are not an
external loading boundary in this revision. CPU state, interrupts, memory,
devices, save states, and lifecycle are too coupled to claim a useful stable
standalone CPU ABI yet.

## Executor Contract

Guarantee and backend selection are independent declarations. A non-baseline
guarantee never implicitly enables acceleration.

Supported backend requests are:

- `Baseline`
- `CachedBlock`
- `PortableIr`
- `NativeExperimental`

Every policy declares its required `RuntimeCapabilityProfile`. Installation
fails before execution if metadata, backend/guarantee coherence, or capability
requirements are invalid. The machine owns a clone of the policy, eliminating
the previous borrowed-reference lifetime contract.

The legacy `--cpu-mode baseline|block|ir|native` CLI remains an alias for the
built-in executor policy IDs.

## Machine Provider Registry

`MachineRegistry` owns provider metadata and factories. It rejects empty or
duplicate IDs and creates machines by stable string ID. The existing
`MachineKind` API remains as a compatibility facade over the registry.

Built-in providers and future dynamic providers must pass through the same
registry validation and lookup behavior.

## Pure-C Module ABI

The ABI header is valid C and C++. It contains only fixed-width integers,
plain structs, opaque pointers, immutable C strings, and function pointers.
It contains no STL types, exceptions, references, virtual classes, `bool`, or
compiler-owned object layout.

A module exports `time_get_plugin_module_v1`. The returned descriptor contains
module metadata and indexed plugin descriptors. Executor descriptors point to
a versioned executor-policy table whose instances are created and destroyed by
the module that owns them.

The host validates every size, version, enum, identifier, callback, and
duplicate ID before exposing a policy. Modules must not allow C++ exceptions to
cross the C boundary; the host reports invalid descriptors and callback results
as normal host errors. The loader keeps the shared library alive until all
policy adapters are destroyed.

Executor-policy callbacks run synchronously on the emulation lane. A module may
keep per-instance policy state, but callbacks must not access live guest state,
spawn execution work, or retain observation pointers. Descriptor strings,
descriptors, and function tables are immutable module-owned storage and remain
valid until the module is unloaded. Policy configuration callbacks are
deterministic and side-effect-free; `create`/`destroy` own instance allocation.

The first ABI revision supports executor policies. Unsupported plugin kinds are
reported deterministically rather than treated as partially usable modules.

## Compatibility

ABI v1 uses exact version matching and `struct_size` prefix validation. The V1
struct prefixes are frozen. Incompatible or required table additions use a new
versioned structure and entrypoint; existing fields never change meaning.

The SDL frontend continues to use its existing build-coupled C++ factory ABI.
It may migrate onto the generic module loader later, but it is not presented as
part of this stable C ABI today.

## Verification

- owned-policy lifetime and clone behavior
- backend/guarantee and capability rejection
- duplicate machine and executor-registry ID rejection
- built-in machine creation through the registry
- loading a shared object compiled from C source
- C policy metadata, decisions, clone, destruction, and library lifetime
- malformed descriptor and missing-entrypoint failures
- legacy CLI behavior and full emulator smoke coverage
