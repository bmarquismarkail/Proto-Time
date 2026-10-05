# S.P.A.C.E. stages 1–3

S.P.A.C.E. captures Game Boy baseline execution into a ROM-bound analysis project.
It provides a control-flow graph, current sparse block snapshots, observed
register/memory dependencies, writer provenance, and paired exploration
checkpoints. Exploration defaults to baseline execution; stage 3 adds an explicit
snapshot execution mode with canonical write-through.

This is an observation and exploration milestone. Routine-purpose inference,
automatic exploration, conversion accounting,
verification, and `.gg` generation remain future work.

## Build and try the original fixture

From the repository root:

```sh
cmake -S . -B build-working
cmake --build build-working -j4
python3 tests/fixtures/space/create_rom.py build-working/space-fixture.gb
build-working/time-space explore --rom build-working/space-fixture.gb --project build-working/space-project.json
```

The CLI reads one JSON command per line and returns one JSON result per command.
It is paused between commands. Example session:

```json
{"op":"run","count":24}
{"op":"input","mask":1}
{"op":"checkpoint","path":"build-working/space-checkpoint"}
{"op":"run","count":8}
{"op":"restore","path":"build-working/space-checkpoint"}
{"op":"export","html":"build-working/space-graph.html"}
{"op":"quit"}
```

The same commands are checked in as `tests/fixtures/space/example.jsonl`:

```sh
build-working/time-space explore --rom build-working/space-fixture.gb --project build-working/space-project.json --commands tests/fixtures/space/example.jsonl
```

Use a fresh checkpoint destination for each run.

Open the exported HTML in your browser. It is self-contained and makes no network
requests. Select nodes to inspect instructions, snapshots, dependencies and
hardware facts. Pan/zoom, drag blocks, search, and follow neighbor buttons. The
capture remains fixed until you select a new JSON file with **Load capture**.
Selections and surviving node positions are retained. Unknown destinations and
capture gaps stay visible.

Add an annotation as an inference, confirmation, or correction, then use **Save
annotations** to download the edited project. Merge it with:

```sh
build-working/time-space merge --project build-working/space-project.json --input /path/to/space-annotated.json
```

This changes the analysis project, not execution state. Conversion and verification
are separate reserved fields, both displayed as **not assessed**.

## Commands

`time-space explore --rom ROM --project PROJECT [--commands FILE.jsonl] [--execution baseline|snapshot]`

| Command | Behavior |
| --- | --- |
| `step` / `run`, optional `count` | Advance 1–1,000,000 machine steps (default 1); boundary/stall steps can retire no opcode. |
| `input`, `mask` | Hold logical buttons until changed; bits: right 1, left 2, up 4, down 8, A 16, B 32, select 64, start 128. |
| `checkpoint`, `path` | Create a new paired bundle; existing destinations are rejected. |
| `restore`, `path` | Validate and restore a paired bundle; retain discoveries and create a new history branch. |
| `export`, optional `path` and `html` | Save JSON, optionally a self-contained viewer; default JSON destination is `--project`. |
| `execution`, `mode` | Select `baseline` or `snapshot` at the paused boundary; entering snapshot mode starts a fresh epoch. |
| `status` | Return revision, block count, gaps, history, machine fingerprint and execution counters. |
| `quit` | Save project and exit. EOF also saves the project. |

Errors are JSON results. Batch input stops on its first failed command and exits
nonzero; interactive input allows corrections. All step counts are bounded.

Other subcommands:

```sh
build-working/time-space export --project PROJECT.json --html GRAPH.html
build-working/time-space merge --project PROJECT.json --input OTHER.json
```

Capture ordinary manual play with:

```sh
build-working/timeEmulator --core gameboy --rom GAME.gb --space-project GAME.space.json
```

The project saves on orderly shutdown. The frontend's save-state request creates
`GAME.space.json.checkpoint-STEPS` while capture is active. Capture rejects
accelerated modes, mods, and executor plugins. Use paired checkpoint restore,
not raw machine restore, during a capture session. Detach capture before replacing
the ROM or boot ROM.

## Evidence, state and limits

Projects use schema 1 UTF-8 JSON with exact ROM SHA-256. Counters are decimal
strings to preserve 64-bit precision in browser tools. Existing block-script,
`SNAP`, machine-state formats and plugin ABIs are unchanged.

Instruction identity includes mapped backing location and bytes. Different ROM
banks and changed RAM code remain separate. Static edges can remain unresolved;
observed edges retain exact target instruction identities. Code discovery is
execution-driven in this milestone, not a whole-ROM static reachability proof.

Each captured CFG block has one current `MemorySnapshot`, materialized from the
active history's sparse observations and registers. Graph splits remap those
views from instruction evidence. Dependencies retain compact historical values
and writer sequences. Read captures do not become writes. The history retains
supplier values even if the originating block runs again.

Loading a project for inspection shows its captured views. Starting a new
exploration session retains its discoveries but starts new active history;
execution resumes only through explicit checkpoint restore. Restored snapshots
come from the checkpoint; abandoned-branch observations remain analysis evidence.

