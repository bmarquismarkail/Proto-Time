# Scripting engine and service

`time-script` executes file automation, Lua 5.4, Python, or QuickJS JavaScript
against an owned paused snapshot of either existing core. Its interpreter work
runs on a tooling worker. The machine worker remains the sole guest-state writer.
A successful invocation returns a bounded recipe; the debugger validates the
entire recipe, generation and pause identity before publishing any change.
Interpreter failure, denied bindings, stale results and unsupported device writes
leave guest state unchanged. Catching a binding error does not make its recipe
admissible. Baseline execution is required, as for the dedicated debugger.

## Build

Python is enabled by default. Lua is enabled when a Lua 5.4 development library
is found or `TIME_LUA_SOURCE_DIR` is configured. QuickJS requires an explicit
source tree. Configure never downloads dependencies. To build the pinned source
versions in private, ignored build storage:

```sh
python3 tools/fetch_script_dependencies.py --output build-working/script-dependencies
cmake -S . -B build-working \
  -DTIME_LUA_SOURCE_DIR="$PWD/build-working/script-dependencies/lua-5.4.9" \
  -DTIME_QUICKJS_SOURCE_DIR="$PWD/build-working/script-dependencies/quickjs-2026-06-04"
cmake --build build-working --target time-script time-smoke-scripting -j4
ctest --test-dir build-working -R '^smoke-scripting' --output-on-failure
```

Sources come from [Lua](https://www.lua.org/ftp/) and
[QuickJS](https://bellard.org/quickjs/), with pinned archive SHA-256 checks and
bounded, path-checked extraction. Existing source trees must match their archives.
Cross builds can compile the same source trees for ARM64. Set
`TIME_SCRIPT_PYTHON_EXECUTABLE` to a Python interpreter on the execution host;
record its executable, libraries and version in validation evidence. Disabling
or losing a runtime is explicit and does not satisfy its matrix requirement.
Existing external plugin ABIs are unchanged; scripting roles use V1 interfaces.

## Invocation

```sh
build-working/time-script --core gamegear --rom fixture.gg \
  --language lua --source patch.lua --allow-ram --allow-registers
```

Game Boy is the compatibility default. Both mutation permissions default off.
The CLI reads `0xc000..0xc0ff`; library clients can supply another owned window
of at most 256 bytes. Register names use the architectural pairs exposed by the
core (`AF`, `BC`, `PC`, and Game Gear indexed/shadow/interrupt registers).

Lua and JavaScript expose `time.reg(name)`, `time.setreg(name,value)`,
`time.read8(address)`, `time.write8(address,value)` and `time.report(text)`.
Python exposes the same operations on the supplied `time` object. Reads observe
staged writes before the original snapshot. Writes are integer checked and staged;
actual pure RAM, mirror and register validation belongs to the machine adapter.
Ports, mapper controls and device registers cannot be edited through this API.

File automation uses an explicit family and schema:

```json
{"schemaVersion":1,"core":"gamegear","actions":[
  {"operation":"register","name":"BC","value":1234},
  {"operation":"write","address":49152,"value":85},
  {"operation":"report","text":"done"}
]}
```

The command writes one JSON result with the owned post-commit registers, RAM
window and report, and exits nonzero on failure. It does not run guest instructions
or save a patched ROM. File automation limits nesting to 32, actions to 128 and
recipes to 64 RAM writes. Source and report limits are 64 KiB by default. Interpreter
limits default to 8 MiB of additional runtime heap, one million work units and
one second. Library limits may be configured within the service's bounded maxima.
Limits are host execution policy, not guest emulation time.

Source builds compile Lua with its supported C++ protected-call path while
preserving the public C linkage; TSAN validation requires this source build.
Lua meters VM instructions and allocations. Its exposed libraries are base, math,
string, table and UTF-8; file loaders, OS, I/O, package, debug and coroutine libraries
are unavailable. Pattern-search functions are unavailable because their native
backtracking does not invoke Lua's instruction hook. Mutation during teardown
is rejected. QuickJS uses its allocation limit, stack limit and interrupt callback;
interrupt polling is charged in 10,000-unit chunks, so limits do not denote exact
JavaScript bytecode counts. Pending promise jobs are drained within the same
invocation; unhandled rejections and a bounded rejection-table overflow fail it.

Python runs with isolated interpreter startup in a separate process group. Opcode
tracing meters work and tracked allocations; the worker also limits additional
address space, CPU, core files and file output. The parent enforces wall time and
protocol output size, kills the worker group and any unreaped child on failure or completion, and validates
all returned recipes again. Python scripts are trusted host code, not a security
sandbox; they can import host modules. No runtime receives a guest pointer.

## Lifecycle and live hooks

`ScriptService` owns eight loaded script slots and permits one outstanding
invocation or commit. Load/unload and capabilities are checked on its single
owning tooling lane. `observe` publishes owned debugger replies; changing the
pause or observation generation, running, faulting or detaching invalidates a
pending recipe. Commit acknowledgements consume a ticket exactly once.
The caller multiplexes recipes into the existing single-producer debugger queue;
it must not introduce another command producer. The service destructor joins its
worker on the tooling lane. Diagnostics expose invocation errors, denied
capabilities, unavailable runtimes, rejected live calls and observed generations.

Interpreters are rejected for live callbacks. `ScriptHookEngine` instead accepts
up to eight prepared counter/pause actions while paused, with deterministic,
snapshot-aware, live-safe and bounded capabilities. Retirement dispatch performs
fixed work without allocation, interpreter callbacks, files or locks. Actions
specify their first retirement and period. Restore, reset, ROM changes and edits
invalidate them; disconnect clears them. Counters do not become guest state.

## Evidence boundary

`smoke-scripting` covers real machines, all available runtimes, read-after-write,
error atomicity, permissions, heap/work/timeout exhaustion, generation changes,
commit rejection, lifecycle, lane ownership and live hook invalidation.
`smoke-scripting-cli` independently verifies file automation on both cores.
Runtime absence is exercised as a rejected capability and remains missing
acceptance evidence. Live editor integration and full two-host validation are
separate matrix obligations; these tests alone do not open third-core admission.
