# VisualizeAOVComputeShader - Design Document

## Overview

The `VisualizeAOVComputeShader` class computes the minimum and maximum depth values from a depth texture using a **multi-pass fragment shader reduction** approach.

This is used by `VisualizeAovTask` to normalize depth values for visualization, so that the full depth range maps to visible grayscale values (0.0 to 1.0).

## Why Not Use a Compute Shader?

The original approach attempted to use a GPU compute shader with atomic operations (`atomicMin`/`atomicMax`) to find min/max values in parallel. However, this has **portability issues**:

- **Metal (macOS/iOS)**: Doesn't support GLSL-style storage buffers (`layout(std430)`)
- **Atomic operations**: Different syntax and capabilities across OpenGL, Metal, and Vulkan
- **OpenUSD's HGI**: Limited cross-platform support for compute shader atomics

The fragment shader approach uses only standard texture rendering, which is well-supported everywhere.

## Algorithm: Hierarchical Reduction

The algorithm progressively reduces the texture size until it's small enough to read back efficiently.

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
         │  CPU Readback (only 12 pixels!)
         ▼
      Final min/max values
```

### Why This Is Efficient

| Approach | Pixels Read Back | Bandwidth |
|----------|------------------|-----------|
| CPU Readback (original) | 2,073,600 | ~8 MB |
| Hierarchical Reduction | 12 | ~192 bytes |

The GPU does the heavy lifting, and we only read back a tiny texture.

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

## C++ Implementation

### Class: `VisualizeAOVComputeShader`

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

// Intermediate textures created during reduction
std::vector<HgiTextureHandle> _reductionTextures;
```

#### Main Function: `ComputeMinMaxDepth()`

```cpp
GfVec2f ComputeMinMaxDepth(depthTexture, sampler)
{
    // 1. Create resources (shaders, pipelines, buffers) if needed
    
    // 2. Calculate output size for first pass
    //    outputSize = inputSize / 8 (reduction factor)
    
    // 3. Create first output texture
    
    // 4. Execute first pass: depth texture → min/max texture
    
    // 5. Loop: while texture is larger than 4x4
    //       Create smaller texture
    //       Execute reduction pass
    
    // 6. Read back the small texture (CPU)
    
    // 7. Find final min/max from the few pixels
    
    return GfVec2f(minDepth, maxDepth);
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
└─────────────────────────────────────────────────────────────────┘
                                                          │
                                                          │ Readback
                                                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                         CPU                                      │
│                                                                  │
│  ┌──────────────┐                                               │
│  │   Min/Max    │      Final loop:                              │
│  │   Texture    │      for each pixel:                          │
│  │     4x3      │        globalMin = min(globalMin, pixel.R)    │
│  │    (RG)      │        globalMax = max(globalMax, pixel.G)    │
│  └──────────────┘                                               │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Uniforms

Both shaders receive the same uniform structure:

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

// Stop reducing when texture is this small or smaller
constexpr int kMinTextureSize = 4;
```

## Usage in VisualizeAovTask

```cpp
void VisualizeAovTask::_UpdateMinMaxDepth(HgiTextureHandle const& inputAovTexture)
{
    // Create compute shader helper on first use
    if (!_depthMinMaxCompute)
    {
        _depthMinMaxCompute = std::make_unique<VisualizeAOVComputeShader>(_GetHgi());
    }

    // Compute min/max using GPU reduction
    GfVec2f minMax = _depthMinMaxCompute->ComputeMinMaxDepth(inputAovTexture, _sampler);
    _minMaxDepth[0] = minMax[0];
    _minMaxDepth[1] = minMax[1];
}
```

## Performance Considerations

1. **GPU-bound**: Most work happens on the GPU in parallel
2. **Minimal readback**: Only ~16 pixels read back to CPU
3. **Texture reuse**: Intermediate textures are recreated each frame (could be optimized to reuse if dimensions match)
4. **Pipeline caching**: Shaders and pipelines are created once and reused

## Potential Improvements

1. **Texture pooling**: Reuse reduction textures across frames
2. **Async readback**: Don't block on readback, use results from previous frame
3. **Larger reduction factor**: Use 16x16 blocks instead of 8x8 for fewer passes
4. **Shared memory**: If compute shaders become portable, use shared memory reduction
