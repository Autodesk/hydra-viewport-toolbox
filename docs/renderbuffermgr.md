# RenderBufferManager

## Overview

The `RenderBufferManager` maintains render buffer Bprims (GPU-backed AOV targets) in the shared `HdRetainedSceneIndex` owned by the parent [FramePass](framepass.md). It does **not** set task parameters directly; instead it exposes AOV settings that task commit functions consult through the `RenderBufferSettingsProvider` interface.

## How Buffer Data Is Stored

Each render buffer is a prim in the retained scene index conforming to `HdRenderBufferSchema`. The data source holds:

| Field | Purpose |
|---|---|
| `dimensions` | Width and height of the buffer |
| `format` | Pixel format (e.g., `HdFormatFloat16Vec4`) |
| `multiSampled` | Whether the buffer uses MSAA |

Buffers are created under the `FramePass` uid namespace (e.g., `/framePass_Main_1/aov_color`, `/framePass_Main_1/aov_depth`).

## Key Operations

### SetRenderOutputs

```cpp
bool changed = bufferManager->SetRenderOutputs(
    HdAovTokens->color,          // AOV to visualize
    { HdAovTokens->color, HdAovTokens->depth },  // outputs to create
    inputBindings,                // optional bindings from a previous pass
    viewport);
```

Removes old Bprims and adds new ones when the AOV configuration changes (e.g., switching renderers or adding a new AOV). Returns `true` when buffers were recreated, signaling the caller to re-dirty tasks that reference them.

This method updates the internal AOV parameter cache and the viewport AOV setting but does **not** touch any task parameters.

### SetBufferSizeAndMsaa

```cpp
bufferManager->SetBufferSizeAndMsaa(GfVec2i(1920, 1080), 4, true);
```

Updates the data source fields for all existing Bprims and calls `DirtyPrims` if anything changed. The render index re-allocates the GPU buffers during the next sync.

### SetRenderOutputClearColor

```cpp
bufferManager->SetRenderOutputClearColor(HdAovTokens->color, VtValue(GfVec4f(0, 0, 0, 1)));
```

Updates clear-value data in the AOV parameter cache, which tasks consult during their commit functions.

## AOV Settings Provider

`RenderBufferManager` implements `RenderBufferSettingsProvider`, exposing read-only accessors that task commit functions use:

| Accessor | Returns |
|---|---|
| `GetViewportAov()` | The AOV token being visualized |
| `GetRenderBufferSize()` | Current buffer dimensions |
| `GetAovParamCache()` | AOV bindings and clear values |
| `GetPresentationParams()` | Framebuffer / interop / window target |
| `IsAovSupported()` | Whether the render delegate supports Bprims |
| `IsProgressiveRenderingEnabled()` | Whether progressive rendering is active |

Tasks read these during `CommitTaskValues` (see [TaskManager -- Commit Functions](taskmgr.md#commit-functions)) to build their `HdRenderPassAovBinding` vectors.

## Presentation

The manager supports three presentation modes:

- **Framebuffer** (`SetPresentationOutput`) -- classic OpenGL framebuffer target.
- **Interop** (`SetInteropPresentation`) -- GPU interop handle for cross-API compositing.
- **Window** (`SetWindowPresentation`) -- direct window surface presentation with optional vsync.

These settings are stored in `PresentationParams` and read by the `HdxPresentTask` commit function.

## Sharing Buffers Between Passes

Render buffers from one pass can be forwarded to another via `FramePass::GetRenderBufferBindingsForNextPass`. The receiving pass supplies these bindings as the `inputs` argument to `SetRenderOutputs`, which reuses the existing `HdRenderBuffer` objects instead of allocating new ones. See [FramePass -- Sharing Render Buffers](framepass.md#sharing-render-buffers-between-passes).

## Related Documentation

- [FramePass](framepass.md) -- Owns the retained scene index and orchestrates the render loop.
- [TaskManager](taskmgr.md) -- Task commit functions that consume AOV settings.
- [LightingManager](lightingmgr.md) -- Light prims managed through the same scene index.

## Reference

- [HdRenderBufferSchema](https://openusd.org/release/api/class_hd_render_buffer_schema.html) -- The schema for render buffer Bprims.
- [HdRetainedSceneIndex](https://openusd.org/release/api/class_hd_retained_scene_index.html) -- The mutable in-memory scene index.
