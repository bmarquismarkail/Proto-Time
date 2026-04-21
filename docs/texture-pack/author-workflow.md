# Texture Pack Author Workflow

Date: 2026-04-20

This document describes the current pack-authoring workflow for Proto-Time visual override packs. It reflects the codebase behavior as implemented today, not the full future design in [design.md](./design.md).

## Current Scope

Visual packs are presentation overrides. They replace decoded visual resources after emulation has produced canonical visual data; they do not change machine-visible state, timing, memory, or CPU behavior.

The current Game Boy path can observe decoded visual resources, dump them for authors, load `pack.json` manifests, resolve replacement PNGs, and optionally hot reload changed manifests or assets while the emulator is running.

## Pack Layout

A pack is a directory with a JSON manifest and replacement images:

```text
packs/
  example-gb-ui/
    pack.json
    images/
      font_A.png
      player_idle.png
```

`pack.json` is loaded directly with `--visual-pack <path>`, so it does not need to be named by convention, but `pack.json` is the recommended name.

For a tracked repo example, use [sample-pack/](./sample-pack/).

## Minimal Manifest

```json
{
  "schemaVersion": 1,
  "id": "example.gb-ui",
  "name": "Example GB UI",
  "targets": ["gameboy"],
  "priority": 100,
  "rules": [
    {
      "match": {
        "kind": "Tile",
        "decodedHash": "0x1234567890abcdef",
        "width": 8,
        "height": 8
      },
      "replace": {
        "image": "images/font_A.png"
      }
    }
  ]
}
```

Required manifest fields:

- `schemaVersion`: must be `1`.
- `id`: stable pack identifier.
- `target` or `targets`: `target` is a single machine id; `targets` can list multiple machine ids and matches any listed target.
- `rules`: array of replacement rules.

Required rule fields:

- `match.kind`: currently `Tile`, `Sprite`, or `BackgroundTile`.
- `match.sourceHash`, `match.decodedHash`, or `match.paletteAwareHash`.
- `replace.image` or `replace.palette`.

Optional match fields:

- `machineId` or `machine`: restricts the rule to a machine id such as `gameboy`.
- `decodedFormat`: currently `Indexed2` or `Rgba8888`.
- `semanticLabel`: matches the source metadata label when the core provides one.
- `sourceHash`: matches canonical source bytes when the core provides them.
- `sourceBank`: matches the source metadata bank value.
- `sourceAddress`: matches the source metadata address; capture mode writes this as a hexadecimal string.
- `tileIndex` or `sourceIndex`: matches the source metadata index.
- `decodedHash`: matches normalized decoded pixels.
- `paletteHash`: restricts a decoded or palette-aware match to a palette hash.
- `paletteAwareHash`: matches normalized decoded pixels plus palette context.
- `paletteRegister`: matches the palette register name, such as `BGP`, `OBP0`, or `OBP1`.
- `paletteValue`: matches the source metadata palette value; capture mode writes this as a hexadecimal string.
- `width` and `height`: restrict by decoded dimensions.

Optional replacement fields:

- `palette`: a 4-entry ARGB palette override for indexed resources. Entries accept numeric JSON values or `0x`-prefixed hexadecimal strings.
- `layers`: an ordered list of replacement PNGs composited front-to-back with straight alpha-over.
- `animation`: an ordered frame sequence with `{ "frameDuration": <milliseconds>, "frames": ["frame0.png", "frame1.png"] }`.
- `effects`: a structured CPU-side post-effect list. Supported kinds are `invert`, `grayscale`, `multiply`, and `alphaScale`.
- `script`: a declarative command list that lowers to the same CPU-side post-effect pipeline. Supported commands are `invert`, `grayscale`, `multiply 0xAARRGGBB`, and `alpha-scale <0.0..1.0>`.
- `slicing`: an optional source rectangle inside a larger replacement image, expressed as `{ "x": ..., "y": ..., "width": ..., "height": ... }`.
- `transform`: optional image-space transforms, currently `{ "flipX": true|false, "flipY": true|false, "rotate": 0|90|180|270 }`.
- `scalePolicy`: empty or `allow-any` scales the full replacement image to the source resource size; `exact` only applies images with matching dimensions; `crop` samples a source-sized crop from the replacement image.
- `filterPolicy`: empty, `nearest`, and `preserve-hard-edges` use nearest-neighbor sampling; `linear` uses bilinear sampling.
- `anchor`: used with `scalePolicy: "crop"`; supported values are `top-left` or empty, `center`/`middle`, and `bottom-right`/`right`/`bottom`.

