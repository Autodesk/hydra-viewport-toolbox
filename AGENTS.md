# Hydra Viewport Toolbox — Agent Guide

This file orients AI coding agents working in this repository. For user-facing build
instructions, see [README.md](README.md). For subsystem design, start with
[docs/README.md](docs/README.md).

## What this repo is

**Hydra Viewport Toolbox (HVT)** is a C++ library built on [OpenUSD](https://openusd.org) Hydra.
It serves two complementary roles:

1. **Simplify viewport integration** — higher-level building blocks on top of stock Hydra: `FramePass`,
   `TaskManager`, render-buffer and lighting managers, multi-pass compositing, selection wiring,
   and preset task pipelines so applications need less Hydra boilerplate.
2. **Extend OpenUSD** — capabilities and features that Hydra/USD do not provide (or do not provide
   in the form viewport applications need): WBOIT transparency, GPU selection outlines, viewport
   post-processing tasks (SSAO, FXAA, blur, …), scene-index display filters (wireframe, bounding
   box), geometry helpers (`include/hvt/geometry/`), pageable buffers, and related shaders/tasks.

Utilities can be used together or independently. When adding code, decide whether the change
**wraps existing Hydra behavior** (simplification) or **introduces new rendering capability**
(extension) — both are in scope, but they land in different places (engine/managers vs
`tasks/`, `sceneIndex/`, `geometry/`, etc.).

## Repository layout

| Path | Purpose |
|------|---------|
| `include/hvt/engine/` | Viewport integration — frame passes, task/buffer/light managers (**simplify**) |
| `include/hvt/tasks/` | Custom `HdxTask` implementations — post-processing and **OpenUSD extensions** (WBOIT, outlines, …) |
| `include/hvt/sceneIndex/` | Scene-index filters — wireframe, bounding box, display overrides (**extend**) |
| `include/hvt/geometry/` | Procedural mesh/polyline helpers for Hydra scene data (**extend**) |
| `include/hvt/pageableBuffer/` | Pageable GPU buffer management (**extend**) |
| `include/hvt/` | Public API headers (other areas: selection, material, data sources) |
| `source/` | Implementations (mirrors `include/hvt/` structure) |
| `include/hvt/resources/shaders/` | GLSLFX shader programs used by tasks |
| `docs/` | Architecture and feature design docs — **read before large changes** |
| `test/tests/` | Unit and image-comparison tests — **backbone for library validation** |
| `test/howTos/` | Usage demonstrations (`howTo01`–`howTo20`) — **one per feature; also run as tests** |
| `test/data/baselines/` | Golden images for rendered output tests |
| `cmake/` | Build helpers (including vcpkg setup) |
| `externals/vcpkg/` | vcpkg submodule — **do not edit** |

## Core architecture (read in this order)

1. [docs/framepass.md](docs/framepass.md) — top-level rendering unit (`FramePass`)
2. [docs/taskmgr.md](docs/taskmgr.md) — ordered Hydra task pipeline (`TaskManager`)
3. [docs/renderbuffermgr.md](docs/renderbuffermgr.md) — AOV / render buffer Bprims
4. [docs/lightingmgr.md](docs/lightingmgr.md) — built-in light Sprims

Each `FramePass` owns a shared `HdRetainedSceneIndex` and three managers (tasks, buffers,
lights). Tasks are `HdxTask` subclasses registered through `TaskManager::AddTask`.

## Build and test

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Or configure, build, and test in one step:

```bash
cmake --workflow --preset debug
```

- Uses CMake presets and vcpkg manifest mode — no manual vcpkg install required.
- Optional local OpenUSD: set `OPENUSD_INSTALL_PATH` before configuring.
- Windows: use the x64 Visual Studio dev environment (see README).
- First configure is slow (vcpkg bootstraps and builds OpenUSD + test deps). Subsequent
  incremental builds are fast — only re-run `cmake --preset` when presets or the manifest change.

### Fast iteration (single tests)

All tests compile into one `hvt_test` binary at `build/<preset>/bin/hvt_test`. Prefer running a
targeted subset over the full suite while iterating:

```bash
# Discover test names (suite.name)
./build/debug/bin/hvt_test --gtest_list_tests

# Run one test or a suite (glob supported)
./build/debug/bin/hvt_test --gtest_filter=howTo.createOneFramePass
./build/debug/bin/hvt_test --gtest_filter='OutlineTask.*'

# Same filtering through ctest by regex
ctest --preset debug -R howTo
ctest --preset debug -R OutlineTask --output-on-failure
```

Tests require a working GPU/display (SDL2 + OpenGL on Linux/Windows, Metal on macOS). In headless
or sandboxed environments rendering tests will fail to initialize — validate with a build-only
check (`cmake --build --preset debug`) in that case.

## Tests vs How-tos

Both live under `test/` and run through the same `hvt_test` binary / `ctest`, but they serve
different purposes:

| | `test/tests/` | `test/howTos/` |
|---|---------------|----------------|
| **Primary goal** | Validate the library (traditional unit testing) | Demonstrate how to use a task, helper, or workflow |
| **Coverage** | Many tests per feature (params, edge cases, regressions, image compares) | Typically **one How-to per feature** |
| **When to read** | Fix a failure, add regression coverage, understand expected behavior | Integrate a feature into an application |

How-tos are executable documentation: they prove the API works end-to-end, but they are not a
substitute for the broader unit test suite in `test/tests/`.

## Common tasks

| Goal | Where to start |
|------|----------------|
| Understand the rendering pipeline | `docs/framepass.md`, `include/hvt/engine/framePass.h` |
| Add or modify a render task (OpenUSD extension) | `test/howTos/howTo04_CreateACustomRenderTask.cpp`, `include/hvt/tasks/`, `source/tasks/` |
| Simplify task/buffer/light wiring | `docs/taskmgr.md`, `docs/renderbuffermgr.md`, `docs/lightingmgr.md`, `include/hvt/engine/` |
| Register tasks in a frame pass | `include/hvt/engine/taskCreationHelpers.h`, `test/howTos/howTo10_CustomListOfTasks.cpp` |
| Selection / outline highlighting | `docs/outline.md`, `test/howTos/howTo20_UseOutlineTasks.cpp` |
| Transparency (WBOIT) | `docs/wboit.md`, `test/howTos/howTo19_UseWBOITRenderTask.cpp` |
| Scene index filters (wireframe, bbox) | `include/hvt/sceneIndex/`, `test/howTos/howTo08_*`, `howTo09_*` |
| Memory paging | `source/pageableBuffer/README.md`, `test/tests/testPageableBuffer.cpp` |
| Fix a failing test | Start in `test/tests/`; check platform skips in feature docs |
| Learn how to use a feature | Matching How-to in `test/howTos/` and [test/README.md](test/README.md) |
| Change public API | Header in `include/hvt/`, implementation in `source/`, add/update unit tests in `test/tests/` and a How-to if it is a new user-facing feature |

## Conventions

- **Namespace:** `HVT_NS` (generated in `include/hvt/namespace.h` at configure time).
- **Export macro:** `HVT_API` on public free functions (`include/hvt/api.h`); not on classes.
- **Hydra tasks:** extend `pxr::HdxTask`; implement `_Sync`, `Prepare`, `Execute`; use a
  params struct with `operator==` for dirty tracking.
- **Task wiring:** tasks communicate through `HdTaskContext` texture tokens, not direct coupling.
- **Unit tests:** add coverage in `test/tests/` (params equality, construction, behavior, image
  baselines in `test/data/baselines/`). Some tests skip Apple/Metal where `primId` rendering is
  non-deterministic (see `docs/outline.md`).
- **How-tos:** add or update **one** How-to in `test/howTos/` when introducing a new
  user-facing feature — show integration, not exhaustive validation.
- **Contributions:** PRs target the latest `contrib/*` branch per [CONTRIBUTING.md](CONTRIBUTING.md).

## Do not

- Edit files under `externals/vcpkg/`.
- Invent build steps outside the CMake preset workflow unless explicitly asked.
- Add dependencies without updating the vcpkg manifest and CMake configuration.
