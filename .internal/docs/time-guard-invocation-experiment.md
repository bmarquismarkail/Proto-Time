# Guard lookup and invocation overhead experiment

2026-10-11. A single change caches the code-byte guard's location in each binding,
removing the immutable guard-vector search at every byte-validation entry.
Scalar validity already requires exactly one code guard. Its pointer belongs to
the binding's shared immutable compiled artifact; remaps share it, while copied
artifacts construct a fresh pointer. No byte comparison, scalar/owner/lifecycle
check, observed/writable continuation check, retirement, or fault behavior is
removed. No acceleration admission or ordinary execution policy changes.

The reference executable compiles the original metadata search from the same
source, with the same binding layout, through `TIME_RESEARCH_REFERENCE_GUARDS`.
It uses independent machine objects, as the diagnostic executables do, to select
the reference template rather than the candidate's archive machine objects.

Reproduction after a full configure/build:

```sh
ctest --test-dir build-working -R 'research|acceleration|whole-block|active-phase' --output-on-failure
ctest --test-dir build-working --output-on-failure
python3 tools/measure_acceleration_experiments.py --synthetic --guard-reference --output build-working/completion-validation/guard-lookup/reference-0.json
python3 tools/measure_acceleration_experiments.py --synthetic --output build-working/completion-validation/guard-lookup/candidate-0.json
python3 tools/measure_acceleration_experiments.py --synthetic --output build-working/completion-validation/guard-lookup/candidate-1.json
python3 tools/measure_acceleration_experiments.py --synthetic --guard-reference --output build-working/completion-validation/guard-lookup/reference-1.json
```

Each collection rotates five backends through nine repetitions of the same six
active workloads and validates per-retirement direct execution plus identical
endpoints for all modes. Builds/tests/measurements do not overlap. Executable
collections are sequential in opposite orders; this reduces but does not remove
host-time and executable-layout confounds. Report raw candidate/reference emitted
time changes together with improvement versus each collection's fastest existing
backend. No diagnostic timings count as throughput evidence. Native ARM64 remains
deferred. Run unchanged v2 qualification only if competitive active benefit appears.

## Validation and results

Full build passed. All 5 targeted CTests and all 194 full CTests passed; the full
suite took 506.67 seconds, including the five registered performance checks.
The new regressions cover copied/moved artifact ownership, corruption and read
failure at every code byte, scalar rejection before byte reads, scalar-only
continuations, and the existing 10,000 allocation-free remaps on each core.
Observer/machine boundary and per-retirement parity checks passed as well.

Four source-bound nine-repeat collections retained 1,080 raw timing samples.
All collections have the same source snapshot, recorded in `summary.json` and
`tested-source-binding.json`; binary/cache/generated-source hashes were verified
again after collection. `tested.patch` and `tested-new-files.tar.gz` reconstruct
the source from `60afc9f`, including the prior uncommitted profiling work.

Results below use the median elapsed-time gain. Positive is faster; negative is
slower. Candidate/reference executable collections are sequential, not temporally
matched pairs, so these raw differences are exploratory and are not qualification
confidence intervals. The existing-backend comparison provides an additional
within-collection control. The raw JSON retains both generic and direct results,
CPU/host/compiler details, governor settings, affinity, loads and collection times.

| Core/workload | Raw gain vs reference, session 0 / 1 | Gain vs fastest existing, session 0 / 1 |
|---|---|---|

| gameboy/compute-heavy | -5.48% / -5.39% | -13.76% / -12.77% |
| gameboy/mixed-devices | +12.36% / -6.95% | -7.43% / -11.38% |
| gameboy/ram-heavy | +20.28% / -16.25% | -16.00% / -12.81% |
| gamegear/compute-heavy | -11.24% / -19.13% | +3.12% / -16.74% |
| gamegear/mixed-devices | +9.35% / +4.60% | -8.31% / -2.60% |
| gamegear/ram-heavy | +0.51% / +46.50% | -10.00% / -12.32% |

The code-byte guard lookup cache does **not** establish repeatable competitive
active benefit. Game Boy compute regressed versus the search reference in both
sessions and remained about 13% slower than its fastest existing backend.
Game Gear compute likewise regressed versus the reference in both sessions;
its existing-backend comparison changed from a small positive point estimate to
a substantial regression. Game Gear mixed work improved versus the reference in
both sessions but remained slower than its fastest existing backend. RAM and
Game Boy mixed raw gains changed sharply across sessions. Host load and
executable-layout effects remain possible confounds; the large inconsistent raw
gains cannot be assigned to this small metadata lookup change.

The candidate remains research code only. No ordinary executor policy or
acceleration admission changed. No full v2 qualification was run: the active
screen does not justify it. Historical qualification and profiling evidence are
preserved. All changes remain uncommitted.

## Next step

Do not combine this cache with another broad sweep or infer a causal speedup from
the inconsistent raw timings. The next stronger entry-guard experiment should
address the repeated mapped byte lookup itself: investigate a checked contiguous
code view for exact byte comparison, with explicit mapping/window boundaries and
the same code bytes, generation, lifecycle, observer, writable-code and fault
checks. Keep it as a separate candidate against this retained reference. A safe
view must not retain live CPU/memory pointers in bindings or bypass observable
retirement boundaries.

For invocation overhead beyond the guard lookup, inspect the before/retire hook
chain and repeated policy lookup separately. Preserve subclass/virtual policy
semantics and callbacks that can change machine state; do not hoist live policy
or lifecycle checks merely because current corpus cases do not mutate them.
The present experiment does not isolate those remaining invocation costs.

## Commit checkpoint, 2026-10-11

The implementation, tests, collectors and these experiment reports are included
in the requested commit checkpoint. Earlier statements about uncommitted work
describe collection-time status. Generated build and raw timing artifacts remain
local and excluded from Git. Recorded hashes continue to identify the original
tested snapshots; adding these previously ignored reports changes the checkout
source digest and does not rebind historical measurements to the commit.
