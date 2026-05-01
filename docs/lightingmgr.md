# LightingManager

## Overview

The `LightingManager` maintains built-in light Sprims (dome lights and camera lights) in the shared `HdRetainedSceneIndex` owned by the parent [FramePass](framepass.md). It translates `GlfSimpleLight` descriptions into Hydra light prims with schema-conforming data sources.

## How Light Data Is Stored

Each light is a prim in the retained scene index with a composite data source providing three schemas:

| Schema | Data | Purpose |
|---|---|---|
| `HdLightSchema` | intensity, exposure, color, shadow params, shadow collection, angle, texture | Core light attributes consumed by the render delegate |
| `HdXformSchema` | transform matrix | Light position/orientation |
| `materialNetworkMap` *(optional)* | `HdMaterialNetworkMap` with `PxrDomeLight` or `PxrDistantLight` nodes | Material network for path-traced renderers (e.g., HdPrman) |

The material network is only created when `isHighQualityRenderer` is true (i.e., the renderer is not Storm).

Light prims are added under the `FramePass` uid namespace (e.g., `/framePass_Main_1/light0`, `/framePass_Main_1/light1`).

## Built-in Light Types

The manager creates two kinds of lights from the `GlfSimpleLightVector`:

| Type | Hydra Prim Type | When |
|---|---|---|
| **Dome light** | `HdPrimTypeTokens->domeLight` | `GlfSimpleLight::IsDomeLight()` returns true |
| **Camera light** | `HdPrimTypeTokens->simpleLight` (Storm) or `HdPrimTypeTokens->distantLight` (others) | All non-dome lights |

Before creating any lights, `SupportBuiltInLightTypes` verifies that the render delegate supports both dome and camera light prim types.

## Light Reconciliation

`SetBuiltInLightingState` reconciles the internal light list (`_lightIds`) with the active lights from the `GlfSimpleLightingContext`:

1. **Add** -- If active lights outnumber tracked lights, new light paths are created and `ReplaceLightSprim` inserts the corresponding prims.
2. **Remove** -- If tracked lights outnumber active lights, excess prims are removed via `RemoveLightSprim`.
3. **Update** -- For lights that already exist, parameters are compared and the prim is replaced if anything changed. Shadow matrix computations are updated, and for non-Storm renderers the camera light transform is recomputed from the inverse view matrix.

## Shadow Support

When a light has shadows enabled, the manager creates `HdxShadowParams` with:

- A `ShadowMatrixComputation` that recomputes the shadow matrix from the world extent and light direction.
- Resolution and blur values from the `GlfSimpleLight`.

Shadow computations are cached per light path and updated on every reconciliation pass.

## Camera Light Transform

For non-Storm renderers, camera lights need their transform expressed in world space. The manager multiplies the inverse view matrix (from `HdxFreeCameraSceneDelegate`) with the `GlfSimpleLight` transform. This is only applied when the view matrix is not identity, preventing unnecessary prim replacements.

## Public API

```cpp
// Set the full lighting state (called per frame from FramePass::GetRenderTasks).
lightingManager->SetLighting(lights, material, ambient, cameraDelegate, worldExtent);

// Control shadows.
lightingManager->SetEnableShadows(true);

// Exclude specific light paths (e.g., lights from scene delegates).
lightingManager->SetExcludedLights(excludedPaths);

// Read-only accessors (via LightingSettingsProvider interface).
auto ctx = lightingManager->GetLightingContext();
bool shadows = lightingManager->GetShadowsEnabled();
auto excluded = lightingManager->GetExcludedLights();
```

## LightingSettingsProvider Interface

`LightingManager` implements `LightingSettingsProvider`, exposing read-only accessors that task commit functions consume:

| Accessor | Returns |
|---|---|
| `GetLightingContext()` | The `GlfSimpleLightingContext` with active lights, material, and ambient |
| `GetExcludedLights()` | Paths to exclude from lighting calculations |
| `GetShadowsEnabled()` | Whether shadow tasks should execute |

## Related Documentation

- [FramePass](framepass.md) -- Owns the retained scene index and calls `SetLighting` per frame.
- [TaskManager](taskmgr.md) -- Shadow and simple-light tasks that depend on light prims.
- [RenderBufferManager](renderbuffermgr.md) -- AOV buffers managed through the same scene index.

## Reference

- [HdLightSchema](https://openusd.org/release/api/class_hd_light_schema.html) -- The schema for light Sprims.
- [HdXformSchema](https://openusd.org/release/api/class_hd_xform_schema.html) -- The transform schema.
- [HdRetainedSceneIndex](https://openusd.org/release/api/class_hd_retained_scene_index.html) -- The mutable in-memory scene index.
