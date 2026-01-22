# VisualizeAovCompute - Design Document

## Overview

The `VisualizeAovCompute` class computes the minimum and maximum depth values from a depth texture using a **multi-pass fragment shader reduction** approach.

This is used by `VisualizeAovTask` to normalize depth values for visualization, so that the full depth range maps to visible grayscale values (0.0 to 1.0).

## Key Feature: GPU-Only Computation (No CPU Readback)

The min/max values stay entirely on the GPU. The visualization shader samples the result directly from a 1x1 texture, **eliminating the performance cost of GPU-CPU synchronization**.

```
┌─────────────────────────────────────────────────────────────────┐
│ GPU (all on GPU, no CPU involvement!)                          │
│                                                                │
│ [Depth Tex] → [240×135] → [30×17] → [4×3] → [1×1]             │
│                                               │                │
│                                               ▼                │
│ [Visualization Shader samples 1×1 texture directly]           │
└─────────────────────────────────────────────────────────────────┘
```

### Why This Matters for Performance

| Approach | CPU Involvement | Latency Impact |
|----------|-----------------|----------------|
| **Old**: CPU readback | `HgiSubmitWaitTypeWaitUntilCompleted` (blocking!) | 1-5ms stall |
| **New**: GPU-only | None | ~0 (single texture fetch) |

## Why Not Use a Compute Shader?

A compute shader with parallel reduction would be the natural choice for finding min/max values. However, this approach has **portability issues** when using OpenUSD's HGI abstraction layer:

**The core problem**: HGI doesn't fully abstract GPU synchronization across backends. Metal requires explicit barriers (MTLFence, MTLEvent) between compute passes that write to a buffer and subsequent passes that read from it. OpenGL handles this implicitly, but Metal does not and HGI doesn't insert the necessary barriers automatically.

**Practical result**: Multiple attempts to implement a compute shader solution on macOS/Metal produced black images, indicating the reduction passes were reading uninitialized or stale data due to missing synchronization.

**The solution**: Fragment shaders with render-to-texture use the standard graphics pipeline, where HGI correctly manages render pass dependencies. This "just works" on all backends (OpenGL, Metal, Vulkan) without manual synchronization.

### HdStComputation Limitations

Note that OpenUSD's `HdStComputation` and `HdStExtCompGpuComputation` classes are **Storm-specific** and won't work with other render delegates (Embree, RenderMan, Arnold, etc.). If cross-delegate compatibility is needed, the fragment shader approach or Scene Index plugins are safer choices.

## Algorithm: Hierarchical Reduction to 1x1

The algorithm progressively reduces the texture size until it reaches a 1x1 texture that can be sampled directly by the visualization shader.

### Visual Example

```
INPUT: 1920 x 1080 depth texture (2 million pixels)
         │
         │  Pass 1: First Pass Shader
         │  - Each output pixel samples an 8x8 block of input
         │  - Outputs: R = min depth, G = max depth
         ▼
      240 x 135 min/max texture (32,400 pixels)
         │
         │  Pass 2: Reduction Shader
         │  - Each output pixel samples an 8x8 block of input
         ▼
       30 x 17 min/max texture (510 pixels)
         │
         │  Pass 3: Reduction Shader
         ▼
        4 x 3 min/max texture (12 pixels)
         │
         │  Pass 4: Reduction Shader
         ▼
        1 x 1 min/max texture (1 pixel!)
         │
         │  DIRECT GPU SAMPLING (no CPU readback!)
         ▼
      Visualization shader reads min/max at (0,0)
```

### Why This Is Efficient

| Approach | Pixels Read Back | Bandwidth | CPU Stall |
|----------|------------------|-----------|-----------|
| CPU Readback (original) | 2,073,600 | ~8 MB | Yes (blocking) |
| CPU Readback (reduced) | 12 | ~192 bytes | Yes (blocking) |
| **GPU-Only (current)** | **0** | **0** | **No** |

The GPU does all the work, and the visualization shader samples the result directly.

## Shader Details

### File: `depthMinMax.glslfx`

Contains three shader stages:

#### 1. Vertex Shader (`DepthMinMax.Vertex`)
Standard fullscreen triangle vertex shader. Passes UV coordinates to fragment shader.

```glsl
void main(void)
{
    gl_Position = position;
    uvOut = uvIn;
}
```

#### 2. First Pass Fragment Shader (`DepthMinMax.Fragment`)
Converts the depth texture to a min/max texture.

**Input**: Single-channel depth texture (HgiFormatFloat32)  
**Output**: RGBA texture where R=min, G=max

```
For each output pixel:
  1. Calculate which block of input pixels it covers
  2. Loop through all pixels in that block
  3. Track minimum and maximum depth values
  4. Output: vec4(min, max, 0, 1)
```

#### 3. Reduction Fragment Shader (`DepthMinMax.ReductionFragment`)
Reduces a min/max texture to a smaller min/max texture.

**Input**: RGBA texture where R=min, G=max  
**Output**: Smaller RGBA texture where R=min, G=max

```
For each output pixel:
  1. Calculate which block of input pixels it covers
  2. Loop through all pixels in that block
  3. Take min of all R channels (local minimums)
  4. Take max of all G channels (local maximums)
  5. Output: vec4(min, max, 0, 1)
```

### File: `visualizeAov.glslfx`

The depth visualization fragment shader samples the 1x1 min/max texture directly:

