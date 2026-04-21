# Texture Pack / Visual Override Design for the Emulator Framework

Date: 2026-04-17

## Goal

Introduce a framework-level visual override system that allows texture packs, sprite replacements, tile replacements, palette-aware overrides, and resource capture workflows without coupling the feature to any single core or frontend.

The feature should fit the current architecture direction where:

- `Machine` is the authoritative runtime owner.
- shared cross-core abstractions belong in the machine layer.
- frontend plugins should not own machine-specific concepts.
- typed internal I/O observation is preferred over ad hoc frontend hooks.

## Why this belongs in the framework layer

The existing project direction already moves common abstractions into the machine layer rather than leaving them trapped in SDL- or core-specific code. That is the same architectural move this feature needs. The machine-hosted runtime model makes `Machine` the correct owner for shared services and policies, while executor and plugin boundaries remain narrow and explicit. See the machine-owned runtime direction in the time-space design and the typed internal I/O plugin model for machine-driven observation and dispatch.

## Design principles

1. **Machine truth, presentation override**
   - Emulation semantics remain authoritative in the core/machine.
   - Texture packs change presentation by default, not machine-visible logic.

2. **Framework-level ownership**
   - Override lookup and pack loading belong in the machine/host layer, not only in SDL.

3. **Resource identity over transient memory locations**
   - Prefer stable content-derived identity and normalized descriptors over VRAM addresses or object slots.

4. **Push small events, pull big data**
   - Reuse the same philosophy as the internal I/O plugin design: push typed observation events, pull resource bytes/pixels through views and descriptors.

5. **Core-agnostic first, core-specific optional enrichment**
   - Every core can expose a baseline resource descriptor.
   - Individual cores may add labels or context hints when helpful.

## High-level architecture

## New machine-layer concepts

### `VisualResourceKind`

Machine-owned enum describing what kind of visual resource is being observed.

Initial values:

- `Tile`
- `Sprite`
- `BackgroundCell`
- `Texture`
- `FramebufferRegion`
- `Palette`
- `FontGlyph`
- `Unknown`

### `PixelFormat`

Machine-owned enum for normalized decoded formats.

Initial values:

- `Indexed1`
- `Indexed2`
- `Indexed4`
- `Indexed8`
- `Rgb565`
- `Rgba8888`

### `VisualResourceDescriptor`

Machine-owned struct describing a visual resource in a normalized way.

Suggested fields:

- `VisualResourceKind kind`
- `uint32_t width`
- `uint32_t height`
- `PixelFormat sourceFormat`
- `PixelFormat decodedFormat`
- `uint32_t machineResourceId`
- `uint64_t contentHash`
- `uint64_t paletteAwareHash`
- `uint64_t paletteHash`
- `std::string_view semanticLabel` or optional owned string
- optional machine-specific metadata map or typed extension payload

The descriptor is intentionally a small identity surface. It should be cheap to emit in events and suitable for caching.

### `DecodedVisualResource`

Machine-owned immutable view or owned blob for normalized decoded visual data.

Suggested fields:

- descriptor
- decoded pixel bytes
- stride
- optional palette bytes
- optional source-bytes hash only

### `VisualOverride`

Resolved replacement or transform decision.

Suggested modes:

- `None`
- `ReplaceImage`
- `ReplacePalette`
- `ApplyTransform`
- `CompositeLayer`

### `VisualOverrideService`

Machine-owned service that:

- manages loaded packs
- indexes match rules
- resolves overrides for a `VisualResourceDescriptor`
- caches decoded replacement assets
- records hit/miss telemetry
- optionally emits authoring diagnostics

### `VisualPackRepository`

Loader/index for packs on disk.

Responsibilities:

- load manifest
- validate assets
- index rules
- expose immutable pack metadata to `VisualOverrideService`

## Machine integration

Add a machine-owned visual service surface, analogous in spirit to the audio service direction where shared functionality lives on `Machine` rather than inside the SDL frontend. The machine host already wants centralized service ownership and typed observation paths, which makes this feature a good fit.

Suggested additions:

- `Machine::visualOverrideService()`
- `MachineView::visualOverrideService()`
- optional `Machine::setVisualOverrideService(...)` if service swapping is desired during controlled lifecycle windows

This should follow the same ownership instinct as the audio-service direction: default non-null service, machine-owned lifetime, frontend consumption through `MachineView` rather than frontend-local ownership.

## Observation path

The internal I/O design already recommends typed categories, machine-driven timing, a central `PluginManager`, and “push events, pull data.” The visual override system should follow that model rather than inventing a side channel.

### New I/O category

Either:

- extend `video` with visual-resource observation events, or
- add a more specific subcategory under `video`

### New event types

Suggested events:

- `VisualResourceObserved`
- `VisualResourceDecoded`
- `VisualOverrideResolved`
- `VisualPackMiss`
- `FrameCompositionStarted`
- `FrameCompositionCompleted`

Event payload should stay small:

- descriptor IDs
- frame number / step counter
- timing metadata
- reason code

