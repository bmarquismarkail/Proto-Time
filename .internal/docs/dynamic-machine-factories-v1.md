# Dynamic machine factories, version 1

This is the construction and lifetime portion of the approved two-core provider
program. It does not close `providers.dynamic`: a complete pure-C table for
independently implemented CPU/device/runtime providers is now provided separately
by [runtime ABI version 2](dynamic-machine-runtime-v2.md). Remaining acceptance
for the selected plugin design stays in the completion matrix. No additional machine family is implemented or admitted.

`TimeMachineProviderAbi.h` is a separately versioned C interface. A shared library
exports `time_get_machine_provider_module_v1`. It describes at most 64 factories,
each with an admitted hardware family, stable ID, display name and frame extent.
The loader copies and validates metadata, versions, sizes, callbacks and IDs.
Registration validates the entire module in a staged MachineRegistry, then swaps
it into the registry. A duplicate or unavailable family publishes no factories.
Existing TimePluginAbi, executor, native-mod and IR interfaces are unchanged.

A version-1 factory constructs a complete current Game Boy or Game Gear machine
through a host-owned, monotonically generated handle. It can install a validated
BIOS of at most 32 KiB, with the existing core imposing its stricter limits.
Host tables and handles expire when the synchronous factory returns. No C++
pointer, class layout, STL value or exception crosses the external interface.
Invalid host operations poison construction; a module cannot ignore an error and
publish a partially configured machine. Returned handles must belong to that
construction and match its declared family. Factories cannot substitute another
family or supply a third core through a mislabeled descriptor.

The actual native machine retains its CPU, hardware, mapper, capture, checkpoint,
input, audio, video, executor and native-mod services. BIOS configuration is owned
by the machine and reapplied after its ordinary ROM-reset path, before RomLoaded
publication. Guest state still has one writer. Explicit --boot-rom configuration
overrides a provider default and follows the same validated reset behavior.
Game Boy FF50 disable remains one-way until an explicit ROM-reset transition.

Modules return optional lifetime metadata and a required release callback. The
host releases failed constructions and retains successful module instances until
all derived machine members and host services have been destroyed. A registry
can be destroyed while its machines continue to execute, save and restore.
Release is nonthrowing and must not access the already-destroyed machine. This is
trusted native code, like existing in-process plugins; descriptor validation is
not process isolation. Modules cannot retain construction tables or create guest
writers on other threads. Runtime foreign-machine callbacks are deliberately not
claimed by this construction ABI and require a separate versioned contract.

Use `timeEmulator --machine-provider ./module.so --core PROVIDER_ID --rom ROM`.
INI configuration uses `machine_provider` and `core` in the `[emulator]` section.
Without a provider module, existing Game Boy/Game Gear aliases remain unchanged.
The provider family determines compatibility MachineKind and core capability
checks, including baseline S.P.A.C.E. capture. Configuration and module creation
run on the control lane before publishing the machine to execution.

`smoke-machine-provider` builds a genuine C shared library and checks both-core
execution/state/BIOS, post-registry lifetime, unload, failed factory cleanup,
ABI/descriptor rejection, family binding and atomic registration.
`smoke-machine-provider-cli` exercises CLI and INI paths, headless diagnostics and
unknown-family rejection in separate processes. Normal core/host/config tests,
the discovered full suite, native TSAN, ARM64 QEMU and guest TSAN provide the
remaining validation. These checks are not completion evidence for the missing
independently implemented machine table.
