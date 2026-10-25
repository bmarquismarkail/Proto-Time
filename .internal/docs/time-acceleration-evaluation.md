# T.I.M.E. acceleration reevaluation, version 2

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

Criteria v2 replace the mandatory 10% gameplay gain with measured active benefit
and representative non-regression. Ten percent remains an optimization target.
The versioned numerical contract is `tools/acceleration-criteria-v2.json`; the
paired evaluator is `tools/acceleration_criteria_v2.py` and native collector is
`tools/measure_x86_acceptance_v2.py`.

Competitive opt-in qualification requires exact differential correctness, all
existing latency gates/full CTest, no individual ROM or active case with a
repeatable regression above 5%, and a 95% paired confidence lower bound at least
-5% for every case. Compute-heavy and mixed-device must each show a positive
confidence lower bound and at least eight of nine positive pairs on each core in
two independent preregistered confirmation sessions. RAM-heavy remains a mandatory
non-regression case. Neutral gameplay can qualify for opt-in review. Default
eligibility additionally requires a positive equally weighted representative
confidence lower bound and independent sustained-active ROM diversity evidence;
that additional diversity is currently not run, so default promotion stays closed.
No evaluator enables a backend or changes family admission.

Nine repetitions rotate backend and variant order. Confidence uses 10,000 seeded
percentile bootstrap resamples of paired log time ratios; aggregate resampling
preserves whole-round case pairing. Preserve the old ratio-of-medians metric and
its median-or-majority regression rejection. Intervals overlapping the allowed
margin are inconclusive. A single preregistered 27-repeat follow-up may resolve
an inconclusive result; no repeat-until-pass collection is allowed. Hash these
rules and this authority with source/build/ROM evidence. V1 history remains v1.
Native ARM64 performance is still user-deferred. Both cores/hosts and explicit
review remain required by the broader completion contract.

A newly reviewed no-go can close this research row only after the new baseline
and backend evaluation. Record the reviewer, reason, measured failures or benefit,
and source/build-bound artifacts. Missing ARM64 runners, failing ordinary
features and unavailable testing cannot be replaced with a no-go. The user has
approved continued whole-block development as **go**. Performance acceptance for
all four core/host evaluations remains pending; that approval does not replace
the correctness, latency, representative-corpus or native-host requirements.

## Reproducible baseline corpus

The [native x86-64 acceptance procedure](time-x86-acceleration-acceptance.md)
collects source-bound active and authored-ROM measurements, repeated
existing performance gates and full CTest evidence. It evaluates thresholds
without opening admission or substituting for deferred native ARM64 measurements.

The [standalone game corpus](time-representative-rom-corpus.md) adds the separate
`whole-block-roms` experiment: both Collect and Exit revisions and their ports,
ROM-bound scenarios, bank-aware native coverage and sampled retirement checks.
This expands authored-game coverage; it does not broaden production policy or
close performance, latency or native ARM64 acceptance.

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
these patterns do not establish representative game coverage. Performance
acceptance remains pending; the development go approval is recorded above. The
user deferred native ARM64 performance;
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

The [ROM continuation optimization](time-whole-block-pilot.md#approved-design-rom-continuation-optimization-2026-10-08)
records the subsequent source/build-bound evaluation. Paired process diagnostics
improved over the previous emitted path, while the standard corpus still found
6.05–14.44% regressions against the fastest validated existing paths. These
measurements do not meet the performance acceptance threshold or change admission.
