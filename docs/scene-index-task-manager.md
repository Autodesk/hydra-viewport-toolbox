# Scene Index Architecture for Tasks, Render Buffers, and Lights

## Overview

The HVT engine manages Hydra tasks, render buffers (Bprims), and lights (Sprims) through a shared `HdRetainedSceneIndex`. This document explains the design, how the components interact, how to use the public APIs, and the trade-offs of the approach.

## Architecture

### Retained Scene Index as the Single Data Authority

A single `HdRetainedSceneIndex` is created per `FramePass` and registered with the `HdRenderIndex`. Three managers share it, each responsible for a different prim type:

```
FramePass
  |
  |-- HdRetainedSceneIndex  (shared, mutable, in-memory)
  |     |
  |     |-- Task prims          (owned by TaskManager)
  |     |     /framePass/renderTask_default
  |     |     /framePass/shadowTask
  |     |     /framePass/presentTask
  |     |     ...
  |     |
  |     |-- Render buffer prims (owned by RenderBufferManager)
  |     |     /framePass/aov_color
  |     |     /framePass/aov_depth
  |     |     ...
  |     |
  |     +-- Light prims         (owned by LightingManager)
  |           /framePass/light0
  |           ...
  |
  |-- TaskManager            --> AddPrims / DirtyPrims / RemovePrims for task prims
  |-- RenderBufferManager    --> AddPrims / DirtyPrims / RemovePrims for Bprims
  +-- LightingManager        --> AddPrims / DirtyPrims / RemovePrims for Sprims
```

The retained scene index is inserted into the render index's scene index tree. Additions, removals, and dirty notifications on the retained scene index automatically propagate to the render index, which creates, syncs, or destroys the corresponding Hydra prims.

### Initialization

```cpp
// Inside FramePass::Initialize()
_retainedSceneIndex = HdRetainedSceneIndex::New();
renderIndex->InsertSceneIndex(_retainedSceneIndex, SdfPath::AbsoluteRootPath());

_bufferManager  = std::make_unique<RenderBufferManager>(uid, renderIndex, _retainedSceneIndex);
_taskManager    = std::make_unique<TaskManager>(uid, renderIndex, _retainedSceneIndex);
_lightingManager = std::make_unique<LightingManager>(uid, renderIndex, _retainedSceneIndex, ...);
```

All three managers receive the same `HdRetainedSceneIndexRefPtr`. Each manager adds prims under paths namespaced by the `FramePass` uid, avoiding conflicts when multiple frame passes share the same render index.

## TaskManager

The `TaskManager` maintains an ordered list of Hydra tasks and prepares them for execution. It stores task data as scene index prims conforming to `HdLegacyTaskSchema`.

### How Task Data Is Stored

Each task is a prim in the retained scene index with four fields:

| Field | Type | Purpose |
|---|---|---|
| `factory` | `HdLegacyTaskFactorySharedPtr` | Creates the `HdTask` instance for the render index |
| `parameters` | `VtValue` | Task-specific params (e.g., `HdxRenderTaskParams`) |
| `collection` | `HdRprimCollection` | Rprim filter for the render pass |
| `renderTags` | `TfTokenVector` | Which render tags this task processes |

A mutable `TaskDataSource` (an `HdContainerDataSource` implementation) holds these values and serves them to the render index on demand. When a value is updated, the data source is mutated in-place and `DirtyPrims` is called on the retained scene index to notify the render index.

### Adding a Task

```cpp
SdfPath taskId = taskManager->AddTask<HdxRenderTask>(
    TfToken("myRenderTask"),   // instance name
    HdxRenderTaskParams(),     // initial parameters
    myCommitFunction);         // called before every execution
```

`AddTask<T>` does two things:
1. Registers a `TaskEntry` in the manager's internal ordered list (tracking the path, commit function, enabled state, and flags).
2. Creates a task prim in the retained scene index using `HdMakeLegacyTaskFactory<T>()`. The render index discovers the prim and instantiates the `HdTask`.

Use `AddRenderTask<T>` as a convenience when adding tasks derived from `HdxRenderTask`; it automatically combines `kRenderTaskBit | kExecutableBit` flags.

### Controlling Task Order

Tasks execute in the order they appear in the internal list. By default tasks are appended to the end. Use the `atPos` and `order` parameters to insert relative to an existing task:

