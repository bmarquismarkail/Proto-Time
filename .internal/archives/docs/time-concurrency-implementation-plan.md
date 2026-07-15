# T.I.M.E. Emulator Concurrency Implementation Plan

## Status: Active Reference

This plan is derived from the concurrency research in `time-deep-research-improving-concurrency.md` and tailored to Proto-Time's current codebase. It defines the three-lane architecture, implementation roadmap, and design decisions for moving forward.

---

## Current State Assessment (as of 2026-05-15)

Proto-Time has already made significant progress toward the recommended architecture. The following table shows what is already implemented versus what still needs work:

| Component | Research Recommendation | Current Status | Gap |
|-----------|------------------------|----------------|-----|
| **Audio callback** | Drain-only consumer of prepared FIFO | PARTIALLY DONE | `SdlAudioOutputBackend::drainAudioCallback()` calls `service_->drainReadyOutput()`, which is a drain path. But it also does epoch checking, stale-block skipping, and wake requests inside the callback — not pure zero-fill-on-underrun. The output transport worker thread already produces ready blocks via `produceReadyOutputBlock()`. |
| **Audio FIFO** | SPSC lock-free ring buffer for raw PCM | PARTIALLY DONE | AudioEngine uses a lock-free MPMC-style ring buffer with atomic read/write indices. Output transport uses a ready-block queue (not truly SPSC — it has epoch-based invalidation). Not a wait-free SPSC as recommended. |
| **Video presentation** | Latest-frame mailbox, depth-2, hardware-backed presenter | PARTIALLY DONE | VideoEngine has a SPSC-style mailbox with dirty-flag atomics and `drop_oldest` semantics. Depth is configurable (default 2 frames). BUT: SdlVideoPresenter uses `SDL_TEXTUREACCESS_STREAMING` + `SDL_UpdateTexture` + `SDL_RenderCopy`, which is not truly hardware-accelerated presentation — it's a software fallback path even when an accelerated renderer exists. |
| **Timing/sleep** | Separate sleep quantum from instruction eligibility | DONE | TimingService/TimingEngine split with `executionSliceSeconds`, `frontendServiceSliceSeconds`, `minSleepQuantum`, adaptive sleep with spin windows. The timing jitter fix from the brief has been implemented. |
| **Machine thread isolation** | Emulation thread owns guest state, never blocks on UI/audio/GPU | MOSTLY DONE | Guest stepping runs on its own lane while SDL setup, event pumping, presentation, and teardown remain on the process main thread. Realtime video publication is lock-free and rich MachineView copies are tooling-only. |
| **Background task pool** | Work-stealing for non-real-time work | PARTIALLY DONE | BackgroundTaskService exists and is used for visual pack reload polling. Not a full work-stealing pool yet. |
| **Render/UI thread separation** | Host thread owns event pump and present, separate from machine loop | DONE | The process main thread is the SDL UI/render lane, as required by SDL thread affinity; guest emulation runs on a separate thread and publishes immutable frames. |
| **Slim RT video/audio packets** | Narrow data contract for real-time paths | DONE | RealtimeVideoPacket v3 carries a complete independently decodable indexed surface: Indexed2 for Game Boy and Indexed5 for Game Gear's 32-entry CRAM. VDP diagnostics travel in a separate sidecar; VRAM/OAM snapshots remain tooling-only. |

---

## Three-Lane Architecture (Target State)

```
  +------------------+     +------------------+     +------------------+
  | Emulation Thread |     | Audio Lane       |     | UI/Render Thread |
  |                  |     |                  |     |                  |
  | - CPU/APU/PPU    |---->| Raw PCM FIFO     |---->| (separate thread)|
  | - Timing         |     | -> Mixer/SRC     |     |                  |
  | - Event emission |     | -> Ready FIFO    |     | - Event pump     |
  | - Guest state    |     |                  |     | - Present        |
  +------------------+     +------------------+     | - GPU upload     |
                                                     | - Input handling |
  +------------------+                               +------------------+
  | Background Lane  |
  |                  |
  | - Visual packs   |
  | - Screenshots    |
  | - Save flushes   |
  | - Debugger       |
  +------------------+
```

### Thread Ownership Rules

