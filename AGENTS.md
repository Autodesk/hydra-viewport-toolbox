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

```
FramePass
  ├── HdRetainedSceneIndex (shared)
  ├── TaskManager          → HdxTask pipeline (docs/taskmgr.md)
  ├── RenderBufferManager  → AOV Bprims (docs/renderbuffermgr.md)
  └── LightingManager      → light Sprims (docs/lightingmgr.md)
```

Tasks are stored as scene-index prims and executed in list order. Task commit functions run
before each frame to sync params from application state.

## Build and test

Use **CMake presets only** — do not invent custom configure flags unless explicitly asked
(see `CMakePresets.json`).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Or configure, build, and test in one step:

```bash
cmake --workflow --preset debug
```

### Build layout

| Path | Purpose |
|------|---------|
| `build/<preset>/` | Configure + build tree (e.g. `build/debug/bin/hvt_test`) |
| `install/<preset>/` | Install prefix set by presets |
| `build/<preset>/compile_commands.json` | Generated for IDE/clang tooling (`CMAKE_EXPORT_COMPILE_COMMANDS`) |

- **Windows:** use the x64 Visual Studio developer environment (see [README.md](README.md)).
- **Local preset overrides:** `CMakeUserPresets.json` at repo root is gitignored — use for personal
  presets, not committed changes.
- **Generated headers:** `include/hvt/namespace.h` is created at configure time — do not hand-edit.

### vcpkg & OpenUSD

- Manifest mode via `vcpkg.json`; setup and bootstrap logic live in `cmake/VcpkgSetup.cmake`
  (auto-initializes the `externals/vcpkg/` submodule when missing).
- **Never edit** ports or files inside `externals/vcpkg/`.
- **Default OpenUSD:** vcpkg `usd-minimal` feature when `OPENUSD_INSTALL_PATH` is unset.
- **Local OpenUSD:** `export OPENUSD_INSTALL_PATH=/path/to/usd/install` **before**
  `cmake --preset debug` (or any preset).
- **First configure is slow** — vcpkg bootstraps and builds OpenUSD + test deps from source.
  Subsequent incremental builds are fast; re-run `cmake --preset` only when presets, the manifest,
  or top-level CMake files change.
- **Advanced vcpkg options** (custom triplet, release-only deps, NuGet cache): see
  [docs/vcpkg.md](docs/vcpkg.md).

**Available presets:** `debug`, `release`, `relwithdebinfo`, `asan`, `ubsan` (Linux/macOS only),
`debugwithvulkan`, `releasewithvulkan`. Substitute the preset name in the commands above
(e.g. `cmake --preset asan`).

### Fix compile errors (agent workflow)

When asked to build and fix until success:

1. Configure once (`cmake --preset debug`) unless already configured or CMake/vcpkg inputs changed.
2. Build (`cmake --build --preset debug`); fix **source** errors reported by the compiler.
3. Re-build only — do not reconfigure after ordinary `.cpp`/`.h` fixes.
4. Match patterns in neighboring files (includes, `PXR_NAMESPACE_USING_DIRECTIVE`, task params).
5. Do not patch `externals/vcpkg/`, generated headers, or CI-only cache assumptions.

If **configure** fails, see Build troubleshooting below — most first-time failures are environment
or dependency issues, not application source bugs.

### Build troubleshooting

Most first-time failures are environment/dependency issues during **configure**, not code errors:

- **Ninja generator required.** The presets use `Ninja` (see `CMakePresets.json`). If configure
  fails immediately with a generator error, install `ninja` (and CMake ≥ 3.26).
- **System libraries for building OpenUSD + test deps from source.** These are not vendored; CI
  installs them (see `.github/workflows/ci-steps.yaml`):
  - Linux (apt): `libxmu-dev libxi-dev libgl-dev libxrandr-dev libxinerama-dev libxcursor-dev
    libltdl-dev autoconf autoconf-archive automake libtool mono-complete python3-venv
    libglu1-mesa-dev freeglut3-dev`
  - macOS (brew): `mono`
- **First configure builds OpenUSD from source** (no local prebuilt cache — the binary cache is
  CI-only), so it is slow and needs disk/RAM. To skip it entirely, point at a prebuilt install:
  `export OPENUSD_INSTALL_PATH=/path/to/usd` before `cmake --preset`.
- **When a dependency build fails, read the real error** in
  `externals/vcpkg/buildtrees/<package>/*.log` — the top-level CMake output only reports that the
  vcpkg step failed.
- **vcpkg submodule** is initialized automatically. If `VCPKG_ROOT` is set in your environment it
  must point to a valid clone, otherwise configure aborts.
- **Windows:** run from the x64 Visual Studio developer environment (see README).

### Fast iteration (single tests)

All tests compile into `hvt_test` at `build/<preset>/bin/hvt_test`. Prefer a targeted run over
the full suite: `./build/debug/bin/hvt_test --gtest_list_tests`, then
`--gtest_filter=Suite.testName` (or `ctest --preset debug -R <regex>`).

Tests need a GPU/display; in headless environments use a build-only check
(`cmake --build --preset debug`). See [`.cursor/rules/testing.mdc`](.cursor/rules/testing.mdc)
for gtest/ctest details, baselines, and platform skips.

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
| Build, test, or fix compile errors | **Build and test** section above (`cmake --preset debug`, agent workflow) |
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

- **License header:** every new `.cpp`/`.h` (and `.glslfx`) must begin with the Apache-2.0
  copyright header used throughout the tree — copy from an existing file (e.g.
  `source/tasks/aovInputTask.cpp`) and use the current calendar year in the copyright line.
- **Formatting:** match house style with the repo's `.clang-format` (and `.editorconfig`); run
  `clang-format` on changed files before committing.
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
- Hand-edit generated files such as `include/hvt/namespace.h` (created at configure time).
- Invent build steps outside the CMake preset workflow unless explicitly asked.
- Add dependencies without updating the vcpkg manifest and CMake configuration.
