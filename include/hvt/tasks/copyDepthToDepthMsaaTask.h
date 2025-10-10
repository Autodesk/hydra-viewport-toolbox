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
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hdx/tokens.h>

#include <pxr/imaging/hgi/attachmentDesc.h>
#include <pxr/imaging/hgi/buffer.h>
#include <pxr/imaging/hgi/graphicsPipeline.h>
#include <pxr/imaging/hgi/resourceBindings.h>
#include <pxr/imaging/hgi/shaderProgram.h>
#include <pxr/imaging/hgi/texture.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

/// CopyDepthToDepthMsaa task parameters.
struct HVT_API CopyDepthToDepthMsaaTaskParams
{
    /// The name of the depth aov to copy from (resolved depth).
    PXR_NS::TfToken sourceDepthAovName = PXR_NS::HdAovTokens->depth;
    
    /// The name of the MSAA depth aov to copy to.
    PXR_NS::TfToken targetDepthAovName = PXR_NS::TfToken("depthMSAA");
};

/// A task for copying resolved depth buffer content to MSAA depth buffer.
/// This is needed when depth modifications (like depth bias) are applied to
/// the resolved depth buffer but subsequent rendering needs to use the MSAA depth buffer.
class HVT_API CopyDepthToDepthMsaaTask : public PXR_NS::HdxTask
{
public:
    /// Constructor
    /// \param delegate A task delegate to store input parameters in.
    /// \param uid The unique id for this task.
    CopyDepthToDepthMsaaTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& uid);
    ~CopyDepthToDepthMsaaTask() override;

    /// Prepare the tasks resources.
    /// \param ctx The task context holding the names of the aovs in use.
    /// \param renderIndex The render index holding the data.
    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;

    /// Execute the depth copy task.
    /// \param ctx The task context holding the names of the aovs in use.
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static const PXR_NS::TfToken& GetToken();

protected:
    /// Sync the render pass resources
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

    /// Returns the path where is the depth copy shader text.
    static const PXR_NS::TfToken& _CopyDepthShaderPath();

private:
    CopyDepthToDepthMsaaTask()                                        = delete;
    CopyDepthToDepthMsaaTask(const CopyDepthToDepthMsaaTask&)         = delete;
    CopyDepthToDepthMsaaTask& operator=(const CopyDepthToDepthMsaaTask&) = delete;

    /// Utility function to create the GL program for depth copy
    /// \return True if successful, otherwise false.
    bool _CreateShaderResources();

    /// Utility function to create buffer resources.
    /// \return True if successful, otherwise false.
    bool _CreateBufferResources();

    /// Utility to create resource bindings
    /// \return True if successful, otherwise false.
    bool _CreateResourceBindings(PXR_NS::HgiTextureHandle const& sourceDepthTexture);

    /// Utility to create a pipeline
    /// \return True if successful, otherwise false.
    bool _CreatePipeline(PXR_NS::HgiTextureHandle const& targetDepthTexture);

    /// Utility to create a texture sampler
    /// \return True if successful, otherwise false.
    bool _CreateSampler();

    /// Copy depth values to the target depth buffer.
    void _ApplyCopyDepth(PXR_NS::HgiTextureHandle const& sourceDepthTexture,
                        PXR_NS::HgiTextureHandle const& targetDepthTexture);

    /// Destroy shader program and the shader functions it holds.
    void _DestroyShaderProgram();

    /// Print shader compile errors.
    void _PrintCompileErrors();

    PXR_NS::HgiAttachmentDesc _depthAttachment;
    PXR_NS::HgiBufferHandle _indexBuffer;
    PXR_NS::HgiBufferHandle _vertexBuffer;
    PXR_NS::HgiSamplerHandle _sampler;
    PXR_NS::HgiShaderProgramHandle _shaderProgram;
    PXR_NS::HgiResourceBindingsHandle _resourceBindings;
    PXR_NS::HgiGraphicsPipelineHandle _pipeline;

    CopyDepthToDepthMsaaTaskParams _params;
};

/// VtValue requirements
/// @{
HVT_API std::ostream& operator<<(std::ostream& out, const CopyDepthToDepthMsaaTaskParams& pv);
HVT_API bool operator==(const CopyDepthToDepthMsaaTaskParams& lhs, const CopyDepthToDepthMsaaTaskParams& rhs);
HVT_API bool operator!=(const CopyDepthToDepthMsaaTaskParams& lhs, const CopyDepthToDepthMsaaTaskParams& rhs);
/// @}

} // namespace HVT_NS