Actual pixel payloads should be pulled via read-only views or service APIs, consistent with the I/O plugin design.

## Where replacement happens

Default replacement point:

1. Core or machine exposes raw graphics state.
2. Framework video path decodes source data into a canonical decoded resource.
3. `VisualOverrideService` matches the descriptor/hash.
4. Renderer or presentation path uses the replacement if available.

This keeps machine-specific bitplane decoding and memory layout logic out of texture-pack authorship and avoids forcing the pack system to understand each console’s raw VRAM format.

## Identity and matching

## Primary rule: do not key mainly on VRAM addresses

VRAM addresses, OAM slots, and transient object indices are useful diagnostics but poor primary identities. They are too machine-specific and too unstable across frames or content streaming.

## Matching modes

Support these from the start:

1. **Source-bytes hash**
   - Match on canonical raw source bytes when a core can produce them reliably.

2. **Decoded-pixels hash**
   - Match on normalized decoded pixels without palette.

3. **Palette-aware decoded hash**
   - Match on normalized decoded pixels plus palette context.

4. **Context-constrained match**
   - Match on one of the above plus descriptor constraints such as kind, dimensions, bank, or semantic label.

## Hash recommendations

For the first phase:

- use one stable 64-bit fast hash for runtime lookup
- optionally persist a wider digest in authoring metadata if desired later

Keep the hash contract explicit and versioned in the manifest so future changes do not silently break packs.

## Pack format

Use a simple directory + manifest structure.

Example:

```text
packs/
  hd-font/
    pack.json
    images/
      tile_001.png
      player_idle.png
    metadata/
      notes.txt
```

### `pack.json` sketch

```json
{
  "schemaVersion": 1,
  "id": "example.gb-hd-ui",
  "name": "Example GB HD UI",
  "targets": ["gameboy"],
  "priority": 100,
  "author": "Brandon",
  "rules": [
    {
      "match": {
        "kind": "Tile",
        "decodedHash": "0x1234567890abcdef",
        "width": 8,
        "height": 8,
        "semanticLabel": "font_glyph"
      },
      "replace": {
        "image": "images/font_A.png",
        "scalePolicy": "allow-any",
        "anchor": "top-left"
      }
    },
    {
      "match": {
        "kind": "Sprite",
        "paletteAwareHash": "0xfedcba0987654321"
      },
      "replace": {
        "image": "images/player_idle.png"
      }
    }
  ]
}
```

## Rule semantics

Each rule should support:

- required match fields
- optional narrowing constraints
- replacement action
- optional priority override

### Suggested match fields

- `kind`
- `sourceHash`
- `decodedHash`
- `paletteAwareHash`
- `width`
- `height`
- `decodedFormat`
- `semanticLabel`
- machine/core name
- optional machine-specific metadata constraints

### Suggested replacement fields

- `image`
- `palette`
- `scalePolicy`
- `filterPolicy`
- `anchor`
- `slicing`
- `animationGroup`

## Ambiguity policy

Define this upfront:

- exact most-specific rule wins
- if multiple equally specific rules match, log ambiguity and either:
  - use highest pack priority, then
  - first manifest order within that pack
- if replacement loading fails, fall back to original resource
- incomplete packs are valid

The system should never fail hard merely because a replacement is missing or ambiguous.

## Game Boy first implementation shape

The first serious reference machine is already Game Boy, and the framework direction emphasizes machine-owned execution and typed I/O observation. That makes Game Boy the right first target for this feature as well.

### Initial Game Boy resource types

- background/window tiles
- sprite tiles / objects
- palettes where relevant to the selected mode
- optional font/UI glyph labeling later

### First extraction targets

- decoded 8x8 tile pixels
- sprite tile pixels after flip/orientation normalization policy is chosen
- palette context where applicable

### Useful Game Boy-specific metadata

- VRAM bank
- tile index
- OAM object index
- LCDC mode/source context
- palette register values
- optional label from the core like `player_sprite`, `hud_digit`, `font_glyph`

These metadata are supplemental, not primary identity.

## Service boundaries

## Core responsibilities

- expose enough graphics state for extraction
- optionally expose semantic labels or context hints
- remain authoritative about emulation semantics and composition rules

## Framework responsibilities

- normalize resource descriptors
- compute identity hashes
- load and validate packs
- resolve overrides
- cache replacements
- provide capture/dump tooling

## Frontend responsibilities

- present the resolved resource or final composed output
- optionally expose pack statistics/overlays to the user
- avoid becoming the sole owner of the override database

This division matches the project’s broader direction: machine-owned abstractions, typed observation, and frontends depending on stable machine contracts rather than console-specific frontend APIs.

## Authoring workflow

Build capture tooling before chasing a rich runtime feature set.

### `--dump-visual-resources` mode

Suggested behavior:

- observe visual resources during execution
- write decoded source images to disk
- emit manifest stubs with hashes and metadata
- group repeated observations
- optionally generate a summary report of most-seen assets

Example output:

