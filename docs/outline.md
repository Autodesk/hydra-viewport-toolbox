# Outline Rendering Tasks

## Overview

The outline tasks provide a GPU-driven, order-independent **selection/highlight outline**
pipeline for HVT frame passes. They draw crisp, anti-aliased outlines around chosen
primitives (typically the selection, the hover target, and on-top objects such as
manipulators), colored per category, without modifying the shaded scene geometry itself.

The implementation is a three-stage, ID-buffer-based edge-detection pipeline built entirely
from Hydra (`HdxTask`) tasks and standard Storm render buffers, so it runs on any Hgi backend
Storm supports (OpenGL, Vulkan, Metal, WebGPU). Any application can compose the tasks into a
frame pass; the tasks themselves carry no application-specific logic and expose their behavior
purely through parameters and task-context texture names.

This document describes the theory, the design, how to use it, its unit tests, and its
limitations.

---

## Theory

An outline is fundamentally an **edge-detection problem**: a pixel belongs to an outline when
it sits on the boundary between two different objects, or between an object and empty space.

Rather than detect edges on the shaded color image (which is fragile — color discontinuities
appear inside a single object due to lighting and texture), the pipeline detects edges on a
**primitive-ID image**. Each primitive is rendered into an integer `primId` buffer plus a
depth buffer. An edge exists wherever a pixel's `primId` differs from a neighbor's `primId`.
Because `primId` is a stable integer identity, the edges found are exactly the silhouettes and
inter-object seams of the rendered set — independent of shading, lighting, or draw order.

### Three-layer priority model

Objects are rendered into **three independent category buffers**, each a `primId` + depth
pair, published into the task context under a string prefix:

| Prefix    | Meaning                              | Typical source                       |
|-----------|--------------------------------------|--------------------------------------|
| `Base`    | Selected / highlighted objects       | Current selection set                |
| `Overlay` | On-top objects drawn above the scene | Manipulators, gizmos, transient tools|
| `Default` | Background / unselected scene objects| Everything else (for internal seams) |

