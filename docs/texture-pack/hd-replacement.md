# HD Texture Replacement

## Overview

HD texture replacement allows texture pack authors to provide higher-resolution replacement images for Game Boy tiles. When enabled, the emulator outputs frames at a scaled resolution (e.g., 2x or 4x) while preserving the original tile layout and behavior.

## Configuration

### Enabling HD Output

There are two ways to enable HD texture replacement:

#### 1. CLI Flag

Pass `--hd-scale` followed by an integer value (1-8):

```bash
timeEmulator --rom game.gb --hd-scale 2
```

The CLI value is clamped to the range [1, 8]. A value of 0 or 1 disables HD upscaling.

#### 2. Config File

Set `video.hd_scale` in your emulator configuration file:

```ini
[video]
hd_scale = 2
```

**Note**: The config-file parser rejects values outside [1, 8] with an error; only the CLI flag clamps out-of-bounds values.

### Scale Bounds

- Minimum: 1 (no upscaling, standard behavior)
- Maximum: 8 (output frames up to 8x the canonical resolution)

### Frame Dimensions

When `hdScale > 1`, output frames are scaled by the factor:

| Canonical | hdScale | Output |
|-----------|---------|--------|
| 160×144   | 2       | 320×288 |
| 160×144   | 4       | 640×576 |
| 160×144   | 8       | 1280×1152 |

The engine automatically propagates the effective scale to the frontend presenter, which creates windows at the scaled dimensions.

### Stability Guarantees

- **No silent changes**: The frame scale is determined at configuration time. It does not change asynchronously as images resolve.
- **Bounded**: All scale values are validated and clamped to prevent frame dimension overflow.
- **Deterministic**: Given the same configuration, the output scale is always the same.

## Texture Pack Authoring

### Image Dimensions

Replacement images should have dimensions that are multiples of the canonical tile size (8×8 pixels):

| Scale | Recommended Tile Size | Example File |
|-------|----------------------|--------------|
| 1x    | 8×8                  | `tile.png`   |
| 2x    | 16×16                | `tile_hd.png` |
| 4x    | 32×32                | `tile_4k.png` |

### Pack Manifest Structure

```json
{
  "schemaVersion": 1,
  "id": "my-hd-pack.gb",
  "name": "My HD Texture Pack",
  "targets": ["gameboy"],
  "rules": [
    {
      "match": {
        "kind": "Tile",
        "decodedHash": "<8-digit-hash>",
        "width": 8,
        "height": 8
      },
      "replace": {
        "image": "images/tile_16x16.png"
      }
    }
  ]
}
```

### Match Rules

Use the same match rules as standard texture packs:

- `kind` — Resource type (`Tile`, `Sprite`, etc.)
- `decodedHash` — Content hash of the decoded tile
- `width` / `height` — Dimensions in canonical pixels (always 8 for tiles)

Under the default policy, the engine accepts replacement images that do not match the expected dimensions and resamples them. Authors should use multiples of 8×8 for proper alignment; the `exact` scale policy rejects mismatched dimensions.

### Transparency Handling

Replacement images use the same alpha channel semantics as standard texture packs:

- **Fully transparent pixels** (alpha = 0) — show through to background
- **Opaque pixels** (alpha = 255) — replace the original tile
- **Partial transparency** — blended based on source palette

### Priority and Layering

HD replacements preserve all existing priority semantics:

- Background/sprite priority is unchanged
- Window tiles are processed independently
- Sprite overlays remain in front of background

## Rendering Behavior

### Pixel Mapping (Integer q-Space)

HD texture replacement uses integer output-resource coordinates to ensure exact 1:1 mapping between output pixels and replacement texels when dimensions match.

For canonical semantic coordinates `sx` and `sy` and HD scale factor `hdScale`:

```text
qx = sx * hdScale + subX
qy = sy * hdScale + subY
```

where `subX = ox % hdScale` and `subY = oy % hdScale` are the sub-pixel indices within each HD block.

When replacement dimensions equal `source_dimensions × hdScale`, q-space coordinates index the replacement directly, guaranteeing all source texels are used with no duplication or skipping.

**Example (2x scale, 8×8 source → 16×16 replacement):**

