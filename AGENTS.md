# Hydra Viewport Toolbox — Agent Guide

This file orients AI coding agents working in this repository. For user-facing build
instructions, see [README.md](README.md). For subsystem design, start with
[docs/README.md](docs/README.md). For how agent guidance is structured (shared skills, Cursor vs
Claude entry points), see [docs/ai-agents.md](docs/ai-agents.md).

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
| `test/howTos/` | Usage demonstrations (`howTo01`–`howTo11`, `howTo19`–`howTo21`; gaps 12–18 unused) — **one per feature; also run as tests** |
| `test/data/baselines/` | Golden images for rendered output tests |
| `cmake/` | Build helpers (including vcpkg setup) |
| `externals/vcpkg/` | vcpkg submodule — **do not edit** |

## Core architecture (read in this order)

1. [docs/framepass.md](docs/framepass.md) — top-level rendering unit (`FramePass`)
2. [docs/taskmgr.md](docs/taskmgr.md) — ordered Hydra task pipeline (`TaskManager`)
3. [docs/renderbuffermgr.md](docs/renderbuffermgr.md) — AOV / render buffer Bprims
4. [docs/lightingmgr.md](docs/lightingmgr.md) — built-in light Sprims

Each `FramePass` owns a render pipeline: a `TaskBackend` (Scene Index or Scene Delegate),
three managers (tasks, buffers, lights), and backend-specific prim storage. Tasks are
`HdxTask` subclasses registered through `TaskManager::AddTask`.

See [docs/framepass.md](docs/framepass.md) (TaskBackend abstraction) for the SI/SD backend
comparison, the pipeline diagram, and backend selection via `UseLegacySceneDelegate()` /
`SetUseLegacySceneDelegate()` in `taskBackend.h`.

## Build and test

**Precedence:** if a skill whose name starts with `build` is available in this session, follow it
and ignore this section and [docs/build.md](docs/build.md) — it describes a deliberately different
toolchain. There is no such skill in this repo, so it can only come from a developer's own
`~/.claude/skills/`. With no `build*` skill available, use the presets below.

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

- **Build details** — build layout, presets (`debug`/`release`/`asan`/…), vcpkg & OpenUSD, the
  fix-compile-errors agent workflow, and build troubleshooting: see [docs/build.md](docs/build.md).
- **Test details** — running and filtering single tests, baselines, and platform skips: see
  [docs/testing.md](docs/testing.md).
- Tests need a GPU/display; in headless environments use a build-only check
  (`cmake --build --preset debug`).

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
| Selection / outline highlighting | `docs/outline.md`, `test/howTos/howTo21_UseOutlineManager.cpp` (recommended wrapper), `test/howTos/howTo20_UseOutlineTasks.cpp` (raw tasks) |
| Transparency (WBOIT) | `docs/wboit.md`, `test/howTos/howTo19_UseWBOITRenderTask.cpp` |
| Flash picking | `include/hvt/tasks/flashPickTask.h`, `CreateFlashPickTask` in `include/hvt/engine/taskCreationHelpers.h`, `test/tests/testTaskHelpers.cpp` |
| Show/hide and scale USD prims | `include/hvt/engine/viewportEngine.h` — `UpdatePrim`, `CreateSelectBox`, `CreateAxisTripod` (no dedicated how-to yet) |
| Partial viewport display (framing clip) | `test/tests/testPartialDisplay.cpp` — `CameraUtilFraming` via `FramePassParams::viewInfo.framing` |
| Scene index filters (wireframe, bbox, display style) | `include/hvt/sceneIndex/`, `test/howTos/howTo08_*`, `howTo09_*` |
| Memory paging | `source/pageableBuffer/README.md`, `test/tests/testPageableBuffer.cpp` |
| Fix a failing test | Start in `test/tests/`; check platform skips in feature docs |
| Learn how to use a feature | Matching How-to in `test/howTos/` and [test/README.md](test/README.md) |
| Change public API | Header in `include/hvt/`, implementation in `source/`, add/update unit tests in `test/tests/` and a How-to if it is a new user-facing feature |

## Conventions

Baseline conventions below always apply. Deeper, on-demand guidance lives in `.claude/skills/`:
`openusd-coding-style` (C++/OpenUSD style), `create-hvt-task` (adding a task), and
`commit-content` (commit messages & PRs) — these load when the task matches.

- **License header:** every new `.cpp`/`.h` (and `.glslfx`) must begin with the Apache-2.0
  copyright header used throughout the tree — copy from an existing file (e.g.
  `source/tasks/aovInputTask.cpp`) and use the current calendar year in the copyright line.
- **Formatting:** match house style with the repo's `.clang-format` (and `.editorconfig`); run
  `clang-format` on changed files before committing.
- **Namespace:** `HVT_NS` (generated in `include/hvt/namespace.h` at configure time).
- **Export macro:** every **public API class and struct** in `include/hvt/` must be declared with
  `HVT_API` (from `include/hvt/api.h`), e.g. `class HVT_API BlurTask`, `struct HVT_API BlurTaskParams`.
  Also put `HVT_API` on params' free operators (`operator==`/`!=`/`<<`). **Exception:** scene-index
  filter subclasses of `HdSingleInputFilteringSceneIndexBase` put `HVT_API` on public/protected
  members instead of the class — see `include/hvt/sceneIndex/boundingBoxSceneIndex.h`.
- **Hydra tasks:** extend `pxr::HdxTask`; implement `_Sync`, `Prepare`, `Execute`; use a
  params struct with `operator==` for dirty tracking (see the `create-hvt-task` skill).
- **Task wiring:** tasks communicate through `HdTaskContext` texture tokens, not direct coupling.
- **Unit tests:** add coverage in `test/tests/` (params equality, construction, behavior, image
  baselines in `test/data/baselines/`); see [docs/testing.md](docs/testing.md). Some tests skip
  Apple/Metal where `primId` rendering is non-deterministic (see `docs/outline.md`).
- **How-tos:** add or update **one** How-to in `test/howTos/` when introducing a new
  user-facing feature — show integration, not exhaustive validation.
- **Contributions:** branch from and target **`main`** per [CONTRIBUTING.md](CONTRIBUTING.md);
  see the `commit-content` skill for commit/PR conventions.

## Do not

- Edit files under `externals/vcpkg/`.
- Hand-edit generated files such as `include/hvt/namespace.h` (created at configure time).
- Invent build steps outside the CMake preset workflow unless explicitly asked.
- Add dependencies without updating the vcpkg manifest and CMake configuration.
