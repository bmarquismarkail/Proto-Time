# T.I.M.E. acceleration reevaluation, version 1

Kind: current research acceptance contract. Status: **not run**.

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