```text
Canonical (8×8)          HD Output (16×16) with replacement
┌──────┐                ┌──────────────────┐
│ A B  │   →            │ A₁A₂ B₁B₂        │
│ C D  │              │ C₁C₂ D₁D₂        │
└──────┘              └──────────────────┘
(replacement has 256 distinct texels for 16×16)
```

Each output pixel maps to exactly one replacement texel. For a 16×16 replacement, all 256 texels are distinct and used.

### Replacement Sampling

When a tile has a replacement:

1. The replacement image is sampled using the effective source dimensions (`descriptor.width × hdScale`)
2. Each output pixel maps to a specific location in the replacement image via q-space coordinates
3. **Nearest-neighbor** interpolation preserves sharp pixel art (default)
4. **Linear** interpolation is available via `filterPolicy: "linear"` in the manifest
5. All source texels are used — no duplication or sampling artifacts when dimensions match

When a tile has no replacement:

1. The original canonical pixel is replicated `hdScale × hdScale` times
2. This ensures mixed replaced/unreplaced content aligns correctly

### Scale Policies

- **`exact`**: Requires replacement dimensions to exactly match `source × hdScale`; rejects mismatched dimensions
- **`crop`**: Crops the replacement image to fit the source dimensions, preserving aspect ratio based on anchor point

### Filter Policies

- **`nearest`** (default): Nearest-neighbor sampling, preserves sharp edges
- **`linear`**: Bilinear interpolation for smoother results

### Example: Mixed Content

With 2x HD scale and a 16×8 frame (2 tiles wide, 1 tall):

```text
Tile 0 (replaced)    Tile 1 (unreplaced)
┌──────────────┬──────────────┐
│ R₁R₂ G₁G₂   │ O₁O₂ O₃O₄   │
│ R₃R₄ G₃G₄   │ O₅O₆ O₇O₈   │
│ B₁B₂ Y₁Y₂   │ O₉O₁₀ O₁₁O₁₂│
│ B₃B₄ Y₃Y₄   │ O₁₃O₁₄O₁₅O₁₆│
└──────────────┴──────────────┘
```

Each canonical pixel becomes a 2×2 block in the output. The replaced tile uses its replacement image with all texels distinct, while the unreplaced tile uses nearest-neighbor replication of the original 8×8 content.

### Palette Replacement

When a replacement specifies a palette override (`palette` key), HD sampling maps q-space coordinates back to canonical tile coordinates before indexing the palette:

```cpp
canonX = floor(sampleX * descriptor.width / effectiveSourceWidth)
canonY = floor(sampleY * descriptor.height / effectiveSourceHeight)
```

This preserves palette semantics across all HD scales.

## Performance Considerations

### Memory Usage

HD frames consume `hdScale²` times more memory than canonical frames:

| Scale | Frame Size | Memory (RGBA) |
|-------|-----------|---------------|
| 1x    | 160×144   | 92 KB         |
| 2x    | 320×288   | 369 KB        |
| 4x    | 640×576   | 1.48 MB       |

### CPU Impact

The HD path adds:
- One lookup pass per pixel (same as standard override)
- One upscale pass (linear in output pixel count)

For 2x scale on a 160×144 frame, the output contains 92,160 pixels, or 69,120 additional pixels compared with canonical resolution — typically sub-millisecond on modern hardware.

### GPU Upload

Frontends upload the full HD frame each present cycle. Consider:
- Using texture atlases for multiple replacements
- Caching decoded replacement images
- Skipping upload when frame content hasn't changed

## Known Limitations

1. **Fixed scale per session** — `hdScale` cannot change without reconfiguration
2. **No per-tile scaling** — all tiles use the same scale factor
3. **Canvas alignment** — replacement images should have dimensions that are multiples of 8×8 for proper grid alignment

## Testing

Run the HD texture replacement smoke tests:

```bash
cmake -S . -B build-working
cmake --build build-working --target time-smoke-hd-texture-replacement -j4
ctest --test-dir build-working -R smoke-hd-texture-replacement --output-on-failure
```

The test suite covers:
- HD scale propagation (1x, 2x, 4x)
- Mixed replaced/unreplaced content alignment
- Exact q-to-q mapping with 256 unique texels
- Blank frame upscaling
- hdScale=0 clamping behavior
- Capture/observation in HD mode
- ReplacePalette in HD mode
- Exact policy accept/reject against effective native dimensions
- Mismatched replacement dimensions with nearest and linear filtering
- Production bootstrap wiring
