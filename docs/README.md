# HVT Documentation Index

**Hydra Viewport Toolbox (HVT)** builds on OpenUSD Hydra to **simplify** application viewport
integration and to **extend** OpenUSD with additional rendering features and helpers (WBOIT,
outlines, geometry utilities, scene-index filters, pageable buffers, custom tasks, …).

Design docs for HVT subsystems live here. Read them before making architectural changes.

For **usage demonstrations**, see [test/howTos/](../test/howTos/) and
[test/README.md](../test/README.md). For **validation coverage**, see `test/tests/`.

## Tests vs How-tos

Both directories compile into the same test binary and run under `ctest`, but their roles differ:

- **`test/tests/`** — unit and image-comparison tests that validate the library. Expect **several
  tests per feature** (params, construction, behavior, regressions, baselines).
- **`test/howTos/`** — runnable examples that show **how to integrate** a task, helper, or
  workflow. There is typically **one How-to per feature**; they also pass as tests, but
  demonstration is the main purpose.

When extending a feature, add or update unit tests in `test/tests/` for coverage and add or
update the corresponding How-to only when the public usage story changes.

## Recommended reading order

1. [framepass.md](framepass.md) — core rendering pipeline
2. [taskmgr.md](taskmgr.md) — task registration, ordering, commit functions
3. [renderbuffermgr.md](renderbuffermgr.md) — AOV / render buffer management
4. [lightingmgr.md](lightingmgr.md) — built-in lights

Then read feature-specific docs as needed.

## Architecture

Core frame-pass infrastructure and managers — primarily **simplify** Hydra viewport integration.

| Doc | Topic | Key headers | Unit tests (`test/tests/`) | How-to (`test/howTos/`) |
|-----|-------|-------------|----------------------------|-------------------------|
| [framepass.md](framepass.md) | `FramePass` lifecycle, preset task lists, per-frame execution | `include/hvt/engine/framePass.h`, `framePassUtils.h` | `testFramePass.cpp`, `testFramePasses.cpp` | `howTo02`, `howTo03` |
| [taskmgr.md](taskmgr.md) | `TaskManager`: task storage, ordering, commit fns, flags | `include/hvt/engine/taskManager.h`, `taskCreationHelpers.h` | `testTaskManager.cpp`, `testTaskHelpers.cpp` | `howTo04`, `howTo10` |
| [renderbuffermgr.md](renderbuffermgr.md) | Render buffer Bprims and AOV settings | `include/hvt/engine/renderBufferManager.h` | `testFramePass.cpp` | — |
| [lightingmgr.md](lightingmgr.md) | Dome and camera lights as Sprims | `include/hvt/engine/lightingSettingsProvider.h` | — | `howTo11_UseSkyDomeTask.cpp` |

## Features

HVT-specific rendering features — **extensions** to stock OpenUSD/Hydra (see also `include/hvt/tasks/`,
`sceneIndex/`, `geometry/`).

| Doc | Topic | Key source | Unit tests (`test/tests/`) | How-to (`test/howTos/`) |
|-----|-------|------------|----------------------------|-------------------------|
| [outline.md](outline.md) | GPU selection/highlight outlines (ID-buffer edge detection) | `include/hvt/tasks/outline/`, `include/hvt/resources/shaders/outline*.glslfx` | `testOutlineTasks.cpp` (many cases) | `howTo20_UseOutlineTasks.cpp` |
| [wboit.md](wboit.md) | Weighted blended order-independent transparency | `include/hvt/tasks/wboitRenderTask.h`, `wboitResolveTask.h` | `testWboitTask.cpp` | `howTo19_UseWBOITRenderTask.cpp` |

## Build and tooling

| Doc | Topic |
|-----|-------|
| [build.md](build.md) | Configure/build/troubleshoot: presets, vcpkg & OpenUSD, fix-compile agent workflow |
| [testing.md](testing.md) | Run/filter tests, baselines, platform skips, adding coverage |
| [vcpkg.md](vcpkg.md) | Custom triplets, release-only deps, NuGet cache cleanup |
| [howToCollectTraces.md](howToCollectTraces.md) | OpenUSD performance tracing in tests (`CollectTraces` RAII helper) |

## Other subsystems (no dedicated doc yet)

| Area | Location | Simplify / extend | Notes |
|------|----------|-------------------|-------|
| Viewport engine entry points | `include/hvt/engine/viewportEngine.h`, `engine.h` | Simplify | Renderer and frame pass creation |
| Scene index filters | `include/hvt/sceneIndex/` | Extend | Wireframe, bounding box, display style overrides |
| Geometry helpers | `include/hvt/geometry/` | Extend | Procedural mesh/polyline data sources |
| Render tasks (SSAO, FXAA, blur, compose, …) | `include/hvt/tasks/` | Extend | Custom tasks and GLSLFX; see `howTo04`–`howTo06` |
| Memory paging | `source/pageableBuffer/README.md` | Extend | Pageable data sources and buffer manager |
| Shaders | `include/hvt/resources/shaders/` | Extend | GLSLFX programs consumed by HVT tasks |

## How-to examples (`test/howTos/`)

One runnable example per feature or workflow — integration documentation that also executes as
a test. For exhaustive validation of a feature, see the matching files in `test/tests/`.

| File | Topic |
|------|-------|
| `howTo01_CreateHgiImplementation.cpp` | Create an Hgi instance |
| `howTo02_CreateOneFramePass.cpp` | Single frame pass with Storm |
| `howTo03_CreateTwoFramePasses.cpp` | Compositing two frame passes |
| `howTo04_CreateACustomRenderTask.cpp` | Custom `HdxTask` (blur example) |
| `howTo05_UseSSAORenderTask.cpp` | Screen-space ambient occlusion |
| `howTo06_UseFXAARenderTask.cpp` | Fast approximate anti-aliasing |
| `howTo07_UseIncludeExclude.cpp` | Include/exclude prims from a pass |
| `howTo08_UseBoundingBoxSceneIndex.cpp` | Bounding box scene index filter |
| `howTo09_UseWireFrameSceneIndex.cpp` | Wireframe display |
| `howTo10_CustomListOfTasks.cpp` | Default, external, and minimal task lists |
| `howTo11_UseSkyDomeTask.cpp` | Sky dome render task |
| `howTo19_UseWBOITRenderTask.cpp` | WBOIT transparency |
| `howTo20_UseOutlineTasks.cpp` | Selection outline pipeline |
