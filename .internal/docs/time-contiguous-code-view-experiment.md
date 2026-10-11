# Checked contiguous code view experiment

2026-10-11. Separate research candidate against `time-measure-acceleration-guard-reference`.
The candidate defines `TIME_RESEARCH_REFERENCE_GUARDS` and
`TIME_RESEARCH_CONTIGUOUS_CODE_VIEW`; it retains the original immutable guard
metadata search and does not combine the cached guard pointer optimization.
Ordinary execution and acceleration admission remain unchanged.

## Mapping contract

`checkedCodeView(address,size)` returns an exact-length, read-only span only when
that entire range has direct contiguous backing with the same values as the
existing executable byte peeks. Empty, overflowing, crossing, unsupported or
partially backed ranges return empty and require the original byte-peek fallback.
No range crosses a ROM bank/window, boot overlay, RAM mirror or MMIO boundary.
Unsupported bytes still fail through the existing peeks; a missing view never
means successful validation. Partial ROM padding and modulo BIOS wrapping use
the fallback rather than inventing contiguous bytes.

Game Boy uses its owned boot ROM, installed ROM windows, WRAM and HRAM. Game Gear
uses BIOS and RAM, or the exact built-in cartridge type with its current physical
bank. Custom mappers and subclasses fall back to virtual reads, preserving their
semantics. The exact-type restriction is deliberately conservative; this
experiment does not extend the public mapper virtual interface.

The span is obtained after scalar guards and consumed immediately inside one
exact-byte comparison. Bindings retain no live memory/CPU/view pointers. Bind
and remap validation remain byte peeks. Each required invocation entry or
observed/writable continuation acquires a fresh view. Owner/lifecycle, mapping,
execution-state, helper ABI, fetch observer, PC, policy, exclusive-execution and
fault checks remain in their existing positions. No view crosses a callback or
retirement. The machine lane remains the sole authoritative writer.

## Invocation inspection, separate from the view candidate

The generated entry calls `Invocation::begin`, then the type-erased before thunk,
then the core's live guard lambda. `Invocation::retire` checks order/retirement,
accounts progress and calls the retire thunk. That thunk retires CPU state,
retires machine/device state, invokes the observer, and then handles machine,
observer, segment and budget exits. The observer is called even when machine
retirement requests an exit; ordering is observable and must remain intact.

Machine retirement includes device stepping and plugin event emission. Those
callbacks, subclass overrides and the retirement observer may change machine
state or replace its policy. Both `attachedExecutorPolicy()` and `backend()` are
virtual; caching a policy reference across a callback could also retain a
replaced policy. Live lookup before each instruction therefore remains required.
No callback is removed, reordered or suppressed, and no live policy/lifecycle
check is hoisted. Regression coverage mutates a policy subclass's virtual backend or replaces
the attached policy through observer callbacks, on both cores and both
generic/direct modes, requiring a side exit before the next instruction. Both
concrete core machines are final; machine-retirement callback ordering is
covered by source inspection and existing boundary tests.

Potential future hook experiments should measure type-erased thunk and duplicated
budget bookkeeping independently while keeping CPU/device retirement, callbacks
and live checks identical. Existing sampled phase timings include guard work and
retirement bodies; neither this candidate nor those timings isolates bare hook
or policy dispatch costs. No causal attribution to these remaining invocation
costs is claimed here.

## Reproduction and evidence

```sh
cmake -S . -B build-working
cmake --build build-working -j4
ctest --test-dir build-working -R 'code-view|research-machine-boundaries|research-remap|acceleration-guard-reference' --output-on-failure
ctest --test-dir build-working --output-on-failure
python3 tools/measure_acceleration_experiments.py --synthetic --guard-reference --output build-working/completion-validation/code-view/reference-0.json
python3 tools/measure_acceleration_experiments.py --synthetic --code-view --output build-working/completion-validation/code-view/candidate-0.json
python3 tools/measure_acceleration_experiments.py --synthetic --code-view --output build-working/completion-validation/code-view/candidate-1.json
python3 tools/measure_acceleration_experiments.py --synthetic --guard-reference --output build-working/completion-validation/code-view/reference-1.json
```

Collections run only after builds/tests complete, without overlap. Opposite
collection orders do not make separate-process timings temporally matched pairs;
raw reference differences are exploratory, not causal or qualification evidence.
Each collection's rotated existing backends provide the competitive control.
No broad variant sweep is included. Historic cache/reference archives remain
unchanged. Native ARM64 timing remains deferred. Results are recorded below.

