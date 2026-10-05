# S.P.A.C.E. stage 5 validation

Date: 2026-10-05. Implementation approved for commit. Stage-4 viewer presentation
and manual acceptance were explicitly signed off by the user before this stage.

## Delivered behavior

A new repository-owned 64 KiB MBC1 Game Boy game and a separate 64 KiB Sega-paged
Game Gear port assemble with installed WLA tools. Bank 1 Tick and bank 2 Attempt
share logical entry 4000; fixed-ROM trampolines restore nested caller banks.
The two-row HUD, 20×16 playfield, wall/gap, three pickups, locked exit, animation,
VBlank movement cadence, reset and square-wave/PSG cues follow the approved model.

`space/Porting.hpp/.cpp` implements optional version-1 schema-1 accounting, exact
physical inventory, reviewed classification/model evidence, bank-aware static
instruction identities, typed translations/replacements, target ranges/contracts,
hardware/control obligations, verification bindings, idempotent matching imports,
conflict rejection and bounded frozen queries. Ordinary annotations cannot establish
conversion. Port operations validate before atomic publication and never own or
resume guest execution. Artifact checks verify exact ROMs, source files, build
manifest, canonical artwork, supplemental evidence and current verification files.
Changed ledger/build/contract evidence marks historical checks stale; altered files
fail artifact verification explicitly.

The viewer adds conversion overlays, whole-ROM inventory, source-to-target details,
contracts and verification evidence. It retains explicit revision replacement,
surviving selection/layout and stage-4 routine/loop/evidence controls.

## Reproduction

Run the repository configure/build/CTest workflow. Then reproduce persistent
artifacts using the command in `tests/fixtures/space/port-game/README.md`.
Outputs are below `build-working/space-stage5-proof/`; `proof-run.json` describes the
latest proof run, `check.json` records separate accounting and acceptance states.

The driver attaches 16 fresh-history sessions at paused instruction boundaries,
32 steps each. Every captured boundary compares capture-on/off fingerprints,
including deterministic machine state and cycles. Intervening execution is
intentionally untraced and separately persisted; evidence loss stays in `gaps`.
A shared conservative cumulative 64 MiB budget covers fresh captures and merged
discoveries. Trigger waits, window lengths and scenario steps/frames are bounded.
Missed triggers, capacity loss and malformed bounds produce explicit incomplete
reports; they do not publish passing evidence. Fresh histories do not acquire
suppliers from abandoned windows.

Independent libretro harnesses run Gambatte and Genesis Plus GX in separate
processes without T.I.M.E. execution code. Read-only RAM probes compare each
committed logical game tick, including movement, collisions, all pickups, revisits,
win, reset, held Start with movement and direction priority. Both T.I.M.E. machines
are compared against the independent core states. Canonical four-shade artwork is
checked after viewport/palette normalization, accepting the current or explicitly
mapped prior tick due to scan/VBlank order. Audio checks measure positive crossing
periods and active durations against cue contracts. Tool/core versions, hashes,
exact ROMs, scenarios, screenshots, PCM audio and results are retained. Private
system/save/content directories preserve user settings.

## Automated checks

The repository configured and built successfully with no new compiler warnings.
The full CTest suite passed **150/150**, 105.33 seconds. The initial focused
S.P.A.C.E. suite passed **7/7**; final accounting/proof regressions passed **2/2**.
Sequential TSAN capture, snapshot execution, analysis, accounting and proof tests
passed **5/5**, 234.28 seconds, with no reported races. A follow-up capture-window
check positions initialization at the ROM-to-VRAM artwork upload, adding direct
transfer evidence. That final proof passed **1/1** normally (22.48 seconds)
and **1/1** under TSAN (124.45 seconds), with no reported races.
All builds and sanitizer runs were sequential; no competing builds ran during
timing checks. `git diff --check` and Python syntax checks passed.

The initial sandboxed full suite passed 149/150: the existing telemetry
test could not bind a Unix socket. A separate sandbox probe confirmed EPERM. The
capable-context suite subsequently passed 150/150. Another full run had a
37.689276 ms scheduling-to-present sample against the existing 16 ms gate; its
isolated rerun passed 1/1 and the final full rerun passed 150/150. No timing gate
was weakened and no competing build ran during those tests.