| Lane | Owns | Deadline | Allowed Sync |
|------|------|----------|--------------|
| Emulation thread | Guest CPU/APU/PPU state, timing budget, event emission | Soft real-time, deterministic pacing | Atomics to publish queue indices; no blocking on UI/audio/GPU |
| Audio worker | Resampler state, mixer state, optional FX chain | Real-time-adjacent | SPSC pop from raw FIFO, SPSC push to ready FIFO |
| Audio callback | Device-facing output buffer fill | Hardest deadline in system | Atomic queue reads only; no allocation, no locks, no I/O |
| UI/render thread | Host window, input pump, present, GPU submission | VSync / presentation cadence | Mailbox pop, GPU fences/semaphores, short-lived host locks only |
| Background task pool | Asset decode, visual-pack resolution, screenshots, save flushes | No hard deadline | Task queue / futures / work stealing acceptable |

---

## Implementation Roadmap (Ordered by Impact-to-Risk)

### Phase 1: Stabilize Existing Timing (Low Risk, Already Mostly Done)
**Priority: Highest | Effort: 0-2 days | Risk: Low**

The timing service already has slice-based execution, frontend servicing thresholds, adaptive sleep with spin windows, and min-sleep-quantum guards. This is largely complete per the timing jitter fix brief.

**Remaining tasks:**
1. Verify `TimingEngine::recordExecutionSliceCycles()` properly returns `frontendServiceDue` and `executionSliceComplete` decisions based on `frontendServiceSliceSeconds` and `executionSliceSeconds`.
2. Add a sleep-wake overshoot histogram to TimingStats (already partially present with `sleepOvershootCount`, `sleepOvershootHighWater`).
3. Ensure the spin window doesn't burn CPU when not needed.

**Verification:** Run existing smoke tests. Audio should be steady in normal throttled mode. Frontend responsiveness acceptable.

---

### Phase 2: Instrument Callback Timing and FIFO Occupancy (Low Risk)
**Priority: Highest | Effort: 1-2 days | Risk: Low**

The codebase already has extensive stats tracking (`AudioOutputTransportStats`, `VideoServiceDiagnostics`, presenter duration percentiles, frame age histograms). The real-time diagnostics are well-established.

**Remaining tasks:**
1. Verify all callback duration percentiles (p50/p95/p99/p999) are being recorded and exposed through the diagnostics report path.
2. Add ready-audio FIFO occupancy sampling at each callback tick.
3. Ensure frame build time vs present time are separated in diagnostics.

**Verification:** Run emulator with `--diagnostics-report /tmp/proto-time-diagnostics.json --diagnostics-interval-ms 500` and verify all latency histograms populate correctly.

---

### Phase 3: Make Audio Callback Purely Drain-Only (Medium Risk)
**Priority: Highest | Effort: 3-6 days | Risk: Medium**

This is the single biggest improvement for audio quality. The callback in `SdlAudioOutputBackend::Impl::drainAudioCallback()` currently calls `service_->drainReadyOutput()`, which does epoch checking, stale-block skipping, and wake requests. These should be eliminated from the callback path.

**Changes needed:**
1. In `AudioService::drainReadyOutput()`: Remove epoch-based stale block skipping (handle this in the worker thread instead). Remove `audioCallbackWakeRequest_` store — let the worker manage its own wake logic based on FIFO occupancy.
2. Ensure the output transport worker produces blocks proactively so the callback always has data (or silence to fill).
3. Add a pre-fill buffer: start producing at least 2 ready blocks before the first callback fires.

**Code changes:**
- `machine/AudioService.hpp`: Simplify `drainReadyOutput()` to only: pop block, copy samples, zero-fill remainder on underrun. No epoch checks, no wake requests.
- `AudioService::produceReadyOutputBlock()`: Ensure it runs fast and proactively (wake on FIFO depth < 2).

**Verification:** Run APU-heavy ROM scenes. Measure p99 callback duration — should be under 50us consistently. Listen for pops/clicks/underruns.

---

### Phase 4: Split UI/Render Thread from Machine Loop (Medium Risk)
**Priority: High | Effort: 4-8 days | Risk: Medium**

**Status: Complete (SDL-affine form)**

SDL video and main event handling remain on the process main thread per SDL's development guidance. Guest emulation runs on a separate lane, preventing SDL upload/present work from stalling deterministic machine stepping.

