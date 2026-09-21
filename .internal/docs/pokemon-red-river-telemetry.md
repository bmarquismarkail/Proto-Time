# Pokémon Red River XMB telemetry

Real telemetry is automatic for the English retail Pokémon Red ROM with SHA-1
`ea9bcae617fdf159b045185467ae58b2e4a48b9a` (1 MiB). Header titles still select
presentation, but cannot authorize RAM decoding. Other versions, translations,
and modified ROMs do not publish real telemetry. `--river-xmb-simulated` enables
demonstration data as a fallback for unsupported ROMs. Recognized Red ROMs always
use real telemetry, including when this flag is present.

## Authoritative data

Verified against the local pokered disassembly and `symbols:pokered.sym` at
`8cb28a31c7fb9f48fc496a8017fb7fa7c3d07e5a`; ROM identity is recorded in
`pokered/roms.sha1`. Layout and behavior sources are `ram/wram.asm`,
`macros/ram.asm`, `constants/map_constants.asm`, `constants/charmap.asm`,
`engine/play_time.asm`, and `engine/battle/core.asm`.

| Field | WRAM source | Interpretation |
| --- | --- | --- |
| Location | D35E, wCurMap | English Red map ID; towns, routes, interiors and dungeons |
| Party | D163 count, D164 species list, D16B records | Up to six 44-byte records; FF list terminator |
| Name | D2B5, wPartyMonNicks | Eleven-byte game-encoded nickname slots |
| Species / level | record +0 / +33 | Internal species ID, not Pokédex number; level 1–100 |
| HP / maximum | record +1 / +34 | Big-endian 16-bit values |
| Battle HP / maximum | D015 / D023 | Override active party slot CC2F while D057 is 1 or 2 and D11D is zero |
| Badges | D356, wObtainedBadges | Population count of eight flags |
| Play time | DA41 hours, DA43 minutes, DA44 seconds | Integer elapsed in-game seconds; DA42 is the maxed flag, not an hours byte |

D732 bit 0 identifies game timer counting; invalid/uninitialized state emits
`available: false`, an empty location and empty party to clear previous data.
Clock ranges, map IDs, party counts/terminators, nickname termination, species
consistency, levels and HP bounds are validated. Zero HP is valid. An empty party
is valid once the game initializes it. Unrepresented name glyphs become `?`.

## Ownership and publication

The emulation lane captures an owned 8 KiB WRAM value between execution slices,
at most four times per host second. `peek8` in this range reads WRAM without MMIO
side effects. The snapshot exposes decoding only, with no mutable memory views.
It is a coherent host-side capture, not a promise that the guest has finished a
multi-instruction update; validation rejects detectable transitional data.

Only the serialized owned JSON string crosses to the existing IPC worker. No
machine pointers or guest-memory references enter that worker. The queue remains
bounded and keeps only the newest pending telemetry. Identical payloads are
suppressed; a failed old send cannot discard a newer snapshot. Socket operations
are nonblocking and retry waits are interruptible by stop. A missing endpoint
backs off, including when XDG_RUNTIME_DIR is unset.

Tests use synthetic guest WRAM and a private temporary UNIX socket; neither a
copyrighted ROM nor a running River session is needed. They cover capture
independence, all six slots, big-endian HP, battle overrides, empty/invalid state,
ROM title spoofing, JSON, delayed endpoint recovery and duplicate suppression.
Live game/display acceptance is separate from these automated checks.
