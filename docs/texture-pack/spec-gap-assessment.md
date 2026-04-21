# Texture Pack Spec Gap Assessment

Date: 2026-04-20

This assessment compares the current implementation with [design.md](./design.md).

## Implemented

- Machine-owned visual override service exposed through `Machine`.
- Game Boy decoded visual resource extraction for tile, sprite, and background tile presentation paths.
- Stable 64-bit source-byte, decoded content, palette, and palette-aware hashes.
- Manifest loading from JSON with validated schema v1, pack id, name, target/targets, priority, match rules, and replacement image paths.
- Match constraints for kind, machine id, decoded format, semantic label, source hash, captured source metadata, decoded hash, palette hash, palette-aware hash, width, and height.
- Specificity-based rule selection with pack priority and manifest order tie-breaks.
- Replacement image mode, indexed palette replacement mode, layered composition, animation groups, image slicing, simple image transforms, and CPU-side declarative post effects/script commands.
- Replacement PNG loading with dimension, compressed-size, inflated-size, and LRU cache eviction under the cache budget.
- Capture mode that writes decoded resource PNGs, `pack.json`, `manifest.stub.json`, and `capture_metadata.json`.
- Typed visual observation events for resource observed, resource decoded, override resolved, pack miss, and frame composition boundaries.
- Hot reload for changed pack manifests and referenced replacement images, preserving the last good pack on failed reload and suppressing repeated identical warnings.
- User-facing activation through CLI and config: `--visual-pack`, `--visual-capture`, `--visual-pack-reload`, plus legacy aliases.
- Compact author diagnostics through `author_report.txt` and shutdown summaries, including skipped rules, missing images, reload failures, capture counts, top observed resources, and semantic labels.
- Game Boy semantic labels for renderer-context resources: background/window signedness, sprite objects, and direct tile extraction.
- Focused smoke coverage for manifest behavior, capture behavior, emulator config activation, and end-to-end visual override behavior.

## Remaining Spec Work

1. Advanced programmable rendering remains intentionally out of scope.

   v1 now includes layered composition, animation groups, and CPU-side declarative post effects/script commands. What remains out of scope is a real programmable rendering surface: GPU shader languages, arbitrary script execution, and plugin-defined visual effect execution.

2. Rule priority is pack-level only.

   The design mentions specificity ordering and pack priority. Current behavior has pack priority plus manifest order, but no explicit per-rule priority or override weight. If author workflows need fine-grained ordering inside a pack, this still needs design and implementation.

3. Semantic labels are intentionally renderer-context only.

   The Game Boy extractor now emits stable labels for background/window signedness and sprite objects. Higher-level labels such as `font_glyph`, `hud_digit`, or game-specific actor names still require a separate enrichment layer and should not be inferred heuristically from raw frames in v1.

4. Hot reload is still polling-only.

   The current emulator polls during frontend service or headless idle paths. It now preserves last good state on failed reload, suppresses repeated identical warnings, and reports reload diagnostics to authors, but it still does not use filesystem notifications.

5. Cache behavior is LRU-only.

   The service now evicts least-recently-used replacement images under the fixed cache budget. It does not implement generation-aware eviction or pack-aware cache partitioning.

6. Test coverage is good for the current v1 slice but not for any future programmable surface.

   Existing tests cover the implemented manifest, capture, config, reload, replacement policy, composition, animation, transform, and effect paths. Future shader/plugin-scripting work would need a new test surface and probably a stricter capability model.

## Recommended Next Steps

1. Treat the current implementation as the v1 boundary.

   Do not grow this into GPU shaders or arbitrary scripting without a separate design pass that defines capability limits, execution model, safety constraints, and test strategy.
