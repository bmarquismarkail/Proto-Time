# Phase 10 Block Cache Invalidation Rules

Phase 10 uses a guarded, deterministic block cache over the LR3592 fast interpreter. Cached bytes are only used when the guard for the current program counter is valid and the fetched bytes still match the cached bytes. A cache miss or invalid guard falls back to the canonical fetch/decode/execute path.

## Runtime Control

- `GameBoyMachine::setBlockCacheEnabled(false)` disables cache lookup and population at runtime.
- Disabling the cache clears existing cached blocks and statistics so A/B comparisons can start from a known state.
- Re-enabling the cache resumes normal miss/populate/hit behavior.

## Invalidation Sources

- Writes to normalized addresses below `0x8000` invalidate the full cache. This covers cartridge control writes, ROM banking writes, and any write path that can change the visible ROM window.
- Writes to `0xFF50` invalidate the full cache because boot ROM visibility changes the executable bytes visible at low addresses.
- Writes to writable memory at or above `0x8000` invalidate cached blocks whose byte range overlaps the written range.
- Full ROM loads call `invalidateAllBlockCache()` after installing the visible memory map and startup state.
- If a cache hit observes fetched bytes that no longer match the cached bytes, the guard for that program counter is invalidated and execution falls back.
- If the fast interpreter cannot execute a cached opcode, the guard for that program counter is invalidated and execution falls back.

## Control Flow

Branch, call, return, restart, interrupt-return, HALT, DI, and EI behavior is not speculated by the cache. The cached path still executes through the LR3592 fast interpreter and then retires through the normal instruction-retirement path, preserving PC, cycle accounting, IME scheduling, HALT state, and device ticking semantics.

## Current Phase Boundary

Phase 10 deliberately does not generate native code and does not chain blocks. Native JIT/DBT work remains Phase 11. Phase 10 completion depends on deterministic guarded cache behavior, a runtime disable switch, invalidation coverage, trace/smoke equivalence checks, and measurement hooks.
