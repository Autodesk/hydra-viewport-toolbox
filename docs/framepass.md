# FramePass

## Overview

A `FramePass` is the top-level unit of rendering in the HVT engine. It owns a render pipeline: a camera, a set of Hydra tasks, render buffers, and lights. Multiple frame passes can coexist (e.g., a main 3-D view and a UI overlay), each with its own task list and AOV configuration, while optionally sharing the same `HdRenderIndex`.

## Architecture

Each `FramePass` creates a shared `HdRetainedSceneIndex` and delegates prim management to three specialised managers:

```
FramePass
  |
  |-- HdRetainedSceneIndex  (shared, mutable, in-memory)
  |     |
  |     |-- Task prims          (owned by TaskManager)
  |     |-- Render buffer prims (owned by RenderBufferManager)
  |     +-- Light prims         (owned by LightingManager)
  |
  |-- TaskManager            --> see taskmgr.md
  |-- RenderBufferManager    --> see renderbuffermgr.md
  |-- LightingManager        --> see lightingmgr.md
  |
  |-- HdxFreeCameraSceneDelegate  (camera prim)
  +-- SelectionHelper             (picking / selection state)
```

The retained scene index is inserted into the render index's scene index tree. Additions, removals, and dirty notifications automatically propagate to the render index, which creates, syncs, or destroys the corresponding Hydra prims. See the [Dirty Notification Path](#dirty-notification-path) section below.

## Initialization

`FramePass::Initialize(FramePassDescriptor const&)` builds the full pipeline:

```cpp
// 1. Create the retained scene index and register it with the render index.
_retainedSceneIndex = HdRetainedSceneIndex::New();
renderIndex->InsertSceneIndex(_retainedSceneIndex, SdfPath::AbsoluteRootPath());

// 2. Create the managers, all sharing the same retained scene index.
_bufferManager   = std::make_unique<RenderBufferManager>(uid, renderIndex, _retainedSceneIndex);
_taskManager     = std::make_unique<TaskManager>(uid, renderIndex, _retainedSceneIndex);
_lightingManager = std::make_unique<LightingManager>(uid, renderIndex, _retainedSceneIndex, ...);

// 3. Create the camera delegate.
_cameraDelegate  = std::make_unique<HdxFreeCameraSceneDelegate>(renderIndex, uid);
```

All managers add prims under paths namespaced by the `FramePass` uid (e.g., `/framePass_Main_1/renderTask_default`), preventing conflicts when multiple frame passes share the same render index.

## Uninitialize

`FramePass::Uninitialize()` detaches the retained scene index from the render index **before** destroying the managers. `RemoveSceneIndex` causes the render index to clean up all prims contributed by this scene index, preventing stale entries from accumulating across repeated `Initialize`/`Uninitialize` cycles.

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
  |-- TaskManager::CommitTaskValues(kExecutableBit)  // run commit fns, dirty the scene index
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

All dynamic prim changes (task params, buffer descriptors, light attributes) flow through the retained scene index:

```
Data change (e.g., new param value)
  --> retainedSceneIndex->DirtyPrims({path, locatorSet})
      --> render index observer receives PrimsDirtied
          --> MarkTaskDirty / MarkBprimDirty / MarkSprimDirty
              --> HdTask::Sync / HdRenderBuffer::Sync / HdLight::Sync during Execute
```

Always use `_retainedSceneIndex->DirtyPrims()` rather than calling `HdChangeTracker::MarkDirty` directly. The scene index notification path is the authoritative channel.

## Related Documentation

- [TaskManager](taskmgr.md) -- Task lifecycle, commit functions, execution ordering.
- [RenderBufferManager](renderbuffermgr.md) -- AOV buffer creation, resizing, MSAA.
- [LightingManager](lightingmgr.md) -- Built-in light management, shadow computation.

## Reference

- [HdRetainedSceneIndex](https://openusd.org/release/api/class_hd_retained_scene_index.html) -- The mutable in-memory scene index.
- [HdxFreeCameraSceneDelegate](https://openusd.org/release/api/class_hdx_free_camera_scene_delegate.html) -- The camera scene delegate.
- [HdRenderIndex](https://openusd.org/release/api/class_hd_render_index.html) -- The central render index.
