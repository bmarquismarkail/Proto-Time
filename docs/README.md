# T.I.M.E. documentation

## Reading path

1. [Philosophy](philosophy.md): purpose, invariants, and how to judge a new feature.
2. [Build and run](runtime.md): setup, execution modes, plugins, packs, mods, and diagnostics.
3. [Current architecture](architecture.md): implementation map and ownership boundaries.
4. A subject-specific guide or contract below.

## Which document should I trust?

For implemented behavior, inspect the current source and tests. Use current
guides for usage and the latest explicit subject-specific decision for design
intent. The philosophy explains the enduring constraints. A proposal is not
proof of implementation; a historical completion report is not a fresh test run.

| Kind | How to use it |
| --- | --- |
| Current guide / contract | Starting point for using or changing that subsystem; verify source when behavior matters. |
| Design reference | Rationale and target contracts; illustrative APIs and rollout steps may be superseded. |
| Historical record | Dated implementation, experiment, or measurement evidence. Preserve its scope and rejected alternatives. |
| Proposal / research | Possible future work and supporting analysis; not an approved implementation backlog. |

## Current guides and contracts

| Subject | Canonical reading |
| --- | --- |
| Setup, optional dependencies, CLI examples, diagnostics | [Build and run](runtime.md) |
| Ownership, execution guarantees, shared IR, frontend/audio boundaries | [Current architecture](architecture.md) |
| Machine providers, executor policies, internal I/O, external C ABI | [Plugin decisions](../.internal/docs/proto-time-plugin-architecture.md) |
| PSG voice stems/events, processing, MIDI | [PSG processor contract](../.internal/docs/psg-audio-processor-plugin-contract.md) |
| Visual pack authoring, capture, matching, reload | [Texture-pack index](texture-pack/README.md) |
| Manifest directories, native modules, Game Boy trampolines | [Modding contract](../machine/modding/README.md) |
| Revision-pinned title-screen species extension | [Pokémon demo](../mods/pokered_species/README.md) |
| Optional desktop presentation and real telemetry | [River XMB guide](river_xmb_game_integration.md), [telemetry contract](../.internal/docs/pokemon-red-river-telemetry.md) |
| Concurrency criteria and dated validation evidence | [Success gates](../.internal/docs/concurrency-success-gates.md) |
| Contributor/agent workflow | Root `AGENTS.md` when present; [CMakeLists.txt](../CMakeLists.txt) owns build/test registration |

## Design references

These existing public specs retain their paths for reference. New plans and
specifications belong under `.internal/docs/`, following repository guidance.

| Reference | Scope and precedence |
| --- | --- |
| [Plugin blueprint](plugin_blueprint.md) | Shared engine/service/contract/adapter vocabulary; does not define the external module ABI. |
| [Audio spec](audio_plugin_subsystem_spec.md) | Historical callback-processing design. Current host transport prepares audio before the drain-only device callback. |
| [Input spec](input_plugin_subsystem_spec.md) | Target logical-input and lifecycle contract; verify proposed APIs and overflow policies against implementation. |
| [Video spec](video_plugin_subsystem_spec.md) | Target video contract. Current realtime delivery uses a latest-frame mailbox; an older FIFO sketch is not its exact implementation. |
| [Debugger/instrumentation spec](debugger_instrumentation_plugin_subsystem_spec.md) | Target observation/control design, not a feature-completeness claim. |
| [Scripting spec](scripting_plugin_subsystem_spec.md) | Target behavioral-extension design, not a claim that a general scripting runtime ships. |
| [Timing blueprint](timing_service_blueprint.md) | Extraction rationale and target policy surface. Its sample loop predates the current local `TimingEngine` hot path. |
| [Timing jitter brief](timing_jitter_fix_brief.md) | Earlier implementation brief; use current code and success gates for present state. |
| [Visual override design](texture-pack/design.md) | Original vision; the author workflow and HD guide define supported authoring behavior. |
| [SDL migration](../.internal/docs/proto-time-sdl-subsystem-migration-plan.md) | Delivered migration record with pre-migration analysis and illustrative proposals; current architecture describes the retained boundary. |

## Execution and concurrency records

The singular `proto-time-phase-*` files below describe the staged concurrency
and acceleration milestones. They preserve contracts and dated evidence, rather
than promising that every earlier "next step" is still planned.

