// Copyright 2025 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <hvt/api.h>

// clang-format off
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/pxr.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hgi/attachmentDesc.h>
#include <pxr/imaging/hgi/buffer.h>
#include <pxr/imaging/hgi/graphicsPipeline.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/resourceBindings.h>
#include <pxr/imaging/hgi/sampler.h>
#include <pxr/imaging/hgi/shaderProgram.h>
#include <pxr/imaging/hgi/texture.h>

#if __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace HVT_NS
{

/// A helper class that calculates min/max depth values from a depth texture
/// using a multi-pass fragment shader reduction approach.
///
/// This approach is portable across all graphics APIs (OpenGL, Metal, Vulkan)
/// since it uses standard texture rendering rather than compute shaders.
///
/// The algorithm:
/// 1. First pass: Sample the depth texture in blocks, output min/max per block
/// 2. Subsequent passes: Reduce the min/max texture until we reach a 1x1 texture
/// 3. Return the 1x1 texture handle (no CPU readback required!)
///
/// The returned texture can be sampled directly by the visualization shader,
/// eliminating the need for blocking GPU-CPU synchronization.
///
/// Usage:
/// @code
///     VisualizeAovCompute depthMinMax(hgi);
///     HgiTextureHandle minMaxTex = depthMinMax.ComputeMinMaxDepth(depthTexture, sampler);
///     // Bind minMaxTex to your shader and sample at (0,0) to get (min, max) in RG channels
/// @endcode
///
class VisualizeAovCompute
{
public:
    /// Constructor.
    /// @param hgi The Hgi instance to use for GPU resource creation.
    explicit VisualizeAovCompute(PXR_NS::Hgi* hgi);

    /// Destructor. Releases all GPU resources.
    ~VisualizeAovCompute();

    // Non-copyable
    VisualizeAovCompute(const VisualizeAovCompute&) = delete;
    VisualizeAovCompute& operator=(const VisualizeAovCompute&) = delete;

    /// Computes the min and max depth values from the given depth texture.
    /// Returns a 1x1 texture where R=minDepth, G=maxDepth.
    /// 
    /// This texture stays on the GPU and can be sampled directly by shaders,
    /// avoiding the performance cost of GPU-CPU synchronization.
    ///
    /// @param depthTexture The depth texture to analyze (must be HgiFormatFloat32).
    /// @param sampler The sampler to use for texture access.
    /// @return A 1x1 texture handle containing (minDepth, maxDepth) in RG channels.
    ///         Returns an invalid handle if computation fails.
    PXR_NS::HgiTextureHandle ComputeMinMaxDepth(PXR_NS::HgiTextureHandle const& depthTexture,
        PXR_NS::HgiSamplerHandle const& sampler);

private:
    // Shader program creation
    bool _CreateFirstPassShaderProgram();
    bool _CreateReductionShaderProgram();

    // Buffer resources (fullscreen triangle)
    bool _CreateBufferResources();

    // Pipeline creation
    bool _CreateFirstPassPipeline();
    bool _CreateReductionPipeline();

    // Texture management
    PXR_NS::HgiTextureHandle _CreateReductionTexture(PXR_NS::GfVec3i const& dimensions);

    // Resource bindings
    void _CreatePartialPipeline(PXR_NS::HgiGraphicsPipelineDesc& desc);
    bool _CreateFirstPassResourceBindings(PXR_NS::HgiTextureHandle const& depthTexture,
        PXR_NS::HgiSamplerHandle const& sampler);
    bool _CreateReductionResourceBindings(PXR_NS::HgiTextureHandle const& inputTexture,
        PXR_NS::HgiSamplerHandle const& sampler);

    // Execute passes
    void _ExecuteFirstPass(PXR_NS::HgiTextureHandle const& depthTexture,
        PXR_NS::HgiTextureHandle const& outputTexture, PXR_NS::GfVec3i const& inputDims,
        PXR_NS::GfVec3i const& outputDims);
    void _ExecuteReductionPass(PXR_NS::HgiTextureHandle const& inputTexture,
        PXR_NS::HgiTextureHandle const& outputTexture, PXR_NS::GfVec3i const& inputDims,
        PXR_NS::GfVec3i const& outputDims);

    // Cleanup
    void _DestroyResources();
    void _DestroyShaderProgram(PXR_NS::HgiShaderProgramHandle& program);

    PXR_NS::Hgi* _hgi;

    // Shader programs
    PXR_NS::HgiShaderProgramHandle _firstPassShaderProgram;
    PXR_NS::HgiShaderProgramHandle _reductionShaderProgram;

    // Pipelines
    PXR_NS::HgiGraphicsPipelineHandle _firstPassPipeline;
    PXR_NS::HgiGraphicsPipelineHandle _reductionPipeline;

    // Buffer resources (fullscreen triangle)
    PXR_NS::HgiBufferHandle _vertexBuffer;
    PXR_NS::HgiBufferHandle _indexBuffer;

    // Resource bindings (recreated each frame based on input texture)
    PXR_NS::HgiResourceBindingsHandle _firstPassResourceBindings;
    PXR_NS::HgiResourceBindingsHandle _reductionResourceBindings;

    // Sampler for the reduction passes
    PXR_NS::HgiSamplerHandle _reductionSampler;

    // Intermediate textures for reduction (reused across frames if dimensions match)
    std::vector<PXR_NS::HgiTextureHandle> _reductionTextures;

    // Cached input dimensions to detect when textures need to be recreated
    PXR_NS::GfVec3i _lastInputDimensions{0, 0, 0};

    // Attachment descriptor
    PXR_NS::HgiAttachmentDesc _attachmentDesc;
};

} // namespace HVT_NS
