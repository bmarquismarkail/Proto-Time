# Texture Pack Walkthrough

This walkthrough takes the sample pack in [`sample-pack/`](./sample-pack/) from a static example to an authoring-ready starting point.

## 1. Capture a real resource

Run capture mode on the screen you want to replace:

```bash
build-working/timeEmulator \
  --rom path/to/game.gb \
  --visual-capture captures/gameboy \
  --steps 2000 \
  --headless
```

Find the target entry in:

- `captures/gameboy/pack.json`
- `captures/gameboy/manifest.stub.json`
- `captures/gameboy/capture_metadata.json`

## 2. Update the sample manifest

Open [`sample-pack/pack.json`](./sample-pack/pack.json) and replace:

- `decodedHash`
- `width`
- `height`

with the captured values for your target tile or sprite.

## 3. Replace the art

Swap [`sample-pack/images/tile-red.png`](./sample-pack/images/tile-red.png) with authored art, or change `replace.image` to another file in the pack.

## 4. Load the pack

```bash
build-working/timeEmulator \
  --rom path/to/game.gb \
  --visual-pack docs/texture-pack/sample-pack/pack.json
```

## 5. Verify hot reload

Enable reload polling:

```bash
build-working/timeEmulator \
  --rom path/to/game.gb \
  --visual-pack docs/texture-pack/sample-pack/pack.json \
  --visual-pack-reload
```

Then make one of these edits while the emulator is running:

- change `replace.image` from `images/tile-red.png` to `images/tile-green.png`
- overwrite `images/tile-red.png` with new art

The service should reload the pack without restarting the emulator.

## 6. Check diagnostics

Look at the shutdown summary or capture report:

- `author_report.txt`
- emulator shutdown output

You should see:

- rules loaded
- reload checks
- reloads succeeded
- any suppressed reload warnings

If the pack does not apply, go back to the captured hash and dimension values before changing anything else.