The emulation lane pushes fixed records into an 8,192-record SPSC queue. A worker
constructs the analysis state; inspection on other lanes does not enter that
queue. Callbacks do not allocate, perform file I/O, or wait for the worker.
CPU access, fetch, device changes, DMA, mappings and boundary effects have separate
record kinds. Analysis snapshots are shadow state; device reads still use the actual bus.
Execution snapshots are separate and owned by the emulation lane.

Analysis has a conservative 64 MiB allocation budget and imports are limited to
128 MiB and nesting depth 64. Exhaustion or trace overflow stops capture and adds
an explicit gap while baseline emulation continues. Snapshot exploration pauses at
the next completed instruction boundary. An incomplete instruction is not
published as complete evidence. Checkpoints of a gapped capture still carry those
limitations; they cannot recover observations that were never recorded.

No completeness claim follows from a gap-free capture: unvisited paths, unknown
mappings, code revisions and unresolved transfers remain visible. Hardware labels
are address-space observations, not claims about a routine's gameplay purpose.

## Snapshot execution through the CLI

Use the original fixture without downloading a ROM:

```sh
build-working/time-space explore --rom build-working/space-fixture.gb --project build-working/space-execution.json --execution snapshot --commands tests/fixtures/space/execution-example.jsonl
```

Use a fresh `build-working/space-execution-checkpoint` destination. The example
captures 24 steps, holds right, checkpoints, replays eight steps, exports the
viewer, and explicitly returns to baseline. At an interactive paused boundary:

```json
{"op":"execution","mode":"snapshot"}
{"op":"run","count":24}
{"op":"status"}
{"op":"execution","mode":"baseline"}
```

WRAM, echo aliases and HRAM data reads use the active block's sparse
`MemorySnapshot`. Unseen operands initialize from canonical RAM; later reads
refresh the local bytes from compact latest-writer values without becoming
writers. AF/BC/DE/HL aliases and SP/PC execute through the snapshot register file.
Writes reach canonical RAM once, and registers publish before bus/device effects
and hardware retirement. Device reads, instruction fetch, interrupt entry, DMA,
stalls and timer/APU/PPU advancement stay on the authoritative machine path.
Mixed-region spans preserve the baseline bus/interceptor contract.

Execution blocks use backing/bank, entry and actual instruction bytes within the
session's exact ROM identity. Interior entries split instruction ownership;
rewritten RAM code creates distinct identities. Runtime suppliers survive local
view replacement. Fetch from ROM, boot ROM, WRAM/echo or HRAM is supported; other
code backing and unsupported opcodes pause before fetch effects. Snapshot mode
requires the captured built-in baseline interpreter. Native mods and accelerated
execution remain rejected. Frontend manual play stays baseline.

`status.execution` reports `requestedMode`, `activeMode`, `pauseReason`, `epoch`,
`snapshotInstructions`, `baselineInstructions`, `authoritativeBoundarySteps`,
`snapshotReads`, `busReads` and `executionBytes`. Counters are decimal strings.
Read counts refer to CPU memory data reads, excluding instruction fetch and
register lanes. Instruction and boundary counters accumulate over mode switches;
entering snapshot mode clears execution suppliers and starts a new epoch.

A run that pauses returns its completed step count, `paused: true` and execution
status. Unsupported contracts and storage exhaustion pause before the affected
instruction; evidence loss pauses after completed effects. No automatic fallback
occurs. Use an explicit baseline command to continue. Capture that has stopped
cannot be restarted in the same session; a fresh session is required for fresh
complete evidence.

Runtime storage shares the 64 MiB analysis budget. Conservative reservations are
3 MiB for supplier state and 512 KiB per execution block (including capacity for
8,320 sparse bytes/pools and 2,048 instructions). A possible new block is reserved
before every step, even if that step ultimately reuses a block. Budget or block
instruction limits therefore can pause conservatively. This milestone makes no
acceleration claim.

Execution read evidence is optional in schema-1 projects. The inspector shows
snapshot, initialization or bus source and epoch/block/write-sequence stamps.
These execution block stamps are separate from CFG block IDs. Older captures
remain loadable; importing projects merges evidence and never activates runtime
snapshots.

Paired checkpoint manifest version 2 includes checksummed `execution.json` with
mode, epoch, local views, supplier values and counters. Save validates the complete
pair before directory publication; restore stages and validates all metadata
before changing the current session. Restore replaces execution history, starts a
new epoch and retains prior discoveries. The requested mode must match the saved
mode. Legacy version-1 bundles restore in baseline only; select baseline before
loading one. Existing `SNAP` and external plugin interfaces are unchanged.

## Validation

`smoke-space` checks state parity, register operands, bank/code identity, CFG
splitting, DMA provenance, persistence/merge, restored writers and retained
analysis, corruption rejection, queue handoff and limits. `smoke-space-cli`
exercises input/checkpoint replay, HTML export, manual capture and mode rejection.
`smoke-space-execution` compares independent baseline and snapshot machines after
every tested instruction/boundary, including ordered effects, 252 supported base
opcodes and all 256 CB operations. It also checks sparse supplier history,
revisions, pauses and paired execution checkpoints. `smoke-snapshot` covers
sparse writes in descending/overlapping address order and uncaptured read gaps.
Use the full repository CTest suite and TSAN for changes to capture handoff.

Visual/browser acceptance is recorded separately in the internal validation
report. Automated HTML generation or syntax checking is not a visual acceptance
claim.