**Changes needed:**
1. Keep the process main thread as the SDL UI/render lane.
2. Run step machine -> emit events -> check timing -> sleep on the emulation thread. No SDL video/event calls occur there.
3. Video frames are published to the existing latest-only mailbox from the emulation thread. The main-thread UI lane consumes it.
4. Audio append (`appendRecentPcm`) is called from the emulation thread (already correct).

**Key design decision:** Use lock-free latest-only publication plus atomic wake state between the emulation producer and main-thread SDL consumer. SDL setup, polling, upload, present, and teardown never migrate to a background thread.

**Code changes:**
- `emulator.cpp`: owns the SDL frontend service loop on the process main thread and runs guest emulation in a joined worker lane.
- `SdlFrontendPlugin`: consumes realtime video on the main thread; the retired internal render worker is disabled for host-thread-affine presenters.

**Verification:** Run sprite-heavy ROM scenes. Verify frame build time no longer affects emulation pacing. Input latency should not increase noticeably.

---

### Phase 5: Convert Video Mailbox to True Latest-Only (Low Risk)
**Priority: High | Effort: 1-2 days | Risk: Low**

VideoEngine already implements a SPSC mailbox with dirty-flag atomics and overwrite semantics. The default `mailboxDepthFrames` is 2. This is already close to the recommended "latest-frame mailbox or depth-2 queue."

**Changes needed:**
1. Ensure `submitPresentPacket()` always overwrites (never queues) — it already does this via the atomic mailbox swap.
2. Verify `tryConsumeLatestFrame()` returns the newest frame and never blocks.
3. Consider reducing to depth 1 (true single-slot mailbox) if no visual artifacts appear.

**Code changes:**
- Minimal: possibly adjust default `mailboxDepthFrames` from 2 to 1 in `VideoEngineConfig`.
- Add a diagnostic counter for mailbox overwrites vs consumes.

**Verification:** Rapid pause/resume/load sequences should not show stale frames. Frame age histogram should cluster near zero.

---

### Phase 6: Slim Real-Time Video/Audio Packets (Medium Risk)
**Priority: High | Effort: 3-5 days | Risk: Medium**

**Status: Complete**

The production contract now publishes a complete immutable indexed scanout surface at VBlank. It does not publish live VRAM/OAM views, PPU commands, or reconstruction deltas. This preserves deterministic PPU ownership on the emulation lane and remains correct when the latest-only mailbox overwrites intermediate frames.

**Changes needed:**
1. For video: publish a self-contained indexed surface from the emulation thread. The SDL main-thread lane performs only color expansion, host processing, upload, and presentation.
2. For audio: The `RealtimeAudioPacket` already carries PCM samples — this is acceptable for the real-time path. But avoid re-deriving full `AudioStateView` if only contiguous PCM is needed.

**Code changes:**
- `RealtimeVideoPacket` v3 owns its surface, lifecycle/generation metadata, and optional upload-region hints.
- Game Boy emits Indexed2 frames with a four-entry palette; Game Gear emits Indexed5 frames because its two 16-entry CRAM banks permit 32 distinct simultaneous colors.
- Every packet is independently decodable; upload regions are hints and never delta dependencies.
- Guest-specific VDP timing/attribute counters use `RealtimeVideoDiagnostics`, a separate sidecar.
- Production realtime construction occurs only at VBlank. `videoState()` and `VideoDebugFrameModel` remain debugger/tooling paths.

**Verification:** Run scenes with rapid memory writes. Verify MachineView construction time decreases. No visual correctness regressions.

---

### Phase 7: Hardware-Backed Presenter (Medium-High Risk)
**Priority: Medium | Effort: 1-3 weeks | Risk: Medium to High**

**Status: Complete**

CPU-produced emulator frames necessarily require a host-to-renderer upload. SDL's
hardware-backed framebuffer path is a persistent streaming texture on an
accelerated renderer, followed by a render copy to the window. A target texture
is an offscreen composition destination; it does not replace the upload and adds
an unnecessary copy when no composition pass is required.

**Changes needed:**
1. Keep SDL video, event, upload, and presentation calls on the process main thread.
2. Preserve the Phase 6 indexed surface through `VideoService` when no processor,
   capture, or non-indexed presenter requires ARGB materialization.
