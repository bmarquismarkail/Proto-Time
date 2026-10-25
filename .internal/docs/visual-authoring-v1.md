# Visual authoring and presentation extensions, version 1

This implements the approved two-core completion program's authoring extension
against the current texture-pack design. It does not change schema-1 pack
loading or authorize another machine family.

## Ownership and limits

`VisualAuthoring::Capture` contains owned resource observations, core, ROM SHA256,
video sequence, generation and optional matching CPU state. Scripted authoring
requires a paused CPU observation from the same capture. Its engine accepts no
Machine reference. Lua, Python and QuickJS use the existing sandboxed ScriptEngine
with register and RAM mutation permissions disabled. Programs report one JSON
recipe; failed programs and invalid recipes publish nothing. File automation
accepts that recipe directly. Runtime absence remains an explicit rejection.

The capture budget is 4 MiB; recipes/programs are at most 64 KiB, JSON nesting is
at most 32, there are at most 256 authored resources and 64 images per replacement.
PNG inputs use the existing decoder and image limits. Owned compressed assets are
limited to 32 MiB. Paths, including symlinks, must remain inside the chosen asset
root. Preparation decodes assets and checks the complete generated manifest with
the existing schema-1 parser before publication. Numeric transforms, animation
intervals and slice extents are validated explicitly.

Prepared assets own their bytes. Publication revalidates the expected binding,
stages all files beside the destination, and uses Linux renameat2 with
RENAME_NOREPLACE. It never overwrites an existing pack, including concurrent
publication by another author. Failed staging is cleaned up. This is atomic
publication, not a guarantee of persistence across a power failure.

## Authoring workflow

In `time-browser`, pause execution and export the authoring capture after the
video view catches up. The CLI also accepts bounded captures assembled by other
owned-observation consumers. Resource coverage is observational and does not
establish physical completeness of a game or port.

Run `time-visual-author --capture capture.json --source recipe.json --assets ART
--output NEW_PACK`. `--language lua|python|javascript` instead evaluates a program
whose `time.report` output is the recipe. Scripts can inspect captured CPU values;
they cannot mutate the guest or access arbitrary host files.

Recipes have schemaVersion 1, id, name, core, romSha256, videoSequence, generation
and a resources array. Each resource names kind and sourceHash; hashes accept
sixteen lowercase hex digits with an optional 0x prefix. An optional label requires
reviewer and evidence text. Optional replace objects use the existing pack image,
layers, animation, palette, transform, slicing and declarative effect contracts.

The output contains manifest.json, copied PNG assets and annotations.json. The
manifest remains schema 1 and uses exact observed source hashes. Legacy pack
matching remains resource based; the legacy runtime does not impose a ROM-wide
restriction. The separately versioned annotations retain the ROM and capture
binding and explicitly identify reviewed annotations. Import these in the browser
to show reviewed gameplay labels separately from renderer observations. Invalid,
stale, cross-ROM, duplicate or unobserved labels retain the previous good set.
Generation changes disable older labels; they require renewed validation.

## GPU presentation contract

`TimePostProcessV1` consumes a complete owned 160x144 RGBA frame on the browser
presentation lane. WebGL2 availability is explicit. It accepts a maximum 16 KiB
GLSL ES 3.00 fragment shader using uFrame and vUV, stages compile/link, and replaces
the active program only on success. The preview remains separate from canonical
pixels, provenance, overlays and guest semantics. CPU, ROM and capture generation
identities are validated independently of GPU assets.

Textures and draw extents are fixed. No guest state, machine callbacks, host file
access or script interpreter runs in the GPU contract. Failed compilation/linking
retains the last good shader. Stale generations hide the preview until reapply.
Context loss is visible and preserves author source; restoration recreates owned
GPU resources and recompiles the last good program. Disposal removes listeners
and releases GPU resources. Shader source is author-supplied project code, not
code extracted from ROM instructions.

## Acceptance boundaries

C++ tests cover both core identities, asset ownership/validation, legacy loading,
reviewed annotations, stale/cross-core/cross-ROM rejection, transactional output,
concurrent publication and all available scripting adapters. Independent CLI
checks verify published animation assets and rejected-publication isolation.
JavaScript contract tests cover compile/link failure, last-good retention,
context loss/restoration, disposal, invalid frames and annotation bindings.
They do not establish GPU rendering: browser/device checks are recorded separately.
ARM64 QEMU runs verify target code; they do not establish physical ARM64 GPU,
controller, IDE or audible acceptance. Required missing evidence keeps admission
closed. No reviewed no-go exception applies to these ordinary features.
