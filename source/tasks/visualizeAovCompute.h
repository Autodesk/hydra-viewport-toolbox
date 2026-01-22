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
//
#pragma once

#include <hvt/api.h>

// clang-format off
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/imaging/hgi/buffer.h>
#include <pxr/imaging/hgi/computeCmds.h>
#include <pxr/imaging/hgi/computePipeline.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/resourceBindings.h>
#include <pxr/imaging/hgi/sampler.h>
#include <pxr/imaging/hgi/shaderProgram.h>
#include <pxr/imaging/hgi/texture.h>

#if __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

/// Computes the min/max depth values from a depth texture using GPU compute shaders.
///
/// This class performs a multi-pass tiled reduction on the GPU to compute the minimum
/// and maximum depth values. The result is stored in a 1x1 RG32F texture where:
/// - R channel contains the minimum depth
/// - G channel contains the maximum depth
///
/// The result texture remains on the GPU and can be directly sampled by shaders,
/// avoiding any CPU readback.
///
class VisualizeAovCompute
{
public:
    /// Constructor.
    /// @param hgi The Hgi instance to use for GPU operations.
    explicit VisualizeAovCompute(PXR_NS::Hgi* hgi);

    /// Destructor. Releases all GPU resources.
    ~VisualizeAovCompute();

    // Non-copyable
    VisualizeAovCompute(const VisualizeAovCompute&) = delete;
    VisualizeAovCompute& operator=(const VisualizeAovCompute&) = delete;

    /// Computes the min/max depth values from the given depth texture.
    /// @param depthTexture The input depth texture (must be a float format).
    /// @return true if computation succeeded, false otherwise.
    bool Compute(PXR_NS::HgiTextureHandle const& depthTexture);

    /// Returns the result texture containing min (R) and max (G) depth values.
    /// The texture is 1x1 with format HgiFormatFloat32Vec2.
    /// @return The result texture handle, or an invalid handle if Compute() hasn't been called.
    PXR_NS::HgiTextureHandle GetResultTexture() const { return _resultTexture; }

    /// Returns whether the compute resources are valid and ready for use.
    bool IsValid() const;

private:
    // Create compute shader programs
    bool _CreateShaderPrograms();

    // Create or resize intermediate buffers based on input dimensions
    bool _CreateBuffers(int inputWidth, int inputHeight);

    // Create compute pipelines
    bool _CreatePipelines();

    // Create the 1x1 result texture
    bool _CreateResultTexture();

    // Create resource bindings for a compute pass
    bool _CreateResourceBindings(
        PXR_NS::HgiTextureHandle const& depthTexture,
        bool firstPass,
        bool lastPass);

    // Destroy all shader programs
    void _DestroyShaderPrograms();

    // Destroy all resources
    void _DestroyResources();

    // Execute a single reduction pass within the given compute command buffer
    void _ExecutePass(
        PXR_NS::HgiComputeCmds* computeCmds,
        int inputWidth, int inputHeight,
        int outputWidth, int outputHeight,
        bool firstPass, bool lastPass);

    PXR_NS::Hgi* _hgi;

    // Shader programs
    PXR_NS::HgiShaderProgramHandle _shaderProgramTexToBuffer;
    PXR_NS::HgiShaderProgramHandle _shaderProgramBufferToBuffer;
    PXR_NS::HgiShaderProgramHandle _shaderProgramBufferToTex;

    // Compute pipelines
    PXR_NS::HgiComputePipelineHandle _pipelineTexToBuffer;
    PXR_NS::HgiComputePipelineHandle _pipelineBufferToBuffer;
    PXR_NS::HgiComputePipelineHandle _pipelineBufferToTex;

    // Resource bindings (recreated each frame as textures may change)
    PXR_NS::HgiResourceBindingsHandle _resourceBindings;

    // Ping-pong buffers for intermediate reduction results
    PXR_NS::HgiBufferHandle _buffer[2];
    int _bufferSize = 0;

    // Result texture (1x1 RG32F containing min/max)
    PXR_NS::HgiTextureHandle _resultTexture;

    // Sampler for depth texture (required by Metal)
    PXR_NS::HgiSamplerHandle _sampler;

    // Tile size for reduction (each thread processes TILE_SIZE x TILE_SIZE elements)
    static constexpr int TILE_SIZE = 16;
};

} // namespace HVT_NS