```cpp
// Insert a skydome task right before the first render task.
taskManager->AddTask<HdxSkydomeTask>(
    TfToken("skydomeTask"), HdxSkydomeTaskParams(), commitFn,
    existingRenderTaskPath,
    TaskManager::InsertionOrder::insertBefore);
```

### Commit Functions

A `CommitTaskFn` is the main way to feed per-frame data into task parameters. It runs for every enabled task during `CommitTaskValues()`, just before rendering. The function receives two callbacks:

```cpp
auto commitFn = [&](TaskManager::GetTaskValueFn const& getValue,
                     TaskManager::SetTaskValueFn const& setValue)
{
    // Read the current params.
    auto params = getValue(HdTokens->params).Get<HdxRenderTaskParams>();

    // Merge in application state.
    params.camera      = cameraId;
    params.aovBindings = aovBindings;

    // Write back. If the value is unchanged, no dirty notification is sent.
    setValue(HdTokens->params, VtValue(params));

    // Render tags and collection can be updated the same way.
    setValue(HdTokens->renderTags, VtValue(myRenderTags));
    setValue(HdTokens->collection, VtValue(myCollection));
};
```

`SetTaskValueFn` performs a value-equality check before mutating the data source. If the new value equals the old one, the call is a no-op, which avoids unnecessary dirty notifications and redundant syncs.

### Getting and Setting Values Directly

Outside of commit functions, you can read and write task values by path:

```cpp
VtValue v = taskManager->GetTaskValue(taskPath, HdTokens->params);
taskManager->SetTaskValue(taskPath, HdTokens->params, VtValue(newParams));
```

The same equality-check and scene-index-dirty logic applies.

### Enabling and Disabling Tasks

Tasks can be toggled on or off. Disabled tasks are skipped by `CommitTaskValues` and `GetTasks`, but remain registered so they can be re-enabled later:

```cpp
taskManager->EnableTask(TfToken("shadowTask"), shadowsEnabled);
taskManager->EnableTask(TfToken("colorCorrectionTask"), colorCorrectionEnabled);
```

### Task Flags

Every task carries a bitmask of `TaskFlags`:

| Flag | Value | Meaning |
|---|---|---|
| `kExecutableBit` | `0x01` | Included in `Execute()` and `CommitTaskValues()` |
| `kRenderTaskBit` | `0x02` | An `HdxRenderTask` derivative |
| `kPickingTaskBit` | `0x04` | Used for selection/picking, separate execution path |
| `kAllTaskBits` | `0xFFFFFFFF` | Matches any flag (useful for queries) |

`GetTasks()`, `GetTaskPaths()`, and `CommitTaskValues()` accept flags to filter which tasks to process.

### Execution Flow

The typical per-frame flow is:

```
FramePass::GetRenderTasks(inputAOVs)
  |
  |-- RenderBufferManager::SetRenderOutputs(...)     // create/update Bprims
  |-- (camera, lighting, selection setup)
  |-- TaskManager::CommitTaskValues(kExecutableBit)   // run commit fns, dirty the scene index
  +-- TaskManager::GetTasks(kExecutableBit)           // return the ordered HdTask list

FramePass::Render(tasks)
  |
  +-- HdEngine::Execute(renderIndex, tasks)           // sync + execute
```

`CommitTaskValues` returns the list of enabled tasks (as `HdTaskSharedPtrVector`), but `GetRenderTasks` calls `GetTasks` separately because it may need to perform additional work between committing and returning.

### Removing Tasks

```cpp
taskManager->RemoveTask(TfToken("myRenderTask"));
// or by path:
taskManager->RemoveTask(taskPath);
```

This removes the prim from the retained scene index (`RemovePrims`) and erases the entry from the internal list. The render index cleans up the `HdTask` object in response to the scene index notification.

### Replacing a Commit Function

If a task's update logic changes at runtime (e.g., a plugin override):

```cpp
taskManager->SetTaskCommitFn(TfToken("renderTask"), newCommitFunction);
```

## RenderBufferManager

Render buffer Bprims are stored using `HdRenderBufferSchema`-conforming data sources. The data source holds dimensions, pixel format, multi-sampling state, and MSAA sample count.

