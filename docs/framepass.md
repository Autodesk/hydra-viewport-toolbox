# FramePass

## Overview

A `FramePass` is the top-level unit of rendering in the HVT engine. It owns a render pipeline: a camera, a set of Hydra tasks, render buffers, and lights. Multiple frame passes can coexist (e.g., a main 3-D view and a UI overlay), each with its own task list and AOV configuration, while optionally sharing the same `HdRenderIndex`.

## Architecture

HVT supports two rendering backends, selected at `FramePass` construction time:

| Backend | Abbreviation | USD Requirement | How prims are managed |
|---|---|---|---|
| **Scene Index** | SI | USD >= 25.05 (`HdLegacyTaskSchema`) | Prims live in an `HdRetainedSceneIndex` inserted into the render index |
| **Scene Delegate** | SD | Any USD version | Prims are inserted/removed through an `HdSceneDelegate` (sync delegate) |

The backend is chosen by `UseLegacySceneDelegate()` / `SetUseLegacySceneDelegate()` (see `taskBackend.h`). The default is SI (i.e. `UseLegacySceneDelegate()` returns `false`) on USD >= 25.05; on older versions only SD is available. Once a `FramePass` is initialized, its backend is fixed for its lifetime.

### TaskBackend abstraction

A `TaskBackend` (abstract class) encapsulates the backend-specific storage, registration, and value access for tasks. `FramePass` creates the appropriate concrete backend via the `CreateTaskBackend` factory function and shares it with the managers:

```
FramePass
  |
  |-- TaskBackend  (shared, SI or SD)
  |     |
  |     |-- [SI] HdRetainedSceneIndex   (task, buffer, light, camera prims)
  |     |-- [SD] SyncDelegate           (HdSceneDelegate-based storage)
  |     |
  |     |-- Task prims          (owned by TaskManager)
  |     |-- Render buffer prims (owned by RenderBufferPrimBackend)
  |     +-- Light prims         (owned by LightingPrimBackend)
  |
  |-- TaskManager            --> see taskmgr.md
  |-- RenderBufferManager    --> see renderbuffermgr.md
  |-- LightingManager        --> see lightingmgr.md
  |
  |-- FramePassCamera            (SI: scene-index based, SD: HdxFreeCameraSceneDelegate)
  +-- SelectionHelper            (picking / selection state)
```

Each manager and the camera are also created through factory functions (`CreateFramePassCamera`, `CreateLightingPrimBackend`, `CreateRenderBufferPrimBackend`) that select the SI or SD implementation based on the `TaskBackend` type.

### SI backend

The SI backend (`TaskSIBackend`) creates an `HdRetainedSceneIndex` and inserts it into the render index's scene index tree. Tasks are stored as prims conforming to `HdLegacyTaskSchema`. Additions, removals, and dirty notifications automatically propagate to the render index, which creates, syncs, or destroys the corresponding Hydra prims.

### SD backend

The SD backend (`TaskSDBackend`) uses a `SyncDelegate` (an `HdSceneDelegate` subclass) to store task values and register tasks with the render index via `HdRenderIndex::InsertTask<T>()`. This path does not use the retained scene index or `HdLegacyTaskSchema` and works with any USD version.

## Initialization

`FramePass::Initialize(FramePassDescriptor const&)` builds the full pipeline:

```cpp
// 1. Capture the backend selection (SI or SD).
_useLegacySceneDelegate = UseLegacySceneDelegate();

// 2. Create the shared task backend (SI or SD) and register it with the render index.
_taskBackend = CreateTaskBackend(renderIndex, uid, _useLegacySceneDelegate);

// 3. Create the camera (SI or SD based).
_camera = CreateFramePassCamera(uid, renderIndex, _taskBackend, _useLegacySceneDelegate);

// 4. Create the managers, all sharing the same task backend.
_bufferManager   = std::make_unique<RenderBufferManager>(uid, renderIndex, _taskBackend, ...);
_taskManager     = std::make_unique<TaskManager>(uid, renderIndex, _taskBackend);
_lightingManager = std::make_unique<LightingManager>(uid, renderIndex, _taskBackend, ...);
```

All managers add prims under paths namespaced by the `FramePass` uid (e.g., `/framePass_Main_1/renderTask_default`), preventing conflicts when multiple frame passes share the same render index.

## Uninitialize

`FramePass::Uninitialize()` detaches the task backend from the render index **before** destroying the managers. For the SI backend, `Uninitialize` calls `RemoveSceneIndex` on the render index, which causes it to clean up all prims contributed by the retained scene index. For the SD backend, tasks are removed from the render index individually. This prevents stale entries from accumulating across repeated `Initialize`/`Uninitialize` cycles.

## Preset Task Lists

`CreatePresetTasks(PresetTaskLists)` populates the task manager with a ready-made pipeline:

| Preset | Description |
|---|---|
| `Default` | Mirrors the task list from `HdxTaskController`: shadow, render, selection, color correction, present, etc. |
| `Minimal` | A stripped-down pipeline with only the essential render and present tasks. |