3. Lock one persistent streaming texture and expand indexed pixels directly into
   its mapped storage, then render-copy it directly to the window backbuffer.
4. Confirm acceleration using the renderer's reported flags and retain the
   software/dummy fallback.
5. Measure expansion, upload, render submission, VSync/present wait, total
   presenter time, and publication-to-consumption frame age separately.

**Code changes:**
- `VideoFramePacket` may carry either materialized ARGB pixels or an immutable
  `RealtimeVideoSurface`; presenter capabilities declare indexed-surface support.
- `HardwareVideoPresenter` uses `SDL_LockTexture`/`SDL_UnlockTexture`, direct
  palette expansion into the locked texture, one `SDL_RenderCopy`, and
  `SDL_RenderPresent`.
- The redundant intermediate target-texture copy was removed. Target support is
  reported for future composition features but is not used without a real pass.
- Compatibility `lastFrame()` inspection materializes ARGB lazily rather than on
  every realtime presentation.
- Presenter diagnostics report actual renderer flags, direct-indexed versus ARGB
  frames, texture locks, and per-stage latency/high-water values.
- `SdlVideoPresenter` remains the software-policy path and
  `HardwareVideoPresenter` falls back to an SDL software renderer when needed.

**Verification:** Dummy-driver smoke coverage verifies direct indexed upload,
fallback behavior, persistent texture reuse, and stage diagnostics. On real
hardware, validate `SDL_RENDERER_ACCELERATED` in the reported flags and budget
CPU expansion + upload + render submission independently from VSync wait. A
dedicated Vulkan/D3D12/Metal backend remains deferred unless these measurements
show the SDL accelerated renderer is inadequate.

---

### Phase 8: SIMD-Optimized Software Frame Path (Medium Risk)
**Priority: Medium | Effort: 3-7 days | Risk: Medium**

**Status: Complete**

Even with a hardware presenter, a fast software fallback is valuable for compatibility and debugging.

**Implemented completion scope:**
1. `SimdPixelOps` now selects scalar, SSE2, SSE4.1/SSSE3, AVX2, or NEON at
   runtime. The target-wide `-msse4.2` requirement was removed, and a forced
   scalar build verifies the portable fallback independently.
2. `SdlVideoPresenter` accepts slim indexed surfaces, locks the SDL texture,
   and expands indexed or ARGB input directly into the texture pitch. The
   temporary RGB565 frame buffer and `SDL_UpdateTexture` copy were removed.
3. Visual override mask/replacement storage is reused after first sizing, and
   masked replacement uses the selected SIMD backend. Lookup and apply stages
   have separate sample, total, and high-water diagnostics.
4. The unused scanline alpha compositing API was removed rather than retaining
   an unmeasured SIMD surface with no production caller.
5. Diagnostics report the selected SIMD backend and software-present expansion,
   upload, render-submit, and total stage durations.

**Code changes:**
- New file: `machine/plugins/video/SimdPixelOps.hpp` — portable SIMD intrinsics abstraction.
- Modify frame build and presenter upload paths to use SIMD where beneficial.

**Verification:** Native and forced-scalar smoke targets compare randomized
pixel output with scalar references, including direct indexed-to-RGB565 writes
with padded destination pitch. The retained `time-perf-simd-pixel-ops`
benchmark reports p50/p95/p99 timings for Game Boy (160x144) and Game Gear
(256x192) frame sizes. Across verification runs on the implementation host,
AVX2 measured about 2.2x-3.8x p50 speedup for ARGB-to-RGB565 and 12.9x-14.9x
for masked replacement; these
numbers are evidence from one host, not cross-platform performance gates.

---

### Phase 9: Full Background Task Pool (Low-Medium Risk)
**Priority: Medium | Effort: 3-5 days | Risk: Low to Medium**

**Status: Complete.** The retained pool is a bounded work-stealing implementation
with configurable production sizing, categorized queue/latency diagnostics,
staged visual reload and image decode, serialized background capture, coalesced
save retry with a shutdown durability fence, immutable-state debug processing,
and overload behavior that does not move nonessential work back onto a deadline
lane. See `.internal/docs/proto-time-phase-9-background-task-pool.md`.