Coverage includes illegal/overlapping inventories, wrong banks and instruction
entries, unknown/unreviewed regions, unaccounted artwork/code, incomplete
replacements/obligations, same-address bank identities, stale/conflicting evidence,
idempotent import/recording, old projects, pagination limits, reproducible builds,
headers, bounded captures, omitted intervals, fresh suppliers, accounting/query
fingerprint parity and existing snapshot pause/checkpoint/continuation behavior.

## Browser, manual and live acceptance

Stage 4: **user accepted** viewer presentation and manual review in this conversation.

Stage 5: **user accepted** viewer review and live tests in this conversation: "viewer review and live tests all signed off. commit changes". This is user-reported acceptance, separate from the automated evidence below. Automated viewer JavaScript syntax, identity,
annotation, conversion-overlay and stale-verification helper checks do not establish
browser presentation or navigation acceptance. The agent visually inspected the
independent idle screenshots while correcting the second HUD row and sprite
palette. This is image inspection, not live play or browser inspection.

The prior browser-tool policy rejected file URLs and prohibited a server/alternate
surface workaround. No workaround is used. Open the delivered `graph.html` locally
and review conversion/inventory/evidence controls and explicit revision replacement.
Follow the fixture README to play the exact tested `.gg` in RetroArch with a private
configuration. No live launch or user configuration changes were performed.
No automated tool records user live/viewer acceptance. Accounting, behavioral,
independent and live states remain separate; the user has now supplied explicit stage-5 live and viewer sign-off. The automated proof outputs retain their original pre-sign-off status; they do not manufacture manual tool identities or measurements.

## Limits

The authored fixture's reviewed assembler inventory and bounded control-flow model
justify all source bytes, including unobserved code and FF padding. This is not an
automatic completeness proof for arbitrary cartridges. Capture windows are useful
observations, not exhaustive tracing. Artifact/model/source/asset changes require
updated accounting and verification. No reusable translator, automatic explorer,
model provider, general source integration, frontend snapshot expansion, acceleration
or third core was implemented. Generated artifacts stay outside version control.

## Final artifact identities and results

The final proof passed 18 scenarios over 273 committed logical ticks in each
independent core and each corresponding T.I.M.E. machine. The two reproducible
builds produced identical 65,536-byte ROMs:

- Game Boy SHA-256: `bdb75e0cf94d1bca4379cdd2fb92dbebb61edcd1a6a3511db3cb81667bdf1ae5`
- Game Gear SHA-256: `40587c4d376e6b254477b11368dfff34d098dc26ad740d3443b6052d457c48d9`

The reviewed physical inventory contains 20 regions and 442 static instructions.
The frozen capture contains 209 instructions, 45 blocks, 21 findings and 18 data
transfers. Its 16 windows executed 32 instructions each, with initialization
positioned at the artwork upload. The scenario driver executed 4,761,759 steps
and reserved 13,249,754 bytes under the conservative cumulative analysis budget.
Capture-on/off and accounting/query fingerprint parity passed.

Reference cores were Gambatte `v0.5.0-netlink d9d6cd0` and Genesis Plus GX
`v1.7.4 f2b40ca6`. Their library hashes and the harness/verifier identities are
recorded in the independent verification record. Assemblers were WLA-GB and
WLA-Z80 10.7a, with WLA-Link 5.22a. All measured cue pitches and durations passed
the declared 5% and one-frame tolerances.

Delivery is under `build-working/space-stage5-proof/`: both ROMs, `project.json`,
`ledger.json`, frozen `graph.html`, source copies, symbols, artwork, captured
windows, reference screenshots/audio and verification records. Nonempty build,
CTest and sanitizer logs are archived under its `validation/` directory.
`validation-summary.json` summarizes the automated run and its manual status at generation time. The subsequent user sign-off is recorded in this report.

The final accounting check reports `complete: true` and no accounting problems.
Behavioral and independent records pass, while `stage5Gate: false` remains explicit
in the original automated output, which predates the user sign-off. The user has subsequently accepted viewer review and live tests and authorized committing the implementation. No new automated run is claimed by that sign-off.
