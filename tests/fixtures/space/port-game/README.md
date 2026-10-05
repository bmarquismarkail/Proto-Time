# Collect and Exit: S.P.A.C.E. stage 5 proof

This is a repository-owned game and an agent-authored port, not a reusable CPU
translator. Earlier fixtures remain separate. Both cartridges contain 64 KiB.
Builds require installed WLA-GB/WLA-Z80 and WLA-Link; reference checks require
Gambatte and Genesis Plus GX shared libraries and Python Pillow. Missing tools
are “not run”, never passing acceptance.

From the repository root:

```sh
cmake -S . -B build-working
cmake --build build-working -j4
python3 tests/fixtures/space/port-game/proof.py \
  --output build-working/space-stage5-proof \
  --space build-working/time-space \
  --capture build-working/time-space-port-capture \
  --state build-working/time-space-port-state
```

`proof.py` builds both ROMs twice and compares their bytes. It discovers the exact
Game Boy ROM with a short S.P.A.C.E. capture before admitting ROM-matched assembly,
WLA symbols and assets. It collects 16 fresh-history windows, compares capture
on/off fingerprints (including cycles) at each captured boundary, analyzes the
merged discoveries, runs both T.I.M.E. machines, launches each libretro reference
core in a separate Python process, and checks committed game ticks, artwork and
sound. `reference.py` neither imports nor links T.I.M.E. execution code. Its ABI
follows the [libretro header](https://github.com/libretro/libretro-common/blob/master/include/libretro.h).

No download, server, configuration change, live launch or third core is involved.
All generated outputs live below the specified output directory. Reference cores
receive that private directory as their system/save/content directory; user
RetroArch configuration is untouched. The runner replaces its own generated
`rom-first.json` on repeat runs; use a dedicated output directory.

## Gameplay and state mapping

The visible area is 160×144, with two HUD rows above a 20×16-cell playfield.
Direction priority is right, left, down, up. Hold a direction for one move per
eight VBlanks. Collect the three gems, then enter the exit. Start resets on a
rising press. Walls and the locked exit block movement. Successful moves toggle
two walking poses; idle and blocked movement use pose zero.

Both implementations map X/Y/picked/score/won/pose/phase/input/previous/cue/
remaining/note/ready/flag to C000–C00D, frame to C00E–C00F, committed tick to
C010–C011 and current bank to C012. Counters at C020–C024 expose blocked moves,
pickups, victories, last cue and sound kind. Probes are reads; the harness never
writes guest memory. Comparison uses committed ticks, not equal CPU cycles.

Bank 1 Tick and bank 2 Attempt both enter at 4000. Fixed-bank trampolines save
the caller bank on the stack and restore it after nested calls. Slot 0 stays
fixed. Game Boy uses MBC1; Game Gear uses FFFE paging. IRQ handlers run in fixed
ROM; only VBlank is enabled. A reviewed 64-byte stack allowance covers the
nonrecursive call/interrupt nesting. The game never executes RAM.

`gameplay.gb.inc` and `gameplay.gg.inc` are separate authored implementations.
Initialization, input, rendering, sound, mapper and interrupt interfaces are
explicit replacements. Tile encoders preserve the artwork in `assets.py`; Game
Gear uses a native viewport offset of (48,24), four planar bits with the upper
two zero, and separate background/sprite palettes. Source/target snapshots are
normalized by four luminance ranks and checked against canonical artwork.

Cue contracts are blocked 440 Hz for four frames, pickup 880 Hz for eight, and
victory 880/1046/1319 Hz for eight frames each. Pitch may differ by 5%, duration by
one logical frame. Sound counters alone do not pass the reference test: it also
measures transitions and active durations in emitted audio. Waveforms need not
match. The video scan precedes the VBlank update; artwork checks accept the
committed tick or its explicitly mapped preceding tick.

## Accounting and outputs

`ledger.py` is deliberately fixture-specific. Exact symbols delimit code/data,
complete LR35902 instruction boundaries classify code, and FF assembler fill
accounts for remaining bytes. Header checksums are patched by the build recipe.
`ledger.json` records inventory, translations, replacements, hardware/control
obligations, reviewed model assumptions and supplemental evidence. Trace coverage
never establishes whole-ROM completeness, and ordinary annotations cannot count
as conversions. These are agent reviews, separate from human live acceptance.

Outputs include `collect-and-exit.gb`, `collect-and-exit.gg`, both symbol files,
assembly/assets, `build.json`, `rom-first.json`, `windows.json`, `captured/`,
`internal-gg.json`, independent screenshots/audio/results, `ledger.json`, analyzed
`project.json`, `graph.html`, verification records and `check.json`.
`proof-run.json` describes the latest runner result. `check.json` keeps accounting,
behavioral, independent and live acceptance separate. The stage-5 gate remains
false until live acceptance is explicitly recorded.

Open `graph.html` locally. It is a frozen capture: new execution never updates it.
Use Conversion overlay and Port inventory to inspect whole-ROM coverage and
source-to-target contracts; select a block for mappings and verification. Load a
new project explicitly to replace the revision while retaining surviving layout
and selection. User viewer review remains separate from syntax/hash tests.

## Live RetroArch acceptance

Use the **exact** `collect-and-exit.gg` whose SHA-256 appears in `check.json` and
`reference-verification.json`. Run the installed Genesis Plus GX core with a
private configuration directory:

```sh
proof_dir="$PWD/build-working/space-stage5-proof"
live_dir="$(mktemp -d /tmp/space-stage5-live.XXXXXX)"
printf 'config_save_on_exit = "false"\ncore_options_path = "%s/core-options.cfg"\nsystem_directory = "%s"\nsavefile_directory = "%s"\nsavestate_directory = "%s"\nauto_overrides_enable = "false"\nauto_remaps_enable = "false"\n' \
  "$live_dir" "$live_dir" "$live_dir" "$live_dir" > "$live_dir/retroarch.cfg"
printf 'genesis_plus_gx_gg_extra = "disabled"\ngenesis_plus_gx_overscan = "disabled"\n' \
  > "$live_dir/core-options.cfg"
retroarch --config "$live_dir/retroarch.cfg" \
  --libretro /usr/lib/libretro/genesis_plus_gx_libretro.so \
  "$proof_dir/collect-and-exit.gg"
```

Map Game Gear directions and Start in that private session if needed. Check
movement cadence and poses, wall blocking, all pickups, the locked exit, winning,
three victory notes, idle pose and Start reset. Review the frozen viewer's
conversion/inventory/evidence controls. Close RetroArch normally. The temporary
configuration may be removed afterward. Report live acceptance for that ROM hash;
headless results and generated metadata cannot provide the human sign-off.

Stage-5 viewer review and live tests were signed off by the user on 2026-10-05.
See [VALIDATION.md](VALIDATION.md) for automated results and the separate acceptance record.
