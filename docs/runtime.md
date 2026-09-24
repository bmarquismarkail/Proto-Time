# Build and run T.I.M.E.

[Documentation index](README.md) · [Architecture](architecture.md)

Status: current usage guide. Run commands from the repository root.

## Build

```bash
cmake -S . -B build-working
cmake --build build-working -j4
```

Zlib, nlohmann_json (3.2 or newer), and OpenSSL Crypto are required. SDL2,
GLFW 3.3/OpenGL, and ALSA are detected as optional host dependencies.
The corresponding features can also be controlled with
`PROTO_TIME_BUILD_SDL_FRONTEND`, `PROTO_TIME_BUILD_SDL_AUDIO`,
`PROTO_TIME_BUILD_GLFW_FRONTEND`, and `PROTO_TIME_BUILD_ALSA_MIDI`.

The default build produces `timeEmulator` and, when enabled, independent shared
modules for the SDL frontend, GLFW frontend, and SDL audio output:

- `libtime-sdl-frontend-plugin.so`
- `libtime-glfw-frontend-plugin.so`
- `libtime-sdl-audio-output-plugin.so`

The modules report deterministic backend-unavailable errors when their optional
host dependency is unavailable.

## Frontends and audio output

`timeEmulator` auto-loads the selected frontend module from the executable
directory by default. Use `--frontend-plugin <path>` to load any compatible
pure-C frontend module, `--frontend <id>` when it exposes multiple frontends, or
`--headless` to skip frontend loading. `--plugin` remains a path alias.

SDL remains the default. Switch window, input, and video presentation to GLFW
with:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb --frontend glfw
```

`--frontend sdl` selects SDL explicitly. Audio output is host-owned and selected
independently with `--audio-backend sdl|dummy|file`; external audio-output
modules use `--audio-plugin <path>`.

Native modules may optionally observe read-only machine state through the
[versioned observation interface](native_observation.md). Desktop integrations
and game-specific decoding live in independently maintained modules.

## Executor policies and IR components

Executor policies are a separate extension layer. Built-in policies can be
selected by stable ID, while external modules use the pure-C ABI in
`machine/plugins/abi/TimePluginAbi.h`:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb \
  --executor-plugin path/to/executor-module.so \
  --executor-policy vendor.executor.policy
```

If a module exposes exactly one executor policy, `--executor-policy` may be
omitted. The legacy `--cpu-mode baseline|block|ir|native` options remain aliases
for the built-in policy IDs. Frontends use the same module ABI with a distinct
frontend descriptor and function table.

The built-in policy IDs are:

- `bmmq.executor.policy.default-step`
- `bmmq.executor.policy.visible-state-preserving-step`
- `bmmq.executor.policy.portable-ir`
- `bmmq.executor.policy.native-experimental`

Portable IR uses the built-in adapter and backend for the selected machine by
default. Either component can be replaced independently through the same
pure-C module ABI:

```bash
# Built-in Game Gear adapter and portable backend
./build-working/timeEmulator --core gamegear --rom path/to/rom.gg --cpu-mode ir

# Dynamic adapter and backend
./build-working/timeEmulator --core gamegear --rom path/to/rom.gg \
  --cpu-mode ir \
  --ir-adapter-plugin path/to/adapter-module.so \
  --ir-adapter-id vendor.gamegear.adapter \
  --ir-backend-plugin path/to/backend-module.so \
  --ir-backend-id vendor.portable.backend
```

Each plugin path and its corresponding ID must be supplied together, and
selecting any dynamic IR component requires `--cpu-mode ir`. An omitted adapter
or backend remains built in. Unknown IDs, malformed tables, architecture or IR
ABI mismatches, and incompatible adapter/backend pairs fail as configuration
errors instead of silently substituting another component.