Key operations:
- **SetBufferSizeAndMsaa**: Updates the data source fields and calls `DirtyPrims` if anything changed. The render index re-allocates the GPU buffers during the next sync.
- **SetRenderOutputs**: Removes old Bprims and adds new ones when the AOV configuration changes (e.g., switching renderers or adding a new AOV). Returns `true` when buffers were recreated, signaling the caller to re-dirty tasks that reference them.
- **SetRenderOutputClearColor**: Updates clear-value data in the AOV parameter cache, which tasks consult during their commit functions.

## LightingManager

Light Sprims are stored using data sources for `HdLightSchema` (intensity, shadow params), `HdXformSchema` (transform), and optionally `HdMaterialSchema` (for path-traced renderers). Lights are added, updated, and removed through the same retained scene index.

## Dirty Notification Path

All changes flow through the retained scene index:

```
Data change (e.g., new param value)
  --> retainedSceneIndex->DirtyPrims({path, locatorSet})
      --> render index observer receives PrimsDirtied
          --> MarkTaskDirty / MarkBprimDirty in the change tracker
              --> HdTask::Sync / HdRenderBuffer::Sync during Engine::Execute
```

When forcing dirty state from outside the scene index (e.g., after recreating Bprims with the same paths), use `_retainedSceneIndex->DirtyPrims()` with the appropriate locator rather than calling `MarkTaskDirty` directly on the change tracker. The scene index notification path is the authoritative channel in this architecture.

## Advantages

1. **Single data authority**: All dynamic prim data lives in one retained scene index. There is no dual ownership between a delegate's data maps and the render index's internal state.

2. **Unified prim lifecycle**: `AddPrims`, `DirtyPrims`, and `RemovePrims` handle creation, update, and removal for tasks, Bprims, and Sprims uniformly. No need for separate `InsertTask` / `InsertBprim` / `InsertSprim` + `MarkDirty` combinations.

3. **Fine-grained dirty tracking**: `HdDataSourceLocator` enables per-field notifications (e.g., changing only a light's intensity does not dirty its transform). This is more granular than the coarse `HdDirtyBits` flags.

4. **Composability**: Scene indices can be chained and filtered. A debugging or visualization scene index can be inserted between the retained scene index and the render index without modifying any manager code.

5. **Alignment with OpenUSD direction**: Scene indices are the recommended pattern going forward. Scene delegates are legacy and may be deprecated in future USD releases.

6. **Type-safe data access**: `HdContainerDataSource` and `HdTypedSampledDataSource<T>` provide typed, schema-conforming access to prim data.

## Trade-offs and Considerations

1. **Data source allocation**: Each prim's data is wrapped in `HdContainerDataSource` / `HdRetainedTypedSampledDataSource` objects. This adds a thin allocation layer compared to flat `VtValue` storage. In practice this is negligible for the number of prims managed here (tens to hundreds, not millions).

2. **Schema coupling**: Task data sources must conform to `HdLegacyTaskSchema` for the render index to recognize them. Custom task types must be creatable via `HdMakeLegacyTaskFactory<T>()`.

3. **Value-equality requirement**: `SetTaskValue` relies on `VtValue::operator==` to skip redundant updates. All task parameter types must implement correct equality comparison. An incomplete `operator==` will cause stale data (the update is silently skipped).

4. **Dirty path consistency**: All dirty notifications must go through the retained scene index's `DirtyPrims` to be recognized by the render index. Calling `HdChangeTracker::MarkTaskDirty` directly bypasses the scene index and may not propagate correctly.

5. **Debugging**: The hierarchical `HdDataSource` tree is less directly inspectable than a flat key-value map. Use `HdRetainedSceneIndex::GetPrim(path)` to access the data source tree for a specific prim.

## Reference

- [HdRetainedSceneIndex](https://openusd.org/release/api/class_hd_retained_scene_index.html) -- The mutable in-memory scene index.
- [HdLegacyTaskSchema](https://openusd.org/release/api/class_hd_legacy_task_schema.html) -- The schema bridging scene index data with the `HdTask` interface.
- [HdRenderBufferSchema](https://openusd.org/release/api/class_hd_render_buffer_schema.html) -- The schema for render buffer Bprims.
- [HdLightSchema](https://openusd.org/release/api/class_hd_light_schema.html) -- The schema for light Sprims.
