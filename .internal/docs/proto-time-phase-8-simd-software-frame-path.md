# Phase 8: SIMD-Optimized Software Frame Path

Status: implemented and verified on 2026-05-31.

## Scope

- Added `machine/plugins/video/SimdPixelOps.hpp` and `.cpp` as the pixel-operation SIMD abstraction.
- Added x86 SSE4.2 target flags for `time-simd-pixel-ops` when building with GCC/Clang on x86.
- Added `BMMQ_SIMD_FORCE_SCALAR=1` to force scalar fallback for validation and benchmarking.
- Wired RGB565 presenter upload through `SimdPixelOps::convert_argb8888_to_rgb565`.
- Wired blank/debug frame fills through `SimdPixelOps::fill_pixels`.
- Wired visual override replacement through `SimdPixelOps::replace_pixels_with_mask`, preserving existing per-pixel replacement sampling semantics.

## Correctness Verification

Commands run from repository root:

```bash
cmake -S . -B build-working
cmake --build build-working --target time-smoke-simd-pixel-ops time-smoke-video-engine -j4
ctest --test-dir build-working -R 'smoke_simd_pixel_ops|smoke_video_engine$' --output-on-failure
```

Focused result: `2/2` tests passed.

The full suite was run after the final implementation pass:

```bash
cmake --build build-working -j4
ctest --test-dir build-working --output-on-failure
```

## Scalar vs SIMD Microbenchmark

Temporary benchmark source: `/tmp/proto-time-phase8-bench.cpp`. It exercises a 640x480 software frame for RGB565 conversion, masked visual replacement, and fill. Scalar build used `-DBMMQ_SIMD_FORCE_SCALAR=1`; SIMD build used the production x86 `-msse4.2` path.

Results:

| Operation | SIMD ns/frame | Scalar ns/frame | Notes |
| --- | ---: | ---: | --- |
| `convert_argb8888_to_rgb565` | 147820 | 157570 | SIMD faster; checksums matched |
| `replace_pixels_with_mask` | 68761 | 162297 | SIMD faster; checksums matched |
| `fill_pixels` | 24914 | 24944 | effectively tied |

Both benchmark variants reported the same checksum: `5350312599790`.

## Residual Notes

- SIMD detection still has scalar fallback and can be forced with `BMMQ_SIMD_FORCE_SCALAR=1`.
- Default CTest remains correctness-focused; the benchmark is documented as an implementation validation artifact, not a timing gate.