Both presets delegate to helpers in `taskCreationHelpers.h`, which register tasks with the [TaskManager](taskmgr.md) and wire up commit functions that pull settings from the [RenderBufferManager](renderbuffermgr.md) and [LightingManager](lightingmgr.md).

## Per-Frame Execution Flow

The typical render loop calls `GetRenderTasks` followed by `Render`:

```
FramePass::GetRenderTasks(inputAOVs)
  |
  |-- RenderBufferManager::SetRenderOutputs(...)     // create/update Bprims
  |-- Camera setup (framing, view/proj matrices)
  |-- LightingManager::SetLighting(...)              // create/update light Sprims
  |-- SelectionHelper updates
  |-- TaskManager::CommitTaskValues(kExecutableBit)  // run commit fns, dirty the backend
  +-- TaskManager::GetTasks(kExecutableBit)          // return the ordered HdTask list

FramePass::Render(tasks)
  |
  +-- HdEngine::Execute(renderIndex, tasks)          // sync + execute
```

`Render` returns a convergence percentage (0-100). Progressive renderers may require multiple `Render` calls to converge.

## Input Parameters

`FramePassParams` groups all per-frame settings:

| Group | Fields | Purpose |
|---|---|---|
| **View** | `viewMatrix`, `projectionMatrix`, `framing`, `fov`, `isOrtho` | Camera setup |
| **Lights** | `lights`, `material`, `ambient` | Forwarded to `LightingManager::SetLighting` |
| **AOVs** | `visualizeAOV`, `renderOutputs` | Which AOVs to render and visualize |
| **Background** | `clearBackgroundColor`, `backgroundColor`, `clearBackgroundDepth` | Buffer clear values |
| **MSAA** | `enableMultisampling`, `msaaSampleCount` | Multisampling configuration |
| **Rendering** | `renderParams` (from `BasicLayerParams`) | Lighting, culling, depth, clipping, etc. |

## Unique Identifiers

Each `FramePass` has a `SdfPath` uid built from its name and a monotonically increasing counter (e.g., `/framePass_Main_1`). All child prims (tasks, render buffers, lights) are parented under this path. When multiple frame passes share a render index, the uid namespacing prevents path collisions.

## Selection and Picking

`FramePass` exposes helpers that delegate to `SelectionHelper`:

- `Pick(HdxPickTaskContextParams)` -- executes a rectangular pick using the current view.
- `Pick(pickTarget, resolveMode, filter)` -- executes a targeted pick and returns `HdSelectionSharedPtr`.
- `SetSelection(HdSelectionSharedPtr)` -- applies a selection for highlighting.

## Shadows

Shadow control goes through convenience methods:

```cpp
framePass->SetEnableShadows(true);
framePass->SetShadowParams(myHdxShadowTaskParams);
```

These forward to the [TaskManager](taskmgr.md) to update the shadow task parameters and to the [LightingManager](lightingmgr.md) to toggle shadow computation on light prims.

## Sharing Render Buffers Between Passes

`GetRenderBufferBindingsForNextPass(aovs, copyContents)` returns `RenderBufferBindings` that a subsequent frame pass can consume as inputs. This enables multi-pass pipelines where one pass produces AOVs consumed by the next, avoiding redundant buffer allocation and unnecessary present steps.

## Dirty Notification Path

All dynamic prim changes (task params, buffer descriptors, light attributes) flow through the task backend:

### SI backend (scene index)

```
Data change (e.g., new param value)
  --> retainedSceneIndex->DirtyPrims({path, locatorSet})
      --> render index observer receives PrimsDirtied
          --> MarkTaskDirty / MarkBprimDirty / MarkSprimDirty
              --> HdTask::Sync / HdRenderBuffer::Sync / HdLight::Sync during Execute
```

Always use `DirtyPrims()` on the retained scene index rather than calling `HdChangeTracker::MarkDirty` directly. The scene index notification path is the authoritative channel.

### SD backend (scene delegate)

```
Data change (e.g., new param value)
  --> SyncDelegate stores the new value
  --> HdChangeTracker::MarkTaskDirty(path, dirtyBits)
      --> HdTask::Sync reads the value from the SyncDelegate during Execute
```

The `TaskBackend` interface hides this difference: callers use `TaskBackend::SetValue()` / `TaskBackend::CreateTask()` / `TaskBackend::RemoveTask()` regardless of which backend is active.

## Related Documentation

- [TaskManager](taskmgr.md) -- Task lifecycle, commit functions, execution ordering.
- [RenderBufferManager](renderbuffermgr.md) -- AOV buffer creation, resizing, MSAA.
- [LightingManager](lightingmgr.md) -- Built-in light management, shadow computation.

## Reference

- [HdRetainedSceneIndex](https://openusd.org/release/api/class_hd_retained_scene_index.html) -- The mutable in-memory scene index (SI backend).
- [HdRenderIndex](https://openusd.org/release/api/class_hd_render_index.html) -- The central render index.
- [HdLegacyTaskSchema](https://openusd.org/release/api/class_hd_legacy_task_schema.html) -- The schema bridging scene index data with the `HdTask` interface (SI backend, USD >= 25.05).
