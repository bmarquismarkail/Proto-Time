# Optional River XMB observation telemetry

This native module publishes generic `rom_sha1`, machine lifecycle `generation`,
and an `observations` counter through River's desktop API v1. It does not decode
ROM-specific memory. Telemetry is available in River's desktop-client state;
this module supplies no visible elements. Existing independent game modules can
publish their own presentation alongside it.

Build and package for your supplied ROM (the output directory must be new):

```sh
cmake -S . -B build-working -DTIME_RIVER_XMB_TELEMETRY=ON
cmake --build build-working -j4
python3 mods/river_xmb/package.py --rom /absolute/game.gb --target gameboy \
  --module build-working/libtime-river-xmb-telemetry.so --output /tmp/time-telemetry-mod
build-working/timeEmulator --core gameboy --rom /absolute/game.gb --mod /tmp/time-telemetry-mod
```

Use `--target gamegear` for a Game Gear package and the emulator's normal Game
Gear launch options. Packages pin the supplied ROM SHA-256; no guest patches,
RAM regions, or trampolines are installed. The build option defaults to OFF.

The emulation callback copies bounded identity strings and counters into a
preallocated two-entry SPSC queue; full queues drop new samples. The worker drains
to the newest queued sample. It never reads guest memory or calls host functions.
Generation changes discard stale queued samples. Reset/restore invalidate the
pending generation; the next observation supplies authoritative machine identity.

Only the worker performs IPC. It resolves the unique frontend app ID via bounded
`window-list` pagination and registers against the compositor window ID. Requests
use the display-scoped socket under `XDG_RUNTIME_DIR`, nonblocking descriptors,
a 300 ms total deadline per request, a 65536-byte response cap, and stop checks
every 10 ms. Failed requests back off for one second; unchanged samples are not
republished. Idle leases renew every five seconds. On failure or shutdown, any
abandoned lease expires within River's 15-second lease period. An already in-flight
request may finish across a generation transition; the next publication replaces
it. Missing River does not prevent emulation or delay module destruction.

`smoke-river-xmb-telemetry` loads the real module with synthetic observations and
a private UNIX server. It checks owned identity copies, window targeting, token
use, generation replacement, duplicate suppression, malformed observations, queue
bounds, unavailable-endpoint saturation, delayed endpoint recovery, and retirement
with a stalled peer. Live River acceptance is
separate and requires launching a packaged ROM in the desktop session.
