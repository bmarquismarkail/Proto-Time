# Game Gear-authored reverse standalone proof

This is a new Game Gear source revision of the repository-owned Collect and Exit
fixture and a separate Game Boy port. It shares authored assets/scenario rules
with the existing forward proof, whose ROMs and sources remain unchanged.
It is a fixture-specific agent-assisted conversion, not an unattended translator.

The source uses Sega banked code in slots 0/1, IX/IY-indexed state/input accesses,
and alternate AF/BC/DE/HL as private Tick working registers. VDP data/control
ports drive graphics, PSG/stereo ports drive audio, and controller/system ports
provide input. The Game Boy port replaces indexed byte accesses with explicit
RAM accesses, alternate banks with stack saves, and Sega paging/VDP/PSG/input/IM1
with Game Boy hardware interfaces. Both ROMs are standalone; neither calls the
T.I.M.E. host. Reviewed contracts/inventories bind these replacements to exact
source/target bytes rather than counting dynamic coverage as completeness.

Build and reproduce from the repository root:

```sh
python3 tests/fixtures/space/port-reverse-game/proof.py \
  --output build-working/space-reverse-proof \
  --space build-working/time-space \
  --capture build-working/time-space-port-capture \
  --state build-working/time-space-port-state
```

Requirements: WLA-GB/WLA-Z80/WLA-Link, Python/Pillow and the separately installed
Gambatte and Genesis Plus GX libretro libraries. Missing dependencies mean
**not run**. `build.py OUTPUT` can independently rebuild the source and target.
The proof builds twice, checks byte identity, performs Game Gear ROM-first
S.P.A.C.E. discovery, imports reviewed source/physical inventory, collects 16
bounded fresh-history source windows with capture-on/off full-state/cycle parity,
and runs all legal scenarios on both T.I.M.E. machines. Independent reference
cores run in separate processes with private data directories and no user
configuration changes. Gameplay quantities map to C000–C024 as in `state.inc`.

`ledger.py` accounts for every physical source byte, exact Z80 instruction
boundaries, fixed/banked entries and matched returns. IX/IY stay C000; interrupt
code preserves working AF/HL; Tick exchanges alternate registers at entry/exit.
A reviewed 64-byte stack bound includes the Game Boy save replacement. Legal
execution never enters RAM, padding, data or the cartridge header. Indirect
unattended conversion is outside this bounded model.

The independent verifier compares 273 committed logical ticks across 18 input
scenarios, normalized canonical artwork and movement poses, and measured audio
pitch/active duration. CPU cycles and waveform samples need not match across
hardware. State counters alone cannot pass graphics/audio verification.

Outputs include sources/assets/tool hashes in `build.json`, exact ROM hashes,
ROM-first capture, analyzed Game Gear project, source/target capture reports,
reviewed inventory/replacement ledger, reference PNG/audio/state artifacts,
verification records, the viewer and a final port check. `complete` in that port
check applies only to the declared fixture model. Live/audible/viewer acceptance
is separately **not run**; the two-core completion gate remains closed.