```text
capture/gameboy/
  manifest.stub.json
  tiles/
    001_0x1234abcd.png
    002_0xdeadbeef.png
  sprites/
    001_0x99887766.png
```

This authoring path matters as much as runtime support. Without it, the feature is theoretically elegant and practically miserable.

## Caching strategy

Use separate caches for:

- decoded original resources
- replacement asset loads/decodes
- match results

Suggested keys:

- original decoded resource cache: descriptor content hash + descriptor-relevant context
- replacement cache: pack ID + asset path
- resolved override cache: active-pack-set generation + descriptor hash tuple

Invalidate caches when:

- active packs change
- manifest reload occurs
- renderer/backend presentation constraints change in ways that affect scaling or filtering

## Hot reload

Not required for phase 1, but desirable in phase 2.

Suggested policy:

- manifest and asset timestamp watcher in development mode only at first
- pack generation counter invalidates resolved override cache
- failed reload should preserve last good state

## Safety and limits

Because packs are untrusted content:

- validate image formats
- cap maximum dimensions
- cap total decoded memory budget
- reject pathological manifests
- keep scripting out of phase 1
- make any future script support explicitly opt-in and sandboxed

## Testing strategy

Follow the same layered testing instinct already present in the project docs.

### Unit tests

- hash generation for normalized descriptors
- manifest parsing and validation
- rule specificity ordering
- ambiguity resolution
- cache invalidation behavior

### Machine/video tests

- Game Boy tile extraction produces stable descriptors
- palette-aware and palette-unaware hashes behave as intended
- replacement lookup does not mutate machine-visible state

### Smoke tests

- load a minimal pack for Game Boy tiles
- verify replacement hit occurs
- verify missing replacement falls back cleanly
- verify capture mode emits deterministic manifest stubs

### Conformance-style tests

- baseline rendering without pack vs rendering with pack should preserve machine-visible semantics
- optionally compare frame composition metadata where relevant

## Proposed file layout

Suggested initial files:

```text
machine/
  video/
    VisualTypes.hpp
    VisualHash.hpp
    VisualResourceDescriptor.hpp
    DecodedVisualResource.hpp
    VisualOverrideService.hpp
    VisualPackRepository.hpp
    VisualPackManifest.hpp
    VisualCaptureService.hpp

machine/plugins/
  IoEventTypes.hpp            # if event enums/types are centralized here

cores/gameboy/
  video/
    GameBoyVisualExtractor.hpp
    GameBoyVisualExtractor.cpp
    GameBoyVisualLabels.hpp   # optional later

tests/
  smoke_visual_override_gameboy.cpp
  visual_manifest_tests.cpp
  visual_hash_tests.cpp
```

## Staged implementation plan

## Stage 1: Foundational resource replacement

Goal: working Game Boy tile/sprite replacement with capture tooling.

Deliver:

- machine-owned visual types
- Game Boy visual extractor
- descriptor hashing
- simple `pack.json` loader
- PNG replacement loading
- replacement at presentation time
- dump/capture mode
- smoke test coverage

## Stage 2: Palette-aware matching and diagnostics

Deliver:

- palette-aware hash mode
- richer pack validation
- hit/miss logging and overlay stats
- optional semantic labels from core
- pack priority and ambiguity diagnostics

## Stage 3: Quality-of-life authoring features

Deliver:

- hot reload in dev mode
- richer manifest fields
- animation grouping
- targeted replacement of UI/font/player groups

## Stage 4: Advanced overrides

Deliver:

- transform rules
- layered composition overrides
- optional shader/post-process integration
- experimental scripted authoring extensions if still desired

## Recommended first milestone for this repo

Build a minimal Game Boy-oriented `VisualOverrideService` in the machine layer, with a Game Boy extractor that can emit normalized descriptors for decoded 8x8 tiles and sprites, a disk-backed pack manifest keyed by decoded hashes, and a capture mode that dumps observed resources and manifest stubs.

That gives the framework:

- a clean machine-owned abstraction
- a useful authoring workflow
- an SDL-independent service boundary
- a feature that can grow to other machines without redesigning the entire renderer

## Rationale against frontend-only design

A frontend-only texture replacement system would conflict with the architecture direction already established in the project:

- common abstractions should move into the machine layer rather than staying SDL-specific
- typed observation belongs in machine-driven plugin/event infrastructure
- the machine host should own shared services rather than duplicate them per frontend

So the recommendation is clear: texture packs should be implemented as a machine/host visual override service with typed video observation and frontend-agnostic lookup, not as a one-off SDL renderer trick.

## References to current project direction

This design aligns with:

- machine-owned shared abstractions rather than frontend-owned console-specific contracts fileciteturn0file0
- machine-owned runtime and service direction with frontends/plugins depending on machine contracts rather than owning machine semantics fileciteturn0file1
- typed machine-driven I/O observation and the “push events, pull data” model for plugins and tooling fileciteturn0file2
- machine-owned shared service ownership patterns already being introduced for audio fileciteturn0file4