`BackgroundTaskService` exists but is a simple task queue, not a work-stealing pool.

**Changes needed:**
1. Implement a thread pool with one thread per available core (or configurable).
2. Add work-stealing for load balancing.
3. Move visual-pack decode, screenshot capture, save flushes, and debugger snapshots into this pool.

**Code changes:**
- Replace `BackgroundTaskService` with a proper task pool implementation.
- Update all call sites to use the new interface.

**Verification:** Run with visual packs loaded. Verify pack decoding doesn't cause emulation stuttering.

---

### Phase 10: Basic-Block Cache / Threaded Interpreter (High Risk)
**Priority: Later | Effort: 2-4 weeks | Risk: High**

**Status: Complete.** The Game Boy runtime now has an opt-in, emulation-lane-
owned translated block cache with direct PC slots, safe sequential chaining,
mapping-generation and overlapping-write invalidation, baseline fallback, CPU
mode configuration, diagnostics, deterministic full-state comparison, and a
retained >=2x CPU-throughput gate. Each runtime and machine step still retires
one guest instruction. See
`.internal/docs/proto-time-phase-10-threaded-block-cache.md`.

CPU acceleration should only come after the latency pipeline is stable. Proto-Time's Game Boy runtime already distinguishes a "fast execute" path from baseline stepping.

**Changes needed:**
1. Implement a basic-block cache with guard-checked invalidation.
2. Cache decoded/translated blocks, chain them when safe.
3. Invalidate on guest state changes that affect execution flow.

**Code changes:**
- New file: `inst_cycle/BlockCache.hpp` — block storage and lookup.
- New file: `inst_cycle/BlockTranslator.hpp` — translates guest instruction sequences to native code.
- Modify `RuntimeContext::step()` to check block cache before baseline interpretation.

**Verification:** Instruction throughput increases 2-5x without breaking deterministic behavior. All smoke tests pass. TSAN clean.

---

### Phase 11: Guarded JIT / DBT (Very High Risk)
**Priority: Last | Effort: 4-8+ weeks | Risk: Very High**

**Phase 11A status: Complete.** The first slice deliberately stops before native
code generation. It defines a versioned Game Boy execution ABI, lowers a
conservative LR3592 subset into architecture-neutral IR, interprets one lowered
guest instruction at a time on the emulation lane, validates mapping/helper/
execution-state/code-byte guards, and falls back to the Phase 10 byte path for
unsupported or invalidated entries. The host timing loop now requests bounded
machine slices, while machine/device retirement and the host timing observer run
synchronously after every instruction. See
`.internal/docs/proto-time-phase-11a-portable-ir.md`.

**Phase 11B status: Complete.** Portable IR guards now use side-effect-free
peeks limited to stable executable storage, dispatch uses the cache-provided
instruction index, and exit, retirement, signed comparison, and shift semantics
are architecture-neutral and explicitly validated. Guard, lowering, and IR
execution costs are observable; randomized opcode differential tests and mixed
boundary/corpus tests cover the lowered subset and unsafe-state fallbacks.
Measurement shows that the current per-instruction IR interpreter does not yet
recover the Phase 10 block-cache speedup, so opcode expansion is deliberately
deferred until a block-level backend can preserve synchronous per-instruction
retirement with less dispatch and guard overhead. See
`.internal/docs/proto-time-phase-11b-ir-hardening-measurement.md`.

**Phase 11C status: Complete.** `RuntimeContext` now supports slice-scoped
backend state while retaining a mandatory return through machine and host
retirement after every guest instruction. The Game Boy portable backend validates
full guards once at block entry, then uses an indexed cursor with mapping, helper,
execution-state, validity, and PC checks between retirements. Overlapping writes,
interrupt/HALT/DMA boundaries, observer exits, and slice budgets reject
continuation before another instruction begins. New diagnostics and differential
tests cover guard amortization and stale-code rejection. The synthetic IR result
improves from approximately baseline speed to about 1.15x baseline, while the
Phase 10 byte backend remains above 2x. See
`.internal/docs/proto-time-phase-11c-block-level-backend.md`.