The attached executor policy ultimately chooses the execution backend.
`--cpu-mode ir` selects the built-in portable-IR policy when neither
`--executor-plugin` nor `--executor-policy` supplies another policy. A
separately selected policy takes precedence and must itself select the
portable-IR backend for the configured IR components to run. A pure-C executor
policy reports this as `TIME_EXECUTION_BACKEND_PORTABLE_IR_V1` from its
`TimeExecutorPolicyApiV1::backend` callback; the machine runtime then performs
IR dispatch. The current validator accepts a non-portable policy alongside
dynamic IR components when `--cpu-mode ir` is also present, but the installed
IR components remain unused.

## Audio processors

Audio processors use another descriptor in the same ABI and run in the
fixed-capacity source pipeline before device output:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb \
  --audio-processor-plugin path/to/processor-module.so \
  --audio-processor-config vendor.processor=processor-config.json
```

Processor modules and configuration mappings are repeatable. A processor that
returns an invalid result is permanently disabled for that instance, passes the
original samples through, and exposes its captured ABI error through pipeline
diagnostics.

## Tests

Run tests:

```bash
(cd build-working && ctest --output-on-failure)
```

Discover the tests registered by the configured build rather than relying on a
hardcoded list:

```bash
(cd build-working && ctest -N)
```

## Runtime Examples

Run either registered machine core:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/game.gb
./build-working/timeEmulator --core gamegear --rom path/to/game.gg
```

Load a visual pack and preserve higher-resolution replacement texels at 4x
output scale:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/game.gb \
  --visual-pack path/to/pack.json --hd-scale 4
```

See the [pack authoring guide](texture-pack/author-workflow.md) and
[HD sampling guide](texture-pack/hd-replacement.md) for the supported contracts.

Translate PSG events to MIDI, or send them to an ALSA sequencer destination
when ALSA MIDI support was built:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/game.gb \
  --midi-file output.mid

./build-working/timeEmulator --core gameboy --rom path/to/game.gb \
  --midi-output 128:0
```

With an active frontend, `F1` writes `quicksave.ptstate` in the current working
directory. Both registered machines implement the machine save-state contract. Game Boy
save/load states are currently rejected while native mod hooks are installed.

## Runtime Diagnostics and Performance Baseline

`timeEmulator` supports periodic runtime diagnostics snapshots without changing
emulation behavior:

```bash
./build-working/timeEmulator \
	--core gameboy \
	--rom path/to/rom.gb \
	--timing-profile balanced \
	--diagnostics-report diagnostics-balanced.jsonl \
	--diagnostics-interval-ms 1000
```

Timing profile examples:

```bash
# balanced
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile balanced \
	--diagnostics-report diagnostics-balanced.jsonl --diagnostics-interval-ms 1000

# low_latency
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile low_latency \
	--diagnostics-report diagnostics-low-latency.jsonl --diagnostics-interval-ms 1000

# deterministic_test
./build-working/timeEmulator --core gameboy --rom path/to/rom.gb --timing-profile deterministic_test \
	--diagnostics-report diagnostics-deterministic.jsonl --diagnostics-interval-ms 1000
```

The diagnostics report is JSON-lines (one object per interval) and includes host
elapsed time, emulated cycles, effective speed, frame submit/present counts,
fresh/fallback presents, mailbox overwrites, publish-to-present age stats,
presenter duration stats, audio underrun/silence counters, audio worker wake
latency stats, frontend tick scheduled/executed/merged, wake jitter buckets,
and active timing profile.

Perf baseline workflow (RelWithDebInfo):

```bash
cmake -S . -B build-working -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-working -j4

perf record -F 999 -g -- ./build-working/timeEmulator \
	--core gameboy \
	--rom path/to/rom.gb \
	--timing-profile balanced \
	--diagnostics-report diagnostics-balanced.jsonl \
	--diagnostics-interval-ms 1000

perf report
```

## Native mods

Use repeatable `--mod <directory>` flags for explicitly selected Game Boy
packages. Read the [manifest and native-module contract](../machine/modding/README.md)
and the [revision-pinned Pokémon title demo](../mods/pokered_species/README.md)
for preparation, activation, and current limits.