## Validation and results

Full build passed after correcting test-fixture compile errors (a temporary
adapter, attempts to subclass final core machines, and a namespace qualification).
All build attempts are retained. The six focused CTests passed in 27.08 seconds.
The full sandboxed CTest run completed in 489.64 seconds: 191/197 passed,
including all acceleration/view tests and all five performance checks. Six
socket/Python-worker tests failed under sandbox restrictions; all six passed
when rerun outside the sandbox in 6.95 seconds. This is passing evidence for all
197 registered tests across the full run and targeted unrestricted rerun, not a
claim that the sandboxed full suite passed. Both logs remain available.

The view parity test checks every CPU address with four range sizes wherever a
view is available, including changed banks, partial ROM/window backing, boot
mapping removal, short BIOS modulo boundaries, MMIO holes, RAM mirrors, reset,
custom cartridge subclasses, arithmetic overflow, exact-byte corruption,
scalar-first rejection, scalar-only continuations and unsupported-view fallback.
The candidate boundary executable repeats writable self-modification, guest and
observer mapping transitions, restore invalidation, interrupt state, exceptions,
fault recovery and mutable/replaced policy checks in both generic/direct modes.
Disassembly shows two calls to checkedCodeView in the candidate (one per core)
and zero in the retained reference.

Four nine-repeat collections retain 1,080 raw samples and passed correctness,
source/build hash and environment stability checks. The tested source SHA-256 is
`c6afefff9940c696fd197e75a1e6a9ee55da0620d4a18e6237db345ab21b11ba`.
`tested.patch`, `tested-new-files.tar.gz` and `tested-source-binding.json`
reconstruct the source from `60afc9f`, including earlier uncommitted profiling
work. `summary.json` includes collection bindings and both generic/direct rows;
the collector stores within-collection paired improvements and raw samples.
All evidence is under `build-working/completion-validation/code-view/`.

Positive percentages mean faster. The raw reference differences below compare
separate executable collections, not temporally matched pairs. They do not
establish causality or qualify the candidate. The within-collection comparison
uses its fastest existing backend selected from baseline/cached/portable IR.

| Core/workload | Generic raw gain vs reference, session 0 / 1 | Generic gain vs fastest existing, session 0 / 1 |
|---|---|---|
| gameboy/compute-heavy | +5.08% / +0.49% | -6.64% / -10.52% |
| gameboy/mixed-devices | -6.62% / -6.56% | -17.60% / -14.43% |
| gameboy/ram-heavy | +2.78% / +0.80% | -11.25% / -2.82% |
| gamegear/compute-heavy | -2.36% / -16.21% | -11.04% / -17.02% |
| gamegear/mixed-devices | +0.92% / -1.20% | -4.44% / -2.32% |
| gamegear/ram-heavy | +0.62% / +7.42% | -3.88% / -8.75% |

There is no repeatable competitive active-workload win. Generic and direct
candidates are slower than their collection's fastest existing backend in all
six workloads in both sessions. Game Boy generic compute has a small positive
raw reference difference in both sessions but remains 6.64% and 10.52% slower
than its competitive control. Game Gear generic compute is 2.36% and 16.21%
slower than the reference, and 11.04% and 17.02% slower than the competitive
control. Neither a causal speedup nor a causal Game Gear regression is assigned
to this implementation from separate-process timings. Direct-mode raw changes
also reverse sign across several cases and do not establish benefit.

No broad sweep, cache combination, acceptance relaxation, policy promotion or
full v2 qualification was performed. The candidate stays independently
selectable research code. The reference/cache evidence remains intact. All
changes are uncommitted. The next invocation experiment should independently
attribute thunk dispatch and repeated virtual policy lookup while retaining
mutable/replaced-policy and callback-order checks; do not cache a live policy
or remove observable retirement work to obtain a timing win.

## Commit checkpoint, 2026-10-11

The implementation, tests, collectors and these experiment reports are included
in the requested commit checkpoint. Earlier statements about uncommitted work
describe collection-time status. Generated build and raw timing artifacts remain
local and excluded from Git. Recorded hashes continue to identify the original
tested snapshots; adding these previously ignored reports changes the checkout
source digest and does not rebind historical measurements to the commit.