**Phase 11D status: Complete.** Measurement fidelity and realistic-corpus
closure remove per-instruction wall-clock probes from normal IR execution,
retain them behind explicit `--cpu-detailed-timing` opt-in, and make the Phase
10 throughput gate unconditional in release-style test builds. A production
external-ROM runner now compares baseline, block, and IR retired instructions,
cycles, and deterministic full-machine fingerprints while reporting repeated
per-ROM and aggregate performance without treating noisy host timing as a
correctness gate. A reproducible 20-ROM sample completed with no state or cycle
mismatches; realistic IR speed remained close to baseline with low aggregate
coverage, so opcode expansion remains deferred. See
`.internal/docs/proto-time-phase-11d-measurement-corpus-closure.md`.

**Phase 11E status: Complete — bounded native spike, expansion no-go.** An
x86-64/POSIX backend now compiles the existing validated IR subset into fixed
entry thunks and immutable predecoded execution plans behind the same versioned
ABI, guards, block cursor, fallback path, and synchronous per-instruction
retirement contract. Executable pages transition once from RW to RX, are never
RWX, are instruction-cache synchronized, and are retired with their cache
artifact. Direct mapping tests verify RX/non-W permissions; randomized
differential tests cover every lowered opcode. The backend is available as
`--cpu-mode native`, and the corpus runner compares it alongside baseline,
block, and portable IR. It did not meet the continuation gate: the synthetic
median was 1.26x baseline and 0.625x the Phase 10 backend, while the 18 runnable
ROM sample had a 1.02x geometric-mean speedup versus 1.07x for Phase 10. Do not
expand opcode coverage or add ARM64 for this thunk/predecoded-plan design. See
`.internal/docs/proto-time-phase-11e-native-backend-spike.md`.

**Phase 11 post-closure hardening status: Complete.** The production CPU focus
returns to Phase 10. Cached byte execution now consumes immutable translated
instruction spans directly instead of copying every instruction through a
temporary `fetchBlock`. Its performance gate uses nine alternating-order paired
runs, twice the former instruction count, a 2.05x median requirement, and a
2.0x lower-quartile requirement. The measured paired median is 2.24x with a
2.23x lower quartile. The corpus defaults to architecture-neutral baseline,
block, and IR modes; native is explicit and capability-checked. Uniform
`ROM too large` rejection is classified as unsupported rather than a backend
failure, and irrelevant non-Game-Boy archives no longer produce discovery
noise. Native opcode and architecture expansion remain frozen.

**Phase 10B hardening status: Complete.** Block formation now stops before an
opcode the cached byte executor cannot handle, with sparse opcode diagnostics
for eligibility stops and unexpected runtime fallback. A single committed-write
observer owns guest-write invalidation, so MMIO intercepted or hardware-rejected
writes do not churn code guards. A conservative executable-page index rejects
unrelated writes before a cache scan while preserving exact overlap and mapping
generation invalidation. The corpus aggregates block lookup, translation,
chaining, fallback, and invalidation-work rates. A deterministic 20-ROM run had
zero mismatches and zero unsupported cached fallbacks (previously 46.6% per
translation), rejected 99.897% of write invalidations at the page index, and
measured a 1.1125x block geometric-mean speedup. The retained synthetic gate
passed at 2.131x paired median and 2.126x lower quartile.

Full just-in-time compilation is the highest upside but also the largest correctness burden. Defer until all other phases are stable.

**Considerations:**
- Determinism tradeoffs: JIT'd code may not execute in exactly the same cycles as interpreted code.
- Security: Treat ROM input as untrusted data — JIT requires careful sandboxing.
- Platform support: x86_64 JIT is straightforward; ARM64 has stricter memory access controls.

---

## Key Design Decisions

### 1. SPSC Ring Buffer for Audio
Use a wait-free SPSC ring buffer (as shown in the research doc) for the raw-audio FIFO between emulation thread and audio worker thread. The existing `AudioEngine` ring buffer is MPMC-style — consider replacing with a true SPSC implementation.

```cpp
template <typename T, size_t CapacityPow2>
class SpscRing {
    // Lock-free push/pop with cache-line padding to avoid false sharing
};
```

### 2. Latest-Frame Mailbox for Video
VideoEngine's current mailbox is already close to the recommended design. The atomic dirty-flag swap provides SPSC semantics. Keep this pattern.

