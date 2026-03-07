# Weighted Blended Order-Independent Transparency (WBOIT)

## Overview

WBOIT is an order-independent transparency technique based on the paper
[Weighted Blended Order-Independent Transparency](https://jcgt.org/published/0002/02/09/)
by McGuire and Bavoil (2013). It provides a single-pass, approximate transparency rendering
method that does not require per-pixel sorting or linked lists.

This document describes the HVT implementation, its theory, usage, unit tests, limitations,
and a comparison with the existing linked-list OIT approach.

---

## Theory

Traditional alpha blending requires rendering transparent objects in strict back-to-front order.
OIT techniques remove this requirement. WBOIT approximates the correct blended result using
weighted averages.

### Algorithm

The WBOIT algorithm works in two passes:

**Pass 1 -- Accumulation (WbOitRenderTask)**

For each transparent fragment with color `C_i`, alpha `a_i`, and a depth-dependent weight
`w_i`:

- Accumulate into a vec4 buffer: `(C_i * w_i, a_i)` using additive blending `(One, One)`.
- Accumulate into a float buffer: `a_i * w_i` using additive blending.
- Alpha channel uses `(Zero, OneMinusSrcAlpha)` to compute total transmittance.

The weight function is:

```
w(z, a) = a * max(0.01, 3000 * (1 - z)^3)
```

This is Equation 10 from the original paper. The depth term `(1 - z)^3` gives higher weight
to fragments closer to the camera, biasing the result towards a roughly correct ordering.

**Pass 2 -- Resolve (WbOitResolveTask)**

A fullscreen pass reads both buffers and computes the final color:

```
opacity = 1 - accumColor.a
finalColor = vec4(accumColor.rgb / clamp(accumWeight, 1e-4, 5e4), opacity)
```

The resolved color is then alpha-blended over the opaque scene using:
- Color: `SrcAlpha, OneMinusSrcAlpha, Add`
- Alpha: `One, OneMinusSrcAlpha, Add`

---

## Implementation

### Source Files

| File | Purpose |
|------|---------|
| `include/hvt/tasks/wboitRenderTask.h` | Public header for `WbOitRenderTask` |
| `source/tasks/wboitRenderTask.cpp` | Accumulation pass implementation |
| `include/hvt/tasks/wboitResolveTask.h` | Public header for `WbOitResolveTask` |
| `source/tasks/wboitResolveTask.cpp` | Fullscreen resolve pass implementation |
| `source/tasks/wboitTokens.h` | Shared private token definitions for buffer names |
| `include/hvt/resources/shaders/wboit.glslfx` | Render pass shader (accumulation) |
| `include/hvt/resources/shaders/wboitResolve.glslfx` | Resolve fragment shader |

### Class Hierarchy

- **WbOitRenderTask** extends `PXR_NS::HdxRenderTask`
  - Overrides `_Sync` to configure blend state and disable MSAA.
  - Overrides `Prepare` to create WBOIT-specific render buffers (Float16Vec4 for color
    accumulation, Float16 for weight accumulation) and set the `oitRequestFlag`.
  - Overrides `Execute` to skip rendering when no translucent draw items exist.
  - Uses `HdStRenderPassShader` with the `wboit.glslfx` to inject the
    `RenderOutput` function that computes weighted accumulation.

- **WbOitResolveTask** extends `PXR_NS::HdxTask`
  - Uses `HdxFullscreenShader` with `wboitResolve.glslfx`.
  - Only executes when the `oitRequestFlag` is present in the task context, ensuring no
    unnecessary GPU work when there are no translucent fragments.

### Integration with TaskCreationHelpers

The `TaskCreationOptions` struct controls which OIT variant is used:

```cpp
hvt::FramePassDescriptor passDesc;
passDesc.renderIndex                    = renderIndex;
passDesc.uid                            = SdfPath("/MyFramePass");
passDesc.taskCreationOptions.useWbOit   = true;  // Enable WBOIT

auto framePass = hvt::ViewportEngine::CreateFramePass(passDesc);
framePass->CreatePresetTasks(hvt::FramePass::PresetTaskLists::Default);
```

When `useWbOit` is `true`:
- The translucent render task uses `WbOitRenderTask` instead of `HdxOitRenderTask`.
- The resolve task uses `WbOitResolveTask` instead of `HdxOitResolveTask`.
- The volume render task falls back to a standard `HdxRenderTask` with a warning,
  because volume rendering is not supported with WBOIT.

When `useWbOit` is `false` (default), the existing linked-list OIT pipeline is used unchanged.

### Token Design

Shared buffer tokens (`hdxWboitBufferOne`, `hdxWboitBufferTwo`) are defined in
`source/tasks/wboitTokens.h` as `TF_DEFINE_PRIVATE_TOKENS`, following the pattern used in the
Autodesk USD fork. Both `WbOitRenderTask` and `WbOitResolveTask` include this header to
exchange texture handles through the task context.

### Shader Design

The render pass shader (`wboit.glslfx`) is self-contained: it inlines the
`HdxRenderPass.RenderWbOit` layout and GLSL function, plus imports standard USD shaders for
camera, clip planes, and selection via `$TOOLS/` prefixed paths.

The resolve shader (`wboitResolve.glslfx`) is a minimal fullscreen fragment shader that reads
the two accumulation buffers and computes the final weighted-average color.

---

## Unit Tests

Tests are located in `test/tests/testWboitTask.cpp`.

### Core Tests

- **constructionDefault**: Verifies the default `TaskCreationOptions` (no WBOIT) produces the
  linked-list OIT resolve task, not the WBOIT resolve task.
- **constructionWbOit**: Verifies `useWbOit=true` produces WBOIT tasks instead of OIT.
- **renderWithWbOit**: End-to-end rendering with WBOIT enabled, validated via image comparison.
- **resolveTaskParamsVtValue**: Validates VtValue requirements for `WbOitResolveTaskParams`.

### Edge-Case Tests

- **renderTranslucentCube**: Renders the dedicated `translucent_cube.usda` test asset with WBOIT.
- **resizeBuffers**: Resizes the viewport mid-session to validate WBOIT buffer reallocation.
- **noTranslucentDrawItems**: Renders an opaque-only scene with WBOIT enabled to verify the
  render task gracefully skips and the resolve task does not execute.

### How-To

`test/howTos/howTo19_UseWBOITRenderTask.cpp` demonstrates the complete workflow to enable WBOIT
in a frame pass, including renderer creation, `TaskCreationOptions` configuration, and rendering.

---

## Limitations

1. **Volume rendering**: Not supported with WBOIT. When `useWbOit` is enabled, the volume OIT
   task is replaced by a standard render task and a warning is emitted. Volume prims will render
   but without proper transparency compositing.

2. **MSAA (Multi-Sample Anti-Aliasing)**: WBOIT buffers are not multi-sampled. The render task
   explicitly disables MSAA (`SetMultiSampleEnabled(false)`) for the accumulation pass. This
   means edges of transparent geometry will not benefit from hardware anti-aliasing.

3. **Accuracy**: WBOIT is an approximation. The depth-based weight function can produce visible
   artifacts when:
   - Multiple transparent surfaces are very close in depth.
   - Transparency values vary widely across overlapping surfaces.
   - Colored transparent surfaces overlap with similar depth values.

4. **High alpha values**: The weight function may lose precision for fragments with very high
   alpha values combined with near-equal depth, leading to color bleeding.

5. **Shader path resolution**: The render pass shader relies on USD's `$TOOLS/` path resolution
   for standard imports (`hdSt/shaders/renderPass.glslfx`, `hdx/shaders/renderPass.glslfx`,
   `hdx/shaders/selection.glslfx`). This requires the USD resource paths to be properly
   configured at runtime.

---

## OIT vs. WBOIT Comparison

| Aspect | Linked-List OIT | WBOIT |
|--------|----------------|-------|
| **Technique** | Per-pixel linked lists with depth sorting | Weighted blending (single-pass approximation) |
| **Accuracy** | Exact (sorts all fragments) | Approximate (depth-weighted average) |
| **Memory** | O(n) per pixel (linked list nodes via SSBOs) | O(1) per pixel (two fixed-size buffers) |
| **GPU features** | Requires atomic counters, SSBOs | Standard blending and render targets |
| **Performance** | Varies with overdraw and list length | Constant per-pixel cost |
| **Volume support** | Yes (`HdxOitVolumeRenderTask`) | No |
| **MSAA** | Resolves before OIT pass | Not supported |
| **WebGPU/Vulkan** | May have driver compatibility issues | Simpler GPU requirements |
| **Artifacts** | None (exact ordering) | Possible with similar-depth overlapping surfaces |

### When to use WBOIT

- Scenes with moderate transparency where approximate results are acceptable.
- Platforms where linked-list OIT is unavailable or slow (e.g., WebGPU).
- When predictable, constant memory usage is preferred over per-scene variable allocation.

### When to use linked-list OIT

- Scenes requiring exact transparency ordering.
- Scenes with volumetric transparency.
- When visual accuracy is more important than performance predictability.

---

## Technical Risks

- **T9 -- Shader `$TOOLS/` path resolution**: The `wboit.glslfx` imports
  standard USD shaders via `$TOOLS/` prefixed paths. If the USD installation does not configure
  these resource paths correctly, the shader will fail to compile at runtime. This is the same
  mechanism used by all other Hydra render pass shaders and is expected to work in standard
  deployments.

- **Token compatibility**: The buffer tokens (`hdxWboitBufferOne`, `hdxWboitBufferTwo`) are
  defined locally to avoid a dependency on the Autodesk USD fork's `HdxTokens`. If the project
  later adopts the fork's token definitions, the local definitions should be replaced.
