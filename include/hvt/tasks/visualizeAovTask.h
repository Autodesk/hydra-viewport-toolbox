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
// Based on HdxVisualizeAovTask from OpenUSD:
// Copyright 2021 Pixar
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
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

#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/attachmentDesc.h>
#include <pxr/imaging/hgi/buffer.h>
#include <pxr/imaging/hgi/graphicsPipeline.h>
#include <pxr/imaging/hgi/resourceBindings.h>
#include <pxr/imaging/hgi/shaderProgram.h>
#include <pxr/imaging/hgi/texture.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

#if __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

// Forward declaration
class VisualizeAOVComputeShader;

struct HVT_API VisualizeAovTaskParams
{
    PXR_NS::TfToken aovName;
};

/// A task for visualizing non-color AOVs such as depth, normals, primId.
///
/// Different kernels are used depending on the AOV:
///     Depth: Renormalized from the range [0.0, 1.0] to [min, max] depth
///            to provide better contrast.
///     Normals: Transform each component from [-1.0, 1.0] to [0.0, 1.0] so that
///              negative components don't appear black.
///     Ids: Integer ids are colorized by multiplying by a large prime and
///          shuffling resulting bits so that neighboring ids are easily
///          distinguishable.
///     Other Aovs: A fallback kernel that transfers the AOV contents into a
///                 float texture is used.
///
/// This task updates the 'color' entry of the task context with the colorized
/// texture contents.
///
class HVT_API VisualizeAovTask : public PXR_NS::HdxTask
{
public:
    VisualizeAovTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);
    ~VisualizeAovTask() override;

    VisualizeAovTask()                                 = delete;
    VisualizeAovTask(const VisualizeAovTask&)          = delete;
    VisualizeAovTask& operator=(const VisualizeAovTask&) = delete;

    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static const PXR_NS::TfToken& GetToken();

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    // Enumeration of visualization kernels
    enum VizKernel {
        VizKernelDepth = 0,
        VizKernelId,
        VizKernelNormal,
        VizKernelFallback,
        VizKernelNone
    };

    // Returns true if the enum member was updated, indicating that the kernel
    // to be used has changed.
    bool _UpdateVizKernel(PXR_NS::TfToken const& aovName);

    // Returns a token used in sampling the texture based on the kernel used.
    PXR_NS::TfToken const& _GetTextureIdentifierForShader() const;

    // Returns the fragment shader mixin based on the kernel used.
    PXR_NS::TfToken const& _GetFragmentMixin() const;

    // ------------- Hgi resource creation/deletion utilities ------------------
    bool _CreateShaderResources(PXR_NS::HgiTextureDesc const& inputAovTextureDesc);
    bool _CreateBufferResources();
    bool _CreateResourceBindings(PXR_NS::HgiTextureHandle const& inputAovTexture);
    bool _CreatePipeline(PXR_NS::HgiTextureDesc const& outputTextureDesc);
    bool _CreateSampler(PXR_NS::HgiTextureDesc const& inputAovTextureDesc);
    bool _CreateOutputTexture(PXR_NS::GfVec3i const& dimensions);
    void _DestroyShaderProgram();
    void _PrintCompileErrors();
    // -------------------------------------------------------------------------

    // Compute min/max depth values using a GPU compute shader.
    void _UpdateMinMaxDepth(PXR_NS::HgiTextureHandle const& inputAovTexture);

    // Execute the appropriate kernel and update the task context 'color' entry.
    void _ApplyVisualizationKernel(PXR_NS::HgiTextureHandle const& outputTexture);

    // Kernel dependent resources
    PXR_NS::HgiTextureHandle _outputTexture;
    PXR_NS::GfVec3i _outputTextureDimensions;
    PXR_NS::HgiAttachmentDesc _outputAttachmentDesc;
    PXR_NS::HgiShaderProgramHandle _shaderProgram;
    PXR_NS::HgiResourceBindingsHandle _resourceBindings;
    PXR_NS::HgiGraphicsPipelineHandle _pipeline;

    // Kernel independent resources
    PXR_NS::HgiBufferHandle _indexBuffer;
    PXR_NS::HgiBufferHandle _vertexBuffer;
    PXR_NS::HgiSamplerHandle _sampler;

    float _screenSize[2];
    float _minMaxDepth[2];
    VizKernel _vizKernel;

    // Compute shader for min/max depth calculation
    std::unique_ptr<VisualizeAOVComputeShader> _depthMinMaxCompute;
};

/// VtValue requirements
/// @{
HVT_API std::ostream& operator<<(std::ostream& out, VisualizeAovTaskParams const& pv);
HVT_API bool operator==(VisualizeAovTaskParams const& lhs, VisualizeAovTaskParams const& rhs);
HVT_API bool operator!=(VisualizeAovTaskParams const& lhs, VisualizeAovTaskParams const& rhs);
/// @}

} // namespace HVT_NS
