# River XMB game presentation

Game Boy sessions publish a display-scoped River XMB context through the
newline-delimited UNIX-socket protocol. The context is presentation-only and
does not change emulator window geometry.

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

Pokémon Red cartridges are recognized from the normalized Game Boy header
title and use `presentation: "pokemon"` with `artwork_key: "pokemon-red"`.
`--river-xmb-simulated` enables the proof-of-concept telemetry stream while
the authoritative Pokémon map, party, HP, badges, and play-time snapshot API
is still being developed.
