# Phase 8: SIMD Software Frame Path

## Status

Complete.

## Delivered contract

- Pixel operations select scalar, SSE2, SSE4.1/SSSE3, AVX2, or NEON at runtime.
- The build no longer requires SSE4.2 globally; a forced-scalar library and smoke
  target keep the fallback independently testable.
- The SDL software presenter accepts slim indexed packets and expands indexed or
  ARGB input directly into a pitch-aware locked texture. It does not allocate a
  temporary RGB565 framebuffer or use `SDL_UpdateTexture`.
- Visual override mask and replacement buffers are retained and reused, with
  separate lookup and SIMD-apply duration diagnostics.
- The unused scanline-alpha compositing surface was removed.
- JSON diagnostics expose the active SIMD backend and presenter/override stages.

## Verification

`smoke-simd-pixel-ops` and `smoke-simd-pixel-ops-scalar` compare randomized
output with scalar references and cover padded-pitch indexed-to-RGB565 writes.
`smoke-video-transport` covers direct indexed SDL software presentation and
texture/stage counters. `smoke-visual-override` covers the lookup/apply metrics.

The retained `perf-simd-pixel-ops` CTest benchmark reports p50/p95/p99 for
160x144 and 256x192 frames. Across verification runs on the implementation
host, AVX2 measured about 2.2x-3.8x p50 speedup for ARGB-to-RGB565 and
12.9x-14.9x for masked replacement. These measurements are host evidence, not
portable pass/fail gates.