```glsl
void main(void)
{
    vec2 fragCoord = uvOut * screenSize;
    float depth = HgiTexelFetch_depthIn(ivec2(fragCoord)).x;
    
    // Sample the 1x1 min/max texture at (0,0) to get the depth range
    vec2 minMax = HgiTexelFetch_minMaxIn(ivec2(0, 0)).xy;
    
    hd_FragColor = vec4(vec3(normalizeDepth(depth, minMax)), 1.0);
}
```

## C++ Implementation

### Class: `VisualizeAovCompute`

#### Key Members

```cpp
// Two shader programs: one for depth→minmax, one for minmax→minmax
HgiShaderProgramHandle _firstPassShaderProgram;
HgiShaderProgramHandle _reductionShaderProgram;

// Two graphics pipelines (one per shader program)
HgiGraphicsPipelineHandle _firstPassPipeline;
HgiGraphicsPipelineHandle _reductionPipeline;

// Fullscreen triangle geometry
HgiBufferHandle _vertexBuffer;
HgiBufferHandle _indexBuffer;

// Intermediate textures created during reduction (including final 1x1)
std::vector<HgiTextureHandle> _reductionTextures;
```

#### Main Function: `ComputeMinMaxDepth()`

```cpp
HgiTextureHandle ComputeMinMaxDepth(depthTexture, sampler)
{
    // 1. Create resources (shaders, pipelines, buffers) if needed
    
    // 2. Calculate output sizes for each reduction pass
    //    Continue reducing until we reach 1x1
    
    // 3. Execute first pass: depth texture → min/max texture
    
    // 4. Loop: while texture is larger than 1x1
    //       Execute reduction pass to smaller texture
    
    // 5. Return the final 1x1 texture handle
    //    (NO CPU readback - shader samples it directly!)
    
    return _reductionTextures.back(); // 1x1 texture
}
```

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         GPU                                      │
│                                                                  │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐   │
│  │    Depth     │      │   Min/Max    │      │   Min/Max    │   │
│  │   Texture    │─────▶│   Texture    │─────▶│   Texture    │──▶│...
│  │  1920x1080   │      │   240x135    │      │    30x17     │   │
│  │   (float)    │      │    (RG)      │      │    (RG)      │   │
│  └──────────────┘      └──────────────┘      └──────────────┘   │
│        │                      │                      │          │
│        │ First Pass           │ Reduction            │          │
│        │ Shader               │ Shader               │          │
│                                                                  │
│                     ┌──────────────┐                             │
│              ...───▶│   Min/Max    │                             │
│                     │   Texture    │                             │
│                     │     1x1      │◀────────────────────────┐  │
│                     │    (RG)      │                         │  │
│                     └──────────────┘                         │  │
│                            │                                 │  │
│                            │ Sampled at (0,0)               │  │
│                            ▼                                 │  │
│                     ┌──────────────┐                         │  │
│                     │ Visualization│     Bound as texture    │  │
│                     │   Shader     │─────────────────────────┘  │
│                     │              │                             │
│                     └──────────────┘                             │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘

      *** NO CPU INVOLVEMENT - Everything stays on GPU! ***
```

## Uniforms

Both reduction shaders receive the same uniform structure:

```cpp
struct Uniforms
{
    GfVec2f screenSize;       // Input texture dimensions
    GfVec2f outputScreenSize; // Output texture dimensions
};
```

These are needed to calculate how many input pixels each output pixel should sample.

## Configuration Constants

```cpp
// Each pass reduces by 8x in each dimension
constexpr int kReductionFactor = 8;

// Final texture size (1x1 for direct GPU sampling)
constexpr int kFinalTextureSize = 1;
```

## Usage in VisualizeAovTask

```cpp
void VisualizeAovTask::Execute(HdTaskContext* ctx)
{
    // ... setup code ...
    
    // For depth visualization, compute min/max on GPU
    HgiTextureHandle minMaxTexture;
    if (_vizKernel == VizKernelDepth)
    {
        // Returns a 1x1 texture - no CPU readback!
        minMaxTexture = _ComputeMinMaxDepthTexture(aovTexture);
    }
    
    // Bind both the depth texture and minMax texture
    _CreateResourceBindings(aovTexture, minMaxTexture);
    
    // The visualization shader samples minMaxTexture at (0,0)
    _ApplyVisualizationKernel(outputTexture, minMaxTexture);
}
```

## Performance Characteristics

1. **No CPU Readback**: The biggest win - eliminates blocking GPU-CPU synchronization
2. **GPU-bound**: All work happens on the GPU in parallel
3. **Single texture fetch**: Visualization shader samples 1x1 texture at (0,0) - negligible cost
4. **Texture reuse**: Intermediate textures are cached and reused across frames when dimensions don't change
5. **Pipeline caching**: Shaders and pipelines are created once and reused

## Texture Caching

The implementation caches reduction textures across frames:

```cpp
// Member variable tracks last input dimensions
GfVec3i _lastInputDimensions{0, 0, 0};

// In ComputeMinMaxDepth():
const bool dimensionsChanged = (textureDesc.dimensions != _lastInputDimensions);

if (dimensionsChanged)
{
    // Only recreate textures when dimensions change
    // (e.g., window resize)
    ...
}
// Otherwise, reuse existing textures
```

**Benefits:**
- First frame: Creates all textures (same as before)
- Subsequent frames with same size: Reuses all textures (no GPU allocations!)
- Resolution change: Recreates textures automatically

## Potential Further Improvements

1. **Larger reduction factor**: Use 16x16 blocks instead of 8x8 for fewer passes
2. **Shared memory**: If compute shaders become portable, use shared memory reduction
3. **RG texture format**: Use Float32Vec2 instead of Float32Vec4 to halve bandwidth
4. **Command batching**: Batch all reduction passes into a single command buffer