### 3. Generation/Epoch Barriers
Use monotonically increasing generation counters to separate ROM loads, resets, and save-state restores. Both AudioService and VideoService already have lifecycle epoch counters — extend this pattern across all lanes.

### 4. MachineView Copy Reduction
Keep rich `MachineView` for debugger/tooling plugins but add slim RT packets for the real-time pipeline. This trims memory traffic without losing debugging capability.

---

## Benchmark Plan

Measure interference, not just average throughput. Audio problems hide in tails; video problems also hide in tails.

| Measure | Target Instrumentation |
|---------|----------------------|
| Audio callback duration p50/p95/p99/p999 | Timestamp callback start/end |
| Ready-audio FIFO occupancy over time | Sample queue depth every callback and every mixer tick |
| Sleep overshoot and catch-up burst size | Log requested vs actual wake time and instructions executed after wake |
| Frame build time and present time | Timestamp `submitVideoState`, queue publish, upload, present |
| Video queue/mailbox age | Stamp frame generation + publish/present timestamps |
| Snapshot copy cost | Measure `MachineView::videoState()` / `audioState()` construction time |
| Lock contention / races | TSAN builds; short lock-duration histograms in non-RT mutexes |

### Test Scenarios
1. Idle ROM scene (minimal APU/video activity)
2. APU-heavy scene with sustained tone/noise changes
3. Sprite/background-heavy scene
4. Rapid pause/reset/load loop
5. Window-hidden/window-visible loop
6. Throttled vs unthrottled comparison

Test on at least two host classes: lower-core machine with integrated graphics and higher-core desktop.

---

## Open Questions

These constraints materially affect the exact shape of the final implementation and should be resolved before proceeding to Phases 4+:

| Unknown | Impact on Design |
|---------|-----------------|
| Target host OS set | Determines whether WASAPI, ALSA, CoreAudio, ASIO, Metal, Vulkan, D3D12, or SDL-only paths are realistic first targets. |
| Required determinism / replay / rollback guarantees | Affects how aggressive concurrency can be, especially if future rewind or netplay is planned. |
| Whether JIT is acceptable | Changes the long-term CPU roadmap substantially. |
| Minimum supported hardware | Decides whether hardware rendering is mandatory or software fallback remains a first-class path. |
| Power budget vs latency budget | Small audio periods improve latency but raise wake frequency and power use. |
| Whether visual-pack/debugger paths are development-only or user-facing in release | Determines how much work should be isolated behind background queues now. |

---

## Files to Watch / Modify

| File | Phase | Notes |
|------|-------|-------|
| `emulator.cpp` | 4, 6 | Main loop restructuring |
| `machine/AudioService.hpp` | 3, 6 | Simplify drain path |
| `machine/plugins/AudioEngine.hpp` | 3 | SPSC ring buffer replacement |
| `machine/VideoService.hpp` | 5 | Mailbox depth tuning |
| `machine/plugins/video/VideoEngine.hpp` | 5 | Confirm overwrite semantics |
| `machine/plugins/video/adapters/SdlVideoPresenter.cpp` | 7, 8 | Hardware presenter path |
| `machine/plugins/sdl_frontend/SdlAudioOutput.cpp` | 3 | Callback simplification |
| `machine/TimingService.hpp` | 1 | Verify existing timing logic |
| New: `machine/UiRenderThread.hpp` | 4 | Separate UI/render thread |
| New: `memory/SpscRing.hpp` | 3 | Lock-free SPSC ring buffer |

---

## Success Criteria

The concurrency redesign is complete when:

1. **Audio**: p99 callback duration under 50us, zero audible pops/clicks/underruns in normal operation, steady playback across all test scenarios.
2. **Video**: Frame presentation does not stall emulation pacing, frame age stays under 16ms (one frame at 60Hz), no visible stuttering.
3. **Input**: Input latency under 16ms (one frame) from keypress to visual feedback.
4. **CPU**: No lock contention on the emulation hot path; all synchronization is atomic-only.
5. **Memory**: MachineView construction cost under 10us for video events, under 5us for audio events.
6. **Stability**: TSAN clean builds pass all smoke tests.
7. **Determinism**: Same ROM input produces identical output frames (byte-for-byte) regardless of concurrency changes.
