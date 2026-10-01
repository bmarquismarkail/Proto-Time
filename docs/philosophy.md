# The philosophy of T.I.M.E.

[Documentation index](README.md) · [Current architecture](architecture.md)

T.I.M.E. means **The Infinite Modder's Emulator**. Its purpose is to make an
emulated machine a programmable environment for playing, understanding,
modifying, and experimenting with games. Faithful execution provides a reference
against which deliberate changes can be understood.

This is a consolidation of principles already expressed in the machine-host,
executor, plugin, modding, visual-override, and concurrency documents. It is
design guidance, not a claim that every envisioned capability is implemented.

## 1. The whole machine is the unit of truth

A CPU instruction only makes sense alongside memory, devices, interrupts, and
time. `Machine` owns that world and its lifecycle. Native core code defines
hardware behavior; `RuntimeContext` gives execution policies a controlled way
to interact with it.

The durable division is: **the CPU defines what instructions mean; the executor
defines how execution is orchestrated or transformed.** A frontend presents the
result and supplies host input. It does not become the owner of guest semantics.

## 2. Be permissive about experiments and precise about guarantees

Faithful baseline execution and invasive experiments both belong here. Each run
must make its contract explicit:

| Guarantee | Meaning |
| --- | --- |
| `BaselineFaithful` | Canonical `fetch → decode → execute`; no silently enabled accelerated path. |
| `VisibleStatePreserving` | Optimization may change the execution path while preserving the promised visible machine state. |
| `Experimental` | A deliberately looser contract, explicitly selected and labeled. |

A guarantee is a policy contract, not proof of complete hardware accuracy.
Backend selection is a separate declaration. Differential tests against the
baseline establish the scope of an optimization's evidence.

Translation and trans-emulation are long-term avenues for transforming source
execution while retaining its machine model. They do not imply that arbitrary
games already run across incompatible consoles. Native execution remains an
experimental, measured path; broader JIT/DBT work is not a baseline dependency.

## 3. Make modding powerful through explicit bridges

Host-owned data, symbols, checked patches, and native hooks can extend what a
game can express. The bridge must preserve the guest hardware model and state
exactly which game consumers have been adapted.

For example, a host-side 16-bit species identity can pass through a Game Boy
native trampoline without widening the LR35902 address space or automatically
upgrading every byte-sized party, battle, and save-file field. The current
Pokémon demonstration proves a title-screen slice. Game-wide expansion remains
separate work.

Native mods run with host privileges. A narrow API makes ownership and bounds
explicit; it does not sandbox native code. Future scripting is a behavioral
extension layer for observation and controlled actions, with native CPU semantics
remaining authoritative.

## 4. Put shared capabilities in the framework

Audio, video, input, timing, visual overrides, and lifecycle coordination should
have machine/host contracts that frontends can share. A feature should not need
to be reinvented for every window backend or console.

Internal C++ interfaces and external shared-library ABIs are distinct. Executor,
IR, frontend, and audio modules use versioned pure-C tables with explicit
lifetimes and validation. Native mods have their own versioned C contract.
Replaceability inside a machine does not automatically imply a stable external
CPU or machine-provider ABI.

## 5. Make observation useful without giving it accidental ownership

Push small typed events; pull larger data through deliberate views or snapshots.
Tracing, recording, debugging, resource capture, and diagnostics should explain
execution. Cross-thread consumers receive owned immutable data with bounded
lifetimes, rather than live mutable guest state.

Visual packs normally change presentation while preserving machine-visible
behavior. Stable resource identity is preferable to transient VRAM locations.
Authoring and capture tools matter alongside runtime replacement support.

## 6. Protect real-time work by separating responsibilities

Guest state has one writer on the emulation lane. Audio preparation belongs to
the audio worker; the device callback drains prepared samples. Window events and
presentation belong to the UI/render lane. Irregular work such as image decode,
capture writes, and save flushes belongs to bounded background work.

Reset, ROM replacement, and state restore require explicit lifecycle/epoch
boundaries so old work cannot publish into a new session. Measure queue age,
underruns, tail latency, and interference before adding concurrency or expanding
CPU acceleration. Average throughput alone does not establish a healthy run.

## 7. Make failure and evidence visible

Validate compatibility, sizes, identities, and capabilities before activation.
Keep optional presentation and tooling failures from corrupting guest execution.
Fallback is useful only where retry is safe: an IR rejection before execution
can return to the interpreter; a failure after execution starts must not run the
instruction again through a second path.

Tests, measurements, and live acceptance answer different questions. Preserve
their limits. A completed milestone describes its tested slice; an old roadmap
or a checked box does not establish current support.

## A quick design check

Before adding a feature, ask:

1. Who owns the guest state, and which lane may mutate it?
2. What guarantee does this path claim, and what demonstrates it?
3. Is the extension boundary explicit about capabilities and lifetime?
4. Can the work block audio, presentation, or machine stepping?
5. What happens at failure, overload, reset, or ROM replacement?
6. Can an author or developer observe and use it without guessing?

## Where these principles are grounded

- [Machine and execution architecture](architecture.md), especially the executor and IR boundaries.
- [Plugin architecture decisions](../.internal/docs/proto-time-plugin-architecture.md).
- [Modding contracts and current limits](../machine/modding/README.md).
- [Visual override design principles](texture-pack/design.md#design-principles) and [supported authoring workflow](texture-pack/author-workflow.md).
- [Concurrency gates](../.internal/docs/concurrency-success-gates.md) and [native-backend decision](../.internal/docs/proto-time-phase-11e-native-backend-spike.md).

The original local `time-space` design supplies the whole-machine laboratory,
explicit-guarantee, and trans-emulation rationale. The [documentation index](README.md)
records its historical status and the newer contracts that supersede parts of it.
