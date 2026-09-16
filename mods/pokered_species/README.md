# Pokémon Red title species patch

This demo patches the real `TitleScreenPickNewMon` selection at `01:44a3` and
the `GetMonHeader` call in `LoadTitleMonSprite` at `01:452d`. The generator is
pinned to English Red SHA-256
`5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b`.
Addresses were checked against pokered symbols and native section layout;
the unused ROM0 tail beginning at `$3fa6` holds the guest shims and RET hooks.
Both the image hash and original patch bytes must match. The original file is
never modified. No ROM, extracted game data, or generated package is committed.

## Build and verify

From the repository root, using a legally obtained reference ROM:

```sh
cmake -S . -B build-working
cmake -S . -B build-working \
  -DTIME_POKERED_TEST_ROM=/absolute/path/to/pokered.gbc \
  -DTIME_POKERED_TEST_MOD=/absolute/path/to/a/new/pokered-species-demo
cmake --build build-working --target time-pokered-species-package -j4
ctest --test-dir build-working -R smoke-pokered-species --output-on-failure
```

The CMake target builds the native module and invokes the generator with the
user-supplied ROM; the output directory must not already exist. The CTest test
is registered only when a ROM is configured (and Python 3 is available).
Without `TIME_POKERED_TEST_ROM`, the module smoke test remains available and no
ROM or package is generated. The ROM path must be absolute so an accidental
build-tree copy cannot become test input.

Output must be a new directory, preventing accidental package overwrites.
The verifier loads the manifest and native module, installs the two hooks by
symbol, and executes the actual ROM selector/header call sites in multi-instruction
slices. It checks all 16 selections, full-width identity, external header bytes,
register/stack preservation, and the initial vanilla-header fallback. This is
headless routine-level integration, not a complete boot or visual gameplay test.
The normal `timeEmulator` CLI does not yet activate this package automatically.
The verifier's setup shows the embedding API sequence for activation.

The Game Boy CLI can activate the generated package directly:

```sh
./build-working/timeEmulator --core gameboy \
  --rom /absolute/path/to/pkrd.gb \
  --mod /absolute/path/to/pokered-species-demo
```

Repeat `--mod` to select multiple packages. The current CLI hook integration is
limited to this title-species package; it validates the original ROM while
loading, applies the guarded patches, and installs both native hooks before
execution.

Native-module unit tests run without copyrighted inputs. Regenerate into a new
directory after changing the native module, since the package contains its own
copied library. The generator retains the pinned ROM hash, revision checks,
original patch-byte checks, and code-cave validation.

## Data and guest contract

- `selection`: 16 little-endian 16-bit IDs, replacing the vanilla title table.
- `records`: 257 records of 29 bytes: one legacy display-proxy ID followed by
  the 28-byte header normally produced by `GetMonHeader`.
- `current`: a two-byte full-width selection, initially `$ffff` (unset).
- Hook 1 (`NativeSpeciesSelect`): HL slot 0..15 in, full species ID out;
  validates the record before updating `current`.
- Hook 2 (`NativeSpeciesRead`): HL field 0..28 in, byte out; field 0 is the
  display proxy, fields 1..28 are the external header. Unset field 0 returns 0
  so the initial title sprite falls back to the original `GetMonHeader`.

Slot 0 selects species **256**, with a Bulbasaur-derived header whose HP is 123.
The ROM-facing title comparison and sprite loader still use Bulbasaur's legacy
ID `$99`; the full ID remains in the external pool. No new graphics are supplied.
The header shim copies data using ordinary guest stores and preserves registers.
Native code executes synchronously on the emulation thread and has full host
privileges: only load trusted modules.

This does **not** widen party, battle, Pokédex, evolution, encounter, or save-file
fields. Different extended species sharing one proxy also share the title's
repeat-suppression identity. Those consumers need their own explicit bridges
before extended species can participate throughout gameplay. Machine save states
are deliberately unavailable while native hooks are installed.