Replacement source modes are mutually exclusive:

- `image`: one static replacement image
- `layers`: one composited multi-image replacement
- `animation`: one time-varying replacement sequence
- `palette`: indexed palette remap

`effects` and `script` are optional post-processing stages that run after the replacement source mode resolves to a sampled ARGB pixel. `script` is not arbitrary code execution; it is only a compact syntax for the supported CPU-side effects above.

For Game Boy tile and sprite resources, policy sampling is applied per 8x8 decoded resource. If `slicing` is present, the existing scale/filter/anchor policies operate inside the sliced region instead of the full replacement image. If `transform` is present, it is applied after slicing and before final sampling. `layers` use the same sampling path per layer, then alpha-compose the sampled pixels. `animation` selects the active frame from the current visual generation and then uses the same sampling path as a static image.

Current Game Boy semantic labels are renderer-context labels, not game-specific guesses:

- `tile_data`: direct tile extraction without renderer context
- `sprite_obj`: sprite/object tile
- `background_tile_unsigned`
- `background_tile_signed`
- `window_tile_unsigned`
- `window_tile_signed`

## Capture Workflow

Use capture mode to generate source images and manifest stubs:

```bash
build-working/timeEmulator --rom path/to/game.gb --visual-capture captures/gameboy --steps 2000 --headless
```

Capture mode writes:

- `captures/gameboy/pack.json`: a directly loadable manifest that points back at captured PNGs.
- `captures/gameboy/manifest.stub.json`: a richer authoring stub with metadata for each observed resource.
- `captures/gameboy/capture_metadata.json`: metadata-only summary.
- `captures/gameboy/author_report.txt`: a compact author-facing report with override counters, capture counters, and the most frequently observed resources.
- Resource directories named by kind, such as `Tile/`, `Sprite/`, and `BackgroundTile/`.

Captured filenames include the resource kind, decoded content hash, and dimensions:

```text
Tile/Tile_0x1234567890abcdef_8x8.png
```

The capture writer deduplicates identical descriptor keys during a run. The emulator prints the same author report at shutdown, including override diagnostics, unique resources dumped, duplicates skipped, and the most frequently observed resources.

## Editing a Pack

1. Run capture mode at the screen or gameplay state that contains the resource you want to replace.
2. Inspect `manifest.stub.json` and `capture_metadata.json` to find the relevant resource. Prefer entries with the intended `kind`, `semanticLabel`, dimensions, `sourceHash`, `decodedHash`, and, when needed, `paletteAwareHash`.
3. Copy the captured `pack.json` into a pack directory, or copy individual rules into an existing pack.
4. Put replacement PNGs under the pack directory, commonly in `images/`.
5. Change each rule's `replace.image` to the authored replacement image path.
6. Test the pack:

```bash
build-working/timeEmulator --rom path/to/game.gb --visual-pack packs/example-gb-ui/pack.json
```

Use repeatable `--visual-pack` flags to load multiple packs:

```bash
build-working/timeEmulator \
  --rom path/to/game.gb \
  --visual-pack packs/base/pack.json \
  --visual-pack packs/local-edits/pack.json
```

Legacy aliases are still accepted:

- `--texture-pack <path>` for `--visual-pack <path>`.
- `--dump-visual-resources <dir>` for `--visual-capture <dir>`.

