# T.I.M.E. acceleration reevaluation, version 1

Kind: current research acceptance contract. Status: **partial research evidence;
acceptance pending**.

This completion program reopens native research for the existing Game Boy and
Game Gear families. It does not reverse the earlier backend's expansion no-go,
make native execution the default, or count the historical spike as new evidence.
The current baseline remains the default.

Measure baseline, cached/block and portable IR on both Linux x86-64 and ARM64.
Record corpus ROM hashes, source digest, compiler/version, effective CMake cache,
CPU/model/governor, backend/policy settings, repetitions, warmup, raw samples,
medians and variability. Choose the fastest correctness-validated existing
backend independently for each core and host as the comparator.

Evaluate a redesigned backend that emits useful work for whole blocks, with
per-instruction retirement and side exits. Require code and mapping guards,
RAM-code invalidation, lifecycle/generation invalidation, W^X executable-memory
protection, complete register/cycle/effect correctness and the existing latency
gates. A trampoline that merely returns to the interpreter is not whole-block
host work.

A go requires exact differential correctness, all existing latency gates, at
least 10% median representative-corpus improvement, and no repeatable individual
case regression above 5%. Publish the samples and reviewed decision for each
core/host. Do not expand opcode/backend coverage until the design review passes.

A newly reviewed no-go can close this research row only after the new baseline
and backend evaluation. Record the reviewer, reason, measured failures or benefit,
and source/build-bound artifacts. Missing ARM64 runners, failing ordinary
features and unavailable testing cannot be replaced with a no-go. No decision
has been made by this document; all four core/host evaluations remain pending.

## Reproducible baseline corpus

`time-measure-acceleration-corpus` compares baseline, cached-block policy, and
portable IR for compute, canonical RAM, and device-access loops on both cores.
Each sample warms the same machine for 4,096 instructions before measuring
100,000 instructions; nine repetitions rotate backend order. Complete
machine fingerprints and retired cycles must agree before output is published.
The CTest smoke variant uses 512 measured instructions and one repetition.
These synthetic cases establish measurement plumbing, not representative game
coverage or per-instruction differential acceptance for a redesigned backend.
Game Gear cached-block policy uses its existing guarded IR implementation.

Run `python3 tools/measure_acceleration.py --build build-working --output
build-working/completion-validation/acceleration/native-baselines.json` to save
raw samples, medians, spread, ROM hashes, source/build bindings, effective cache,
and host/governor details. For ARM64, pass the cross-build directory and
`--launcher /tmp/time-arm64-sdk/root/usr/bin/qemu-aarch64-static -L
/tmp/time-arm64-sdk/root/usr/aarch64-linux-gnu` after the other arguments.
Launcher measurements are explicitly labeled emulated throughput; they cannot
satisfy native ARM64 performance acceptance. Representative game coverage,
latency evidence, and reviewed go/no-go decisions remain pending.

The [whole-block emission pilot](time-whole-block-pilot.md) adds a separately
versioned internal research contract and build-time native code emission for
both selected host compilers. Its IR-model differential tests and dispatch
measurements are preliminary research evidence. Real-core integration was
reviewed in `7b8eb12`; per-instruction RAM/mapping/interrupt/checkpoint boundary
coverage now supplements it. The real-core `whole-block-corpus` experiment adds
baseline-versus-emitted comparisons for synthetic compute-heavy, RAM-heavy, and
mixed device-access patterns with per-retirement correctness checks, separate
timing, coverage accounting and comparison to the fastest validated existing
backend per case. See the pilot document for the exact procedure and evidence;
these patterns do not establish representative game coverage. Latency evidence
and reviewed decisions remain pending. The user deferred native ARM64 performance;
QEMU continues to supply correctness evidence only. This deferral does not close
the research requirement or open admission.

The 2026-10-08 x86-64 real-core pattern run passed all recorded per-retirement and
timed endpoint comparisons and produced 216 raw timing samples. Emitted Game Boy
execution took 13.04–20.05% less time than baseline, but 18.47–30.81% more time than
the fastest validated existing path. Emitted Game Gear execution took
69.87–117.13% more time than its baseline comparator. These are synthetic,
segmented AOT measurements on one host, not the representative-game acceptance
result. The full CTest run passed 183/184 checks; the existing headless scheduling
latency gate also failed on an isolated rerun. The pilot document records the raw
artifact path, exact procedure, host/build binding and validation limits. Neither
a passing acceleration result nor a reviewed no-go is established.
