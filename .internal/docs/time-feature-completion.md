# Complete T.I.M.E. on the two existing machine families

Contract version: 1. Kind: current completion program and admission contract.
Baseline: PR #19 merge `6d130b3`, followed by the approved two-core completion
program. Machine families: Game Boy and Game Gear. Hosts: Linux x86-64 and ARM64.

The machine-readable contract is [time-feature-completion.json](time-feature-completion.json).
It selects the current guides/design references catalogued by
[docs/README.md](../../docs/README.md), the latest
[S.P.A.C.E. design](space-analysis-porting-design.md), and the new
[acceleration evaluation](time-acceleration-evaluation.md).
Prospective adapters in those references are obligations; superseded archival
mechanisms are not additional requirements. Later proposals require a scope
revision. Completing this program admits another family; it does not implement one.

## Admission and evidence

Run `python3 tools/check_feature_completion.py` from the repository root.
No evidence leaves admission closed. CMake generates an internal admission
constant and reruns contract validation on builds. `MachineRegistry` accepts the
two existing families and explicitly declared aliases backed by them, rejects
unknown families while closed, and verifies the created machine's family.
New directories under `cores/` also fail the check while completion is open.

Supply reviewed evidence with `--evidence PATH` or CMake's
`-DTIME_COMPLETION_EVIDENCE=PATH`. The JSON envelope has `schemaVersion: 1`,
`sourceSha256`, `buildConfigurations`, and `records`. The source digest covers
source/tests/tools/current guides, selected authorities, this matrix, CMake and
the admission workflow. A changed source or authority invalidates the evidence.

Each host configuration records `compiler`, `compilerVersion`, `buildType`,
`cmakeCacheSha256`, `cpu`, `configureCommand`, `buildCommand` and `testCommand`.
Each record names `feature`, `core`, `host`, `kind` (`automated` or `live`),
`status`, `buildConfigurationSha256`, `artifact`, and `artifactSha256`.
The configuration digest is SHA256 of sorted compact JSON. Artifact paths must
remain within the evidence directory and match their content hashes. Evidence
records are trusted reviewed acceptance reports, not instructions to execute.
The checker verifies their structure/binding; reviewers remain responsible for
whether the reports substantiate every acceptance item.

Missing, failed, not-run, or stale required records keep admission closed.
`reviewed-no-go` also requires a reviewer and is allowed only for the newly
evaluated native research. It cannot close any ordinary feature row. Automated
coverage is required for both cores on both hosts. Rows requiring physical/UI
acceptance also require separate live records on both hosts. CI contract tests
use fabricated records in temporary test repositories to exercise validation;
these records are not product acceptance evidence.

## Work sequence and current status

1. Shared core adapters, core-aware persistence and Game Gear instruction capture.
2. Snapshot execution parity, hardware/provenance analysis, symbols and bounded
   checkpoint exploration.
3. Bidirectional standalone port accounting and independently verified ROM proofs.
4. Engine/service contracts, native-mod coordinated state, debugger/DAP,
   scripting runtimes, input/netplay/browser adapters, visual authoring/GPU and
   dynamic machine providers.
5. New measured acceleration evaluation.

Implementation is in progress. No feature row has been certified complete.
The initial changes add admission checks, shared S.P.A.C.E. core boundaries,
Game Gear capture/snapshot support, core-aware checkpoint formats, generalized
port ledger validation, exploration controls and ROM-bound meaning metadata.
Both standalone fixture directions now have reproducible builds, reviewed
physical accounting and independent reference-emulator verification. Game Gear
analysis also follows observed byte-copy provenance and controller polling.
Fixture completeness does not certify the full feature matrix. Dedicated debugger
engine/service, bounded DAP stdio, trace/profile output and transactional paused
RAM/register edits now have both-core regression targets. Actual editor
acceptance and current-source host validation remain open. File automation, Lua,
Python and QuickJS now have owned-snapshot execution and staged mutation recipes;
prepared live counters/pause actions avoid interpreter work on the machine lane.
Scripting validation remains separate from certification. Dynamic providers, netplay,
streaming, controller/GPU/live acceptance and new native research remain open,
along with complete hardware-envelope assessment and ARM64 validation.
Coordinated host-region/native-module checkpoint and reset implementation now
has explicit transactional regression targets for both cores; its matrix row
remains uncertified until current-source checks pass on both hosts.

Baseline execution, one guest-state writer, drain-only audio callbacks and
presentation-owned GPU work remain invariants. Capture and snapshot execution
continue rejecting incompatible accelerated/native-hook combinations.

## Validation obligations

Differential tests compare full registers, cycles, ordered effects, canonical
writes, mapper/video/audio state and fingerprints. Game Gear coverage must also
include mirrors, rewriting RAM code, bank/fetch boundaries, prefixes (including
long chains), refresh, shadow/indexed aliases, interrupt modes, deferred EI,
HALT and device reads/writes. Persistence tests cover legacy Game Boy projects,
core/ROM rejection before mutation, corrupt checkpoints, history branching,
detach/replacement lifetimes, capture loss and storage exhaustion.

Discover and run the full CTest suite, targeted tests, TSAN handoff checks and
existing performance gates. Record exact source/build configuration with each
result. Independent reference-emulator port verification and browser, IDE,
controller, GPU and audible checks remain separate from headless checks.
Unavailable devices/runners are **not run**, and keep admission closed.

## ARM64 checks under QEMU

The requested ARM64 correctness runner may use QEMU user mode. Configure a
separate build with `tools/cmake/aarch64-qemu.cmake` and absolute
`TIME_ARM64_COMPILER_PREFIX`, `TIME_ARM64_SYSROOT` and `TIME_ARM64_QEMU` paths.
The sysroot must supply ARM64 libc/libstdc++, OpenSSL, zlib and JSON headers.
Host tools such as Python and the fixture assemblers remain host executables.
Disable unavailable SDL/GLFW/ALSA adapters explicitly in this headless build;
their acceptance remains separately not run.

Use `tools/qemu_ctest.py --build BUILD --output SHADOW --qemu QEMU --sysroot
SYSROOT` to run the discovered tests. CMake wraps direct executable tests; the
shadow runner also wraps ARM64 executable arguments passed to Python harnesses.
Shared libraries remain actual target libraries for guest `dlopen`. The runner
records binary/wrapper/QEMU hashes and never installs system binfmt handlers or
rewrites build outputs. Its timings describe this emulated configuration and
must remain separate from native ARM64 and physical-device acceptance.

Linux controller boundary snapshots and the frame-indexed remote input engine, transport and host now have both-core regression targets. Physical controller/hotplug and live network acceptance remain open; new phase validation does not certify admission. See [input adapter contracts](../../machine/plugins/input/README.md).
