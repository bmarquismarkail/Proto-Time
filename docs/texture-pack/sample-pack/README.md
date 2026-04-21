# Documentation Sample Pack

This is a canonical v1 sample pack for the Proto-Time visual override pipeline.

It is intentionally small:

- one `pack.json`
- one static replacement rule
- two replacement PNGs used during walkthrough and reload verification

The rule uses a placeholder `decodedHash` so the pack will not match a live game until you replace the captured values with real data from `--visual-capture`.

Files:

- `pack.json`: minimal schema-v1 sample manifest
- `images/tile-red.png`: default replacement image
- `images/tile-green.png`: alternate image used for hot-reload walkthroughs

To make this pack real for a game:

1. Run capture mode on the target screen.
2. Replace `decodedHash`, `width`, and `height` in `pack.json` with the captured values.
3. Replace the sample PNG with authored art.
