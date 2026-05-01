# TaskManager

## Overview

The `TaskManager` maintains an ordered list of Hydra tasks and prepares them for execution. It stores task data as scene index prims conforming to `HdLegacyTaskSchema` in a shared `HdRetainedSceneIndex` owned by the parent [FramePass](framepass.md).

## How Task Data Is Stored

Each task is a prim in the retained scene index with four fields:

| Field | Type | Purpose |
|---|---|---|
| `factory` | `HdLegacyTaskFactorySharedPtr` | Creates the `HdTask` instance for the render index |
| `parameters` | `VtValue` | Task-specific params (e.g., `HdxRenderTaskParams`) |
| `collection` | `HdRprimCollection` | Rprim filter for the render pass |
| `renderTags` | `TfTokenVector` | Which render tags this task processes |

A mutable `TaskDataSource` (an `HdContainerDataSource` implementation) holds these values and serves them to the render index on demand. When a value is updated, the data source is mutated in-place and `DirtyPrims` is called on the retained scene index to notify the render index.

## Adding a Task

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

## Controlling Task Order

Tasks execute in the order they appear in the internal list. By default tasks are appended to the end. Use the `atPos` and `order` parameters to insert relative to an existing task:

```cpp
// Insert a skydome task right before the first render task.
taskManager->AddTask<HdxSkydomeTask>(
    TfToken("skydomeTask"), HdxSkydomeTaskParams(), commitFn,
    existingRenderTaskPath,
    TaskManager::InsertionOrder::insertBefore);
```

## Commit Functions

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

    // Write back. Returns true on success. If the value is unchanged, no dirty
    // notification is sent but the call still returns true.
    bool ok = setValue(HdTokens->params, VtValue(params));

    // Render tags and collection can be updated the same way.
    setValue(HdTokens->renderTags, VtValue(myRenderTags));
    setValue(HdTokens->collection, VtValue(myCollection));
};
```

`SetTaskValueFn` returns `bool`: `true` when the value was accepted (whether or not it changed), `false` on error (e.g., unsupported key). It performs a value-equality check before mutating the data source. If the new value equals the old one, the call is a no-op, which avoids unnecessary dirty notifications and redundant syncs.

## Getting and Setting Values Directly

Outside of commit functions, you can read and write task values by path:

```cpp
VtValue v = taskManager->GetTaskValue(taskPath, HdTokens->params);

taskManager->SetTaskValue(taskPath, HdTokens->params, VtValue(newParams));
```

`GetTaskValue` returns an empty `VtValue` when the uid is empty, the task does not exist, or the key is unsupported. All three cases also emit a `TF_CODING_ERROR`.

`SetTaskValue` returns `false` under the same three conditions, and `true` when the value was accepted (including the unchanged-value early-return).

## Enabling and Disabling Tasks

Tasks can be toggled on or off. Disabled tasks are skipped by `CommitTaskValues` and `GetTasks`, but remain registered so they can be re-enabled later:

```cpp
taskManager->EnableTask(TfToken("shadowTask"), shadowsEnabled);
taskManager->EnableTask(TfToken("colorCorrectionTask"), colorCorrectionEnabled);
```

## Task Flags

Every task carries a bitmask of `TaskFlags`:

| Flag | Value | Meaning |
|---|---|---|
| `kExecutableBit` | `0x01` | Included in `Execute()` and `CommitTaskValues()` |
| `kRenderTaskBit` | `0x02` | An `HdxRenderTask` derivative |
| `kPickingTaskBit` | `0x04` | Used for selection/picking, separate execution path |
| `kAllTaskBits` | `0xFFFFFFFF` | Matches any flag (useful for queries) |

`GetTasks()`, `GetTaskPaths()`, and `CommitTaskValues()` accept flags to filter which tasks to process.

## Execution Flow

See [FramePass -- Per-Frame Execution Flow](framepass.md#per-frame-execution-flow) for the full picture. Within the task manager the sequence is:

```
TaskManager::CommitTaskValues(kExecutableBit)
  |-- for each enabled task matching the flags:
  |     |-- call CommitTaskFn(getValue, setValue)
  |     +-- DirtyPrims if any value changed
  +-- return the ordered HdTaskSharedPtrVector

TaskManager::GetTasks(kExecutableBit)
  +-- return enabled tasks matching the flags (from the render index)
```

## Removing Tasks

```cpp
taskManager->RemoveTask(TfToken("myRenderTask"));
// or by path:
taskManager->RemoveTask(taskPath);
```

This removes the prim from the retained scene index (`RemovePrims`) and erases the entry from the internal list. The render index cleans up the `HdTask` object in response to the scene index notification.

## Replacing a Commit Function

If a task's update logic changes at runtime (e.g., a plugin override):

```cpp
taskManager->SetTaskCommitFn(TfToken("renderTask"), newCommitFunction);
```

## Trade-offs

1. **Schema coupling**: Task data sources must conform to `HdLegacyTaskSchema` for the render index to recognize them. Custom task types must be creatable via `HdMakeLegacyTaskFactory<T>()`.

2. **Value-equality requirement**: `SetTaskValue` relies on `VtValue::operator==` to skip redundant updates. All task parameter types must implement correct equality comparison. An incomplete `operator==` will cause stale data (the update is silently skipped).

3. **Dirty path consistency**: All dirty notifications must go through `DirtyPrims` on the retained scene index. Calling `HdChangeTracker::MarkTaskDirty` directly bypasses the scene index and may not propagate correctly.

## Related Documentation

- [FramePass](framepass.md) -- Owns the retained scene index and orchestrates the render loop.
- [RenderBufferManager](renderbuffermgr.md) -- AOV buffers consulted by task commit functions.
- [LightingManager](lightingmgr.md) -- Light prims that tasks may reference.

## Reference

- [HdRetainedSceneIndex](https://openusd.org/release/api/class_hd_retained_scene_index.html) -- The mutable in-memory scene index.
- [HdLegacyTaskSchema](https://openusd.org/release/api/class_hd_legacy_task_schema.html) -- The schema bridging scene index data with the `HdTask` interface.