| Record | Retained meaning |
| --- | --- |
| [Phase 8: software frame path](../.internal/docs/proto-time-phase-8-simd-software-frame-path.md) | SIMD implementation record. |
| [Phase 9: background pool](../.internal/docs/proto-time-phase-9-background-task-pool.md) | Bounded background-work ownership and overload rules. |
| [Phase 10: block cache](../.internal/docs/proto-time-phase-10-threaded-block-cache.md) | Optional Game Boy acceleration; baseline remains the default. |
| [11A: portable IR](../.internal/docs/proto-time-phase-11a-portable-ir.md) | Conservative reference execution and guards. |
| [11B: hardening](../.internal/docs/proto-time-phase-11b-ir-hardening-measurement.md) | Validation and measurement record. |
| [11C: block execution](../.internal/docs/proto-time-phase-11c-block-level-backend.md) | Block dispatch still retires one guest instruction at a time. |
| [11D: corpus closure](../.internal/docs/proto-time-phase-11d-measurement-corpus-closure.md) | Bounded corpus evidence and recommendation that led to 11E. |
| [11E: native spike](../.internal/docs/proto-time-phase-11e-native-backend-spike.md) | Completed experiment; expansion no-go. Game Boy native/opcode and ARM64 expansion stay frozen under this decision. |
| [Phase 12: multi-core IR](../.internal/docs/proto-time-phase-12-multi-core-ir-il.md) | Shared adapter/backend contract and Game Gear proof; does not reverse the native freeze. |
| [Archived concurrency plan](../.internal/archives/docs/time-concurrency-implementation-plan.md) | Original roadmap plus accumulated closure notes. Prospective commands and open questions are historical. |

## Local research and planning corpus

The following files were present in the reviewed workspace on 2026-09-22 but
were **not tracked by Git**. The host's global ignore file excludes `.internal/`
(already tracked files there remain tracked). They may be absent in a fresh
clone. This catalog preserves their context without making them dependencies
of the public reading path or changing the user's ignore configuration.

Paths in this table are relative to `.internal/`.

| Local file | Classification and reading notes |
| --- | --- |
| `docs/time-deep-research-improving-concurrency.md` | Research snapshot; motivation and alternatives, not current implementation status. |
| `docs/Improving Concurrency in Proto-Time.pdf` | Same substantive research, with resolved source URLs. Keep for bibliography; Markdown has unresolved citation tokens. |
| `docs/proto-time-concurrency-phases-01-10.md` | Historical implementation/experiment log. |
| `docs/proto-time-concurrency-phases-11-20.md` | Historical implementation/experiment log. |
| `docs/proto-time-concurrency-phases-21-30.md` | Historical implementation/experiment log. |
| `docs/proto-time-concurrency-phases-31-40.md` | Historical log; early fallback and thread designs are superseded by later contracts. |
| `docs/proto-time-concurrency-phases-41-50.md` | Historical log, including corrections and rejected optimizations. |
| `docs/proto-time-concurrency-phases-51-60.md` | Historical profiling/audio log, including reverted experiments. |
| `docs/proto-time-concurrency-phases-61-70.md` | Historical audio diagnosis; retain distinctions between ingestion loss and source starvation. |
| `docs/proto-time-concurrency-phases-71-80.md` | Contains phases 71–76 only. Phase 74 says both "kept" and "revert"; phase 75 calls its shape rejected. Actual retention needs source/history verification. |
| `docs/proto-time-ir-il-continuation-implementation-plan.md` | Dated follow-up proposal. Measurement/approval gates for Game Gear work do not authorize expansion of the Game Boy native experiment. |
| `docs/modding-foundation.md` | Earlier foundation summary; its pending CLI/trampoline claims are superseded by the current modding contract. |
| `docs/superpowers/specs/2026-03-30-time-space-design.md` | Original laboratory vision. Machine ownership and explicit guarantees endure; initial CPU/executor-only ABI scope is superseded. |
| `docs/superpowers/plans/2026-03-30-time-space-implementation-plan.md` | Historical first-milestone procedure, including old paths and commands. |
| `docs/superpowers/specs/2026-04-06-io-plugin-design.md` | Internal typed observation rationale; not a public shared-library ABI. |
| `docs/superpowers/specs/2026-04-10-machine-input-abstraction-design.md` | Historical logical-input abstraction design. |
| `docs/superpowers/specs/2026-04-10-machine-input-abstraction-plan.md` | Historical implementation procedure for that design. |
| `docs/superpowers/specs/2026-04-12-audio-service-design.md` | Historical service extraction; callback/thread advice must be read through later contracts. |
| `docs/superpowers/specs/2026-04-13-audio-service-config-design.md` | Historical config extraction; mechanism and service admission are distinct. |
| `vision/scenarios.md` | Empty requirements placeholder; not an additional source of project philosophy. |

The plural decadal logs and singular milestone records use **different phase
numbering schemes**. Use a document title/path, not just "Phase 10," when
referencing a decision. Test counts, local ROM paths, `/tmp` reports, and timing
results in those records describe their original runs.

## Keeping this organized

- Maintain one home for each fact: philosophy here in its own guide, usage in
  runtime/feature guides, exact contracts next to their subsystem, and dated
  rationale in internal records. Link instead of copying full sections.
- Label new documents with kind and scope. When a decision changes, add a
  superseding link to the older document; preserve useful evidence and rationale.
- Keep new plans/specs under `.internal/docs/`. Check Git visibility before
  depending on a local document in shared guidance.
- Record validation dates and conditions. Discover current tests with `ctest -N`;
  do not promote historical totals or target latency numbers into current guarantees.
- Treat generated `graphify-out/` reports/wiki and build-tree files as derived
  navigation aids. They are excluded from this authored-document consolidation
  and cannot override source or maintained contracts. Agent customization files
  such as `.github/agents/sega-8bit-systems.agent.md` describe tool roles, not
  additional product requirements.