## Config File Usage

Visual pack options can also be configured in an INI-style config:

```ini
[emulator]
rom = path/to/game.gb

[visual]
pack = packs/base/pack.json
texture_pack = packs/local-edits/pack.json
capture = captures/gameboy
reload = true
```

The `visual.pack`, `visual.visual_pack`, and `visual.texture_pack` keys are repeatable. Relative paths are resolved from the config file directory.

## Hash Contract

Manifest schema v1 uses the current 64-bit FNV-1a helpers in `VisualTypes.hpp`:

- `decodedHash` is the hash of decoded format, width, height, and decoded pixel bytes.
- `sourceHash` is the hash of canonical source bytes when a core provides them. For Game Boy tile resources, this is the 16 raw tile bytes from VRAM.
- `paletteHash` is the hash of the core-provided palette value.
- `paletteAwareHash` combines `decodedHash` and `paletteHash`.

Persist hashes as 16-digit lowercase hexadecimal strings with a `0x` prefix, matching the values written by capture mode.

## Hot Reload

Enable reload polling while authoring:

```bash
build-working/timeEmulator \
  --rom path/to/game.gb \
  --visual-pack packs/example-gb-ui/pack.json \
  --visual-pack-reload
```

Reload checks watch each loaded manifest and each replacement image referenced by loaded rules. When a manifest or watched asset changes, the service reloads the affected packs, clears resolution/image caches, and increments the visual generation.

If reload fails, the previous loaded packs remain active. The emulator prints a warning with the service error instead of replacing good state with a failed reload. Repeated identical reload failures are suppressed until the warning changes or a reload succeeds, and the author report includes the suppressed warning count.

## Matching and Priority

The service finds every loaded rule that matches the observed descriptor, then chooses:

1. Highest computed specificity.
2. Highest pack priority.
3. Earliest rule order in the manifest.

Specificity increases when a rule includes stronger constraints such as machine id, palette-aware hash, semantic label, palette hash, decoded format, dimensions, or decoded hash.

Rules that do not match simply miss and the original decoded resource is used. Missing replacement images are counted as diagnostics during manifest load; if such a rule later resolves, loading the replacement image fails and the original resource is used.

## Image and Manifest Limits

Current safety limits:

- Manifest size: 1 MiB.
- Manifest rules: 1024.
- PNG file size: 8 MiB.
- Replacement image dimension: 2048 pixels on either axis.
- Inflated replacement image data: 16 MiB, assuming 4 bytes per pixel.
- Replacement image cache budget: 64 MiB.

The replacement image cache uses LRU eviction. When a newly loaded replacement would exceed the cache budget, older cached images are evicted instead of rejecting the new replacement immediately.

Replacement PNG loading currently supports non-interlaced 8-bit RGB and RGBA PNGs. Other PNG formats are rejected.

## V1 Boundary

The current v1 intentionally stops at CPU-side declarative effects. It does not support:

- GPU shader languages
- arbitrary scripting or pack-provided code execution
- plugin-defined visual effect execution
- filesystem event-based reload notifications

## Troubleshooting

If a pack does not apply:

- Confirm the pack loaded without an emulator startup error.
- Check that `target` is `gameboy` or that `targets` includes `gameboy`.
- Check `kind`, `width`, `height`, and hash values against `capture_metadata.json`.
- Use `sourceHash` when the raw source bytes are the intended identity.
- Use `sourceAddress`, `tileIndex`, `paletteRegister`, or `paletteValue` when the same resource bytes need to be constrained to a specific source context.
- Use `paletteAwareHash` when the same decoded pixels need different replacements under different palettes.
- Confirm `replace.image` is relative to the manifest directory and points at an existing RGB/RGBA PNG.
- Run with `--visual-pack-reload` while editing, but watch for reload warnings; a failed reload preserves the last good pack.

If too many resources are captured, use a shorter `--steps` count or navigate to the specific screen before starting a capture run.