At each pixel the mask shader resolves an **effective primId** using a fixed priority order —
`Overlay > Base > Default`. This lets an overlay object (e.g. a manipulator that "burns
through" the scene and is always drawn on top) take visual precedence over a selected object
underneath it, which in turn takes precedence over background geometry.

### Edge classification

For each pixel the mask compute shader scans a `3x3` or `5x5` neighborhood and classifies the
center pixel:

- **Effective boundary** — a neighbor has a different effective `primId` (a real
  object-to-object or object-to-empty edge).
- **Internal boundary** — the effective `primId` is unchanged but the underlying `Default`
  `primId` differs. This surfaces internal structure (sub-part seams) *within* a single
  selected object without outlining every triangle.

When several categories meet at one pixel, the winning style is chosen deterministically:

1. Higher layer priority wins (`Overlay > Base > Default`).
2. Within the `Base` layer, the **nearer** primitive (smaller depth) wins, so the outline of a
   closer selected object is not overwritten by a farther one. Hover feedback is protected: if
   either competing prim is hovered, a styling-priority ranking is used instead of raw depth so
   hover highlight is never "eaten" by a nearer non-hovered prim.
3. On a coplanar tie (depths within `kCoplanarBaseDepthEpsilon`), a stable styling-priority /
   higher-`primId` tie-break keeps the result order-independent.

### Soft edges (coverage anti-aliasing)

The fraction of neighbors in the search window that are boundary pixels approximates how much
of the pixel is covered by the edge. The shader raises that coverage to a falloff exponent and
scales the outline alpha by it, producing a soft, anti-aliased line without MSAA:

```glsl
coverage  = boundaryNeighborCount / totalNeighbors;
edgeAlpha = pow(coverage, softnessFalloff);
color.a  *= mix(1.0, edgeAlpha, softnessStrength);
```

`softnessStrength` (0 = hard edge, 1 = full softness) and `softnessFalloff` (lower = thicker
lines) are style parameters.

---

## Pipeline

The full pipeline is five tasks (three `OutlinePrimIdsTask` instances feeding one
`OutlineMaskTask` feeding one `OutlineOverlayTask`):

```
OutlinePrimIdsTask "Base"    -----+
OutlinePrimIdsTask "Overlay" -----+---> OutlineMaskTask ---> OutlineOverlayTask
OutlinePrimIdsTask "Default" -----+          |                       |
   (primId + depth AOVs               (RGBA color mask         (composite mask
    per category, keyed by             "outlineMaskTexture")    onto color AOV)
    string prefix)
```

Data is passed between tasks through the shared **task context** (`HdTaskContext`) by string
token, never by direct coupling:

- Each `OutlinePrimIdsTask` publishes `outline<Prefix>PrimIdsTexture` and
  `outline<Prefix>DepthTexture`.
- `OutlineMaskTask` reads those six entries and publishes `outlineMaskTexture`.
- `OutlineOverlayTask` reads `outlineMaskTexture` and blends it into the color AOV.

---

## Implementation

### Source Files

| File | Purpose |
|------|---------|
| `include/hvt/tasks/outline/outlinePrimIdsTask.h` | Public header for `OutlinePrimIdsTask` + params |
| `source/tasks/outline/outlinePrimIdsTask.cpp` | Offscreen primId/depth render pass |
| `include/hvt/tasks/outline/outlineMaskTask.h` | Public header for `OutlineMaskTask`, `OutlineMaskStyleParams`, `VisualizationMode` |
| `source/tasks/outline/outlineMaskTask.cpp` | Compute-shader mask generation |
| `include/hvt/tasks/outline/outlineOverlayTask.h` | Public header for `OutlineOverlayTask` + `BlurMode` |
| `source/tasks/outline/outlineOverlayTask.cpp` | Fullscreen composite pass |
| `include/hvt/resources/shaders/outlineMask.glslfx` | Compute shader: edge detection + visualization modes |
| `include/hvt/resources/shaders/outlineOverlay.glslfx` | Fullscreen composite + optional Gaussian blur |
| `include/hvt/resources/shaders/renderPassPickingShader.glslfx` | Render-pass shader that writes `primId` (and sub-prim ids) into the AOV |

All types live in the `HVT_NS::Outline` namespace.

### OutlinePrimIdsTask

Extends `PXR_NS::HdxTask`. Renders one Rprim collection into a dedicated `primId` + depth AOV
buffer pair using the picking render-pass shader (`renderPassPickingShader.glslfx`), which
emits `HdGet_primID()` into an integer color attachment.

- **Params** (`OutlinePrimIdsTaskParams`): `bufferPrefix`, `size`, `collection`, `camera`,
  `cullStyle`, optional `framing` / `overrideWindowPolicy`, and optional caller-supplied
  `aovBindings`.
- Owns its own `HdStRenderBuffer`s and `HdRenderPassState`. Blending is disabled
  (`SetBlendEnabled(false)`) — integer IDs must not be blended.
- `Execute` publishes the resulting `primId` and depth texture handles into the task context
  keyed by prefix; when the task is disabled it erases those entries so a stale buffer is never
  consumed downstream.
- Three instances are created — one per category — differing only in `bufferPrefix` and
  `collection`.

### OutlineMaskTask

Extends `PXR_NS::HdxTask`. Runs a GPU **compute** shader (`outlineMask.glslfx`) over the six
input textures and writes a single RGBA `outlineMaskTexture`.

- Resolves scene paths to primitive IDs at `_Sync`: `activePath` (lead selection), `hoverPaths`
  and `overlayPaths` are each converted to `primId` lists via
  `HdRenderIndex::GetRprim(...)->GetPrimId()`, descending into subtrees
  (`GetRprimSubtree`) so a parent path highlights all its child Rprims.
- Those ID lists are uploaded to three dynamic SSBO-style buffers (overlay / hover / active),
  packed as `vec4` arrays (four IDs per `vec4`, rounded up) so the shader can iterate them.
- `OutlineMaskStyleParams` are pushed as a shader constant block: per-category colors
  (`selectedColor`, `selectionLeadColor`, `overlayColor`, the corresponding hover colors,
  `unselectedHoverColor`, `defaultColor`), counts, `isHoverSelected`, the `hasDistinctOverlay`
  / `hasDistinctDefault` flags, and the softness controls.
- **Texture-reuse optimization:** when the caller has no distinct overlay (or no distinct
  default) buffer, it points that prefix's texture names at the `Base` names and sets
  `hasDistinctOverlay = 0` (resp. `hasDistinctDefault = 0`). The shader then skips those
  lookups entirely rather than sampling redundant buffers.
- Compute dispatch uses an `8x8` local work-group (`LOCAL_SIZE`), rounded up to cover the
  buffer. Pipeline, resource bindings, and program are cached and only rebuilt when their hash
  changes.
- `SetVisualizationMode` / `VisualizationMode` select debug outputs — `VISUALIZE_MASK_3x3`,
  `VISUALIZE_MASK_5x5`, `VISUALIZE_PRIM_IDS` (golden-ratio hashed colors per ID), and
  `VISUALIZE_DEPTH` (relative depth grayscale) — useful for diagnosing buffer contents.

### OutlineOverlayTask

Extends `PXR_NS::HdxTask` and uses `HdxFullscreenShader`. Composites `outlineMaskTexture` onto
the scene color AOV with straight src-alpha-over blending.

- **Params** (`OutlineOverlayTaskParams`): `enabled`, `size`, `screenScale` (UV scale around
  the bottom-right pivot, `1.0` = full screen), `blurMode`, `blurIntensity`.
- `BlurMode` — `None`, `Blur3x3`, `Blur5x5` — selects a pre-weighted Gaussian kernel applied to
  the mask before compositing. Blur uses premultiplied-alpha accumulation so the softened edge
  does not darken toward black. `blurIntensity` linearly mixes between the unblurred and blurred
  result.
- Falls back to a transparent default texture when no mask texture is present, so the task is a
  no-op when the mask stage produced nothing.

### Integration with a frame pass

The tasks are **opt-in** and added manually to a frame pass's `TaskManager`; none are created
by the default preset. Each task is registered with a *commit* callback that runs every frame
to keep size (and, for the primId tasks, camera/framing) in sync with the render buffer:

```cpp
auto& taskManager = framePass->GetTaskManager();

// One OutlinePrimIdsTask per category (Base / Overlay / Default).
hvt::Outline::OutlinePrimIdsTaskParams base;
base.enabled      = true;
base.bufferPrefix = "Base";
base.collection   = HdRprimCollection(HdTokens->geometry,
    HdReprSelector(HdReprTokens->smoothHull), SdfPath("/Root/Selected"));
taskManager->AddTask<hvt::Outline::OutlinePrimIdsTask>(baseToken, base, primIdsCommit);
// ... Overlay and Default instances similarly ...

// Mask: name the six task-context textures and set category colors.
hvt::Outline::OutlineMaskTaskParams mask;
mask.enabled               = true;
mask.basePrimIdsTexture    = "outlineBasePrimIdsTexture";
mask.baseDepthTexture      = "outlineBaseDepthTexture";
mask.defaultPrimIdsTexture = "outlineDefaultPrimIdsTexture";
mask.defaultDepthTexture   = "outlineDefaultDepthTexture";
mask.overlayPrimIdsTexture = mask.basePrimIdsTexture;   // no distinct overlay -> reuse Base
mask.overlayDepthTexture   = mask.baseDepthTexture;
mask.activePath            = SdfPath("/Root/Selected/Cylinder");
mask.style.selectedColor      = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
mask.style.selectionLeadColor = GfVec4f(0.18f, 0.95f, 0.64f, 0.7f);
taskManager->AddTask<hvt::Outline::OutlineMaskTask>(maskToken, mask, maskCommit);

// Overlay: composite with a 3x3 blur.
hvt::Outline::OutlineOverlayTaskParams overlay;
overlay.enabled  = true;
overlay.blurMode = hvt::Outline::BlurMode::Blur3x3;
taskManager->AddTask<hvt::Outline::OutlineOverlayTask>(overlayToken, overlay, overlayCommit);
```

The commit callbacks read the current buffer size and camera each frame and write them back
into the task params, so the tasks stay correct across viewport resizes and camera changes.

> **Populating categories from selection.** In a real application the `collection` root paths
> and the `activePath` / `hoverPaths` / `overlayPaths` are driven from selection and hover
> state. When there are no overlay prims, reuse the `Base` textures (as above) rather than
> rendering all geometry into the `Overlay` pass — the mask shader gives `Overlay` the highest
> priority, so feeding it unselected geometry would color everything as selected.

### Mapping an application's selection model onto the categories (non-normative)

A consuming application composes the five-task set into its viewport render pipeline and maps
its own selection semantics onto the three category buffers. A typical mapping is:

- The active selection set into `Base`.
- Manipulators, gizmos, and transient on-top geometry into `Overlay`.
- The remaining scene into `Default`, so internal seams within selected objects can be
  detected.
- The lead-selected object as `activePath`, and the hover target through `hoverPaths`.

Per-category style colors typically come from the application's theme. Nothing about this
mapping lives in the tasks: the categories are just three string-prefixed buffers plus three
path lists, so any application can map whatever selection/highlight semantics it has onto the
same prefixes.

---

## Unit Tests

Tests are in `test/tests/testOutlineTasks.cpp`:

- **Params equality / defaults** — `outline_maskStyleParamsEquality`,
  `outline_maskTaskParamsEquality`, `outline_primIdsTaskParamsEquality`,
  `outline_overlayTaskParamsEquality`, and the `*ParamsDefaultValues` cases validate the
  `operator==` / default-constructed state relied on for dirty-tracking.
- **Construction / teardown** — `outline_maskTaskConstruction`,
  `outline_primIdsTaskConstruction`, `outline_overlayTaskConstruction`, and
  `outline_allThreeTasksConstruction` verify each task can be added to and removed from a
  `TaskManager`.
- **Behavior** — `outline_maskTaskSetVisualizationMode` cycles all visualization modes;
  `outline_getTokens` checks the static token accessors.
- **Rendered image comparisons** — `outline_renderDisabled`, `outline_renderEnabled`,
  `outline_renderBlurModes`, and `outline_renderNonBasePrefix` render the full pipeline and
  compare against baselines in `test/data/baselines/`. These are disabled on Apple/Metal
  because `primId` rendering is non-deterministic there.

### How-To

`test/howTos/howTo20_UseOutlineTasks.cpp` is the end-to-end reference. It builds a scene with a
selected cube + cylinder (Base), an unselected sphere (Default), a lead-selection `activePath`,
and a blurred overlay composite, then validates the result against
`test/data/baselines/howTo/useOutlineTasks.png`.

---

## Limitations

1. **Metal / Apple non-determinism.** `primId` rendering is not deterministic on Metal, so the
   image-comparison tests and the how-to are disabled on Apple platforms. The pipeline still
   runs there; only golden-image validation is skipped.

2. **Approximate depth ordering within a layer.** Boundary style within the `Base` layer is
   resolved by comparing normalized depth. Under dynamic near/far fitting, depth is a `0..1`
   scene-relative value, so coplanar or near-coplanar selected surfaces fall back to a
   heuristic styling-priority tie-break (`kCoplanarBaseDepthEpsilon`) rather than true 3D
   ordering. Overlay depth is "burn-through" and is deliberately *not* comparable to scene
   depth.

3. **No MSAA on the ID buffers.** Edges come from integer `primId` buffers, which cannot be
   multi-sampled. Anti-aliasing is achieved in software via coverage-based soft edges and the
   optional Gaussian blur, not hardware MSAA.

4. **Fixed neighborhood width.** Outline thickness is bounded by the `3x3` / `5x5` search
   window (and the blur kernel). Very thick outlines require larger kernels, which are not
   provided; softness/thickness is tuned via `softnessFalloff`, `blurMode`, and `blurIntensity`.

5. **Cost scales with categories.** Each enabled category is a full offscreen render pass over
   its collection. The `hasDistinctOverlay` / `hasDistinctDefault` reuse path mitigates this by
   skipping redundant buffers, but three fully-distinct categories mean three extra scene draws
   per frame plus one compute and one fullscreen pass.

6. **ID-list buffer iteration.** Hover / active / overlay membership is tested by linear scan of
   the uploaded ID arrays inside the shader. This is fine for typical selection sizes but is
   `O(idsCount)` per boundary comparison; extremely large explicit ID lists would be costly.

7. **Task-context contract by string.** Inter-task wiring relies on matching texture token
   names (`outline<Prefix>PrimIdsTexture`, etc.). A prefix mismatch between the primId tasks and
   the mask task silently produces an empty outline rather than an error.

---

## Comparison with alternative outline techniques

| Aspect | ID-buffer edge detection (this pipeline) | Stencil/jump-flood outlines | Post-process depth+normal edges |
|--------|------------------------------------------|-----------------------------|---------------------------------|
| **Edge source** | Per-object integer `primId` | Stencil mask of selected set | Depth + normal discontinuities |
| **Per-object identity** | Exact (distinguishes adjacent objects) | Coarse (one mask for all selected) | None (edges are geometric only) |
| **Internal seams** | Yes, via `Default` layer | No | Partial (normal breaks) |
| **Category priority** | Built-in 3-layer model | Manual | None |
| **Order independence** | Yes (ID-based, deterministic tie-break) | Yes | Yes |
| **Anti-aliasing** | Coverage softness + optional blur | Distance-field based | Kernel based |
| **Cost** | One render pass per category + compute + composite | One mask pass + flood iterations | Single fullscreen pass |

### When to use this pipeline

- Selection / hover highlighting where **adjacent objects must be distinguished** and each
  category needs its own color.
- Products that already produce a `primId`/pick buffer, so the render cost is partly shared.
- Backends without reliable MSAA on integer targets (e.g. WebGPU), where software coverage
  softening is preferable.

### When a simpler approach suffices

- A single uniform highlight color for the whole selection with no internal seams — a stencil or
  jump-flood outline is cheaper.
- Purely geometric "toon" edges independent of selection — a depth+normal post-process avoids
  the per-category render passes entirely.
