# Phase 4 Render Thread Scaffold

This directory preserves an experimental UI/render-thread scaffold that was found untracked in the live `machine/` tree.

It is intentionally quarantined under `.internal/experiments/` because it is not buildable against the current codebase:

- `machine/VideoService.cpp` defines constructors and methods that are not declared by the current `machine/VideoService.hpp`.
- `machine/threading/UiRenderThread.hpp` contains duplicate nested `BMMQ` namespaces and references SDL and project types without the required declarations.
- The scaffold conflicts with the current integrated render-service implementation in `machine/plugins/sdl_frontend/SdlFrontendPlugin.cpp`.

Treat these files as reference material only. Future Phase 4+ render-thread work should start from the current SDL frontend render-service path unless this scaffold is rewritten and wired into CMake with tests.
