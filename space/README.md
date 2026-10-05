# S.P.A.C.E. stages 1–2

S.P.A.C.E. captures Game Boy baseline execution into a ROM-bound analysis project.
It provides a control-flow graph, current sparse block snapshots, observed
register/memory dependencies, writer provenance, and paired exploration
checkpoints. Guest execution still uses the original machine state.

This is an observation and exploration milestone. Routine-purpose inference,
automatic exploration, snapshot-backed execution, conversion accounting,
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

`time-space explore --rom ROM --project PROJECT [--commands FILE.jsonl]`

| Command | Behavior |
| --- | --- |
| `step` / `run`, optional `count` | Advance 1–1,000,000 machine steps (default 1); boundary/stall steps can retire no opcode. |
| `input`, `mask` | Hold logical buttons until changed; bits: right 1, left 2, up 4, down 8, A 16, B 32, select 64, start 128. |
| `checkpoint`, `path` | Create a new paired bundle; existing destinations are rejected. |
| `restore`, `path` | Validate and restore a paired bundle; retain discoveries and create a new history branch. |
| `export`, optional `path` and `html` | Save JSON, optionally a self-contained viewer; default JSON destination is `--project`. |
| `status` | Return revision, block count, gaps, history and machine fingerprint. |
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
record kinds. Snapshots are shadow state; device reads still use the actual bus.

Analysis has a conservative 64 MiB allocation budget and imports are limited to
128 MiB and nesting depth 64. Exhaustion or trace overflow stops capture and adds
an explicit gap while the emulator continues. An incomplete instruction is not
published as complete evidence. Checkpoints of a gapped capture still carry those
limitations; they cannot recover observations that were never recorded.

No completeness claim follows from a gap-free capture: unvisited paths, unknown
mappings, code revisions and unresolved transfers remain visible. Hardware labels
are address-space observations, not claims about a routine's gameplay purpose.

## Validation

`smoke-space` checks state parity, register operands, bank/code identity, CFG
splitting, DMA provenance, persistence/merge, restored writers and retained
analysis, corruption rejection, queue handoff and limits. `smoke-space-cli`
exercises input/checkpoint replay, HTML export, manual capture and mode rejection.
Use the full repository CTest suite and TSAN for changes to capture handoff.

Visual/browser acceptance is recorded separately in the internal validation
report. Automated HTML generation or syntax checking is not a visual acceptance
claim.
