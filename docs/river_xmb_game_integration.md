# River XMB game presentation

Game Boy sessions publish a window-scoped River XMB context through the
newline-delimited UNIX-socket protocol. The GLFW Wayland window receives a
unique `timeEmulator-<instance>` app ID through either the SDL or GLFW frontend.
The IPC worker retries `game-register`
until River returns that window's compositor identifier and owner token. Every
game update carries the token; the worker renews the 15-second lease every five
seconds while idle. Window close, explicit clear, and lease expiry remove the
owner. Each River output displays its selected game window's state; another
window's selection restores the ordinary scene. The context is presentation-only
and does not change emulator window geometry.

The small schema is:

```json
{
  "emulator": "T.I.M.E.",
  "platform": "Game Boy",
  "game": "Game Boy",
  "title": "Game Boy",
  "presentation": "gameboy",
  "artwork_key": "gameboy",
  "visualizer": "off",
  "visualizer_color": "#8bac0f"
}
```

Pokémon Red header titles select `presentation: "pokemon"` with
`artwork_key: "pokemon-red"`. Real RAM decoding additionally requires the
English retail 1 MiB ROM with SHA-1
`ea9bcae617fdf159b045185467ae58b2e4a48b9a`; a matching title alone is insufficient.

Recognized Red sessions publish real location, party nicknames/levels/current
and maximum HP, badges, and in-game play time in seconds. The emulation lane
captures an owned WRAM snapshot at up to 4 Hz; only serialized data reaches the
bounded IPC worker. Unsupported or modified ROMs retain presentation without
real telemetry. `--river-xmb-simulated` enables demo data as their fallback;
recognized Red always uses real telemetry even with that flag.

See the [telemetry contract](../.internal/docs/pokemon-red-river-telemetry.md)
for identity checks, WRAM fields, invalid-state handling, and test scope.

[Documentation index](README.md)
