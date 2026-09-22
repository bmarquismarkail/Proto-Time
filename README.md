# Project T.I.M.E.

**The Infinite Modder's Emulator** is a pre-alpha C++20 framework for running,
understanding, and extending emulated machines. Game Boy is the reference
machine; Game Gear is a work in progress.

The central idea is a programmable whole-machine laboratory: the machine owns
hardware truth, the CPU defines instruction semantics, and an executor chooses
how execution proceeds under an explicit guarantee. Modding, observation, and
alternative presentation build on that foundation.

## Start here

- [Project philosophy](docs/philosophy.md): what T.I.M.E. is trying to preserve.
- [Documentation index](docs/README.md): current guides, contracts, and design history.
- [Build and run](docs/runtime.md): dependencies, frontends, execution modes, mods, and diagnostics.
- [Current architecture](docs/architecture.md): ownership, execution, plugins, and test coverage.

## Build

```bash
cmake -S . -B build-working
cmake --build build-working -j4
ctest --test-dir build-working --output-on-failure
```

See [build dependencies and options](docs/runtime.md#build). Run a local ROM:

```bash
./build-working/timeEmulator --core gameboy --rom path/to/game.gb
```

## Current Status

This repository is still pre-alpha and intentionally incomplete. The summary
below describes the current working framework. The latest execution work adds
a versioned multi-core IR boundary and a Game Gear portable-IR path:

- Game Boy and work-in-progress Game Gear machines selected through the machine
  registry
- Machine-owned CPU, PPU/VDP, APU/PSG, input, mapper, save-state, and timing
  paths with hardened state and memory validation
- Baseline and portable-IR execution for Game Boy and Game Gear, plus Game Boy
  block-cache and frozen experimental native x86-64 paths
- A shared guarded IR service with built-in core adapters and a portable
  backend, plus independently loaded pure-C IR adapter and backend modules
- Dynamically loaded pure-C executor policies with explicit capability and
  execution-guarantee checks
- Interchangeable SDL and GLFW frontends, a headless path, and audio output that
  is selected independently from the window/input/video frontend
- A prepared audio transport, rich PSG voice stems and events, fixed-capacity
  audio-processor plugins, Standard MIDI File export, and optional live ALSA
  MIDI output
- Visual override packs with asynchronous image decoding and capture, optional
  hot reload, and 1x-8x HD texture replacement
- Periodic JSON-lines runtime diagnostics covering timing, video, audio, and
  background-work interference
- Broad CTest smoke, differential, corruption, lifecycle, concurrency, and
  performance coverage

## Development Hooks

Tracked post-merge and post-rewrite hooks can refresh an existing Graphify
knowledge graph after pulls and rebases. Activate the tracked hook directory in
each checkout:

```bash
git config core.hooksPath .githooks
```

The refresh remains non-fatal, does not bootstrap a missing graph, and checks
that the installed Graphify supports deletion-safe `update --force` first.
