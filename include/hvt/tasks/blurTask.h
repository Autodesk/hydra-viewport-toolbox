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

#include <memory>

namespace HVT_NS
{

/// BlurTask parameters.
struct HVT_API BlurTaskParams
{
    /// The amount of Blur to apply
    float blurAmount = 0.5f;

    /// The name of the aov to blur
    PXR_NS::TfToken aovName = PXR_NS::HdAovTokens->color;
};

/// A task for performing a blur on a color buffer.
class HVT_API BlurTask : public PXR_NS::HdxTask
{
public:
    /// Constructor.
    /// \param delegate A task delegate to store input parameters in.
    /// \param uid The unique id for this task.
    BlurTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& uid);
    ~BlurTask() override;

    /// Prepare the tasks resources.
    /// \param ctx The task context holding the names of the aovs in use.
    /// \param renderIndex The render index holding the data.
    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;

    /// Execute the blur task
    /// \param ctx The task context holding the names of the aovs in use
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static const PXR_NS::TfToken& GetToken();

protected:
    /// Sync the render pass resources
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

    /// Returns the path where is the blur shader text.
    static const PXR_NS::TfToken& _BlurShaderPath();

private:
    BlurTask()                           = delete;
    BlurTask(const BlurTask&)            = delete;
    BlurTask& operator=(const BlurTask&) = delete;

    /// Utility function to create the GL program for blur
    /// \return True if successful, otherwise false
    bool _CreateShaderResources();

    /// Utility function to create buffer resources.
    /// \return True if successful, otherwise false
    bool _CreateBufferResources();

    /// Utility to create resource bindings
    /// \return True if successful, otherwise false
    bool _CreateResourceBindings(PXR_NS::HgiTextureHandle const& aovTexture);

    /// Utility to create a pipeline
    /// \return True if successful, otherwise false
    bool _CreatePipeline(PXR_NS::HgiTextureHandle const& aovTexture);

    /// Utility to create a texture sampler
    /// \return True if successful, otherwise false
    bool _CreateSampler();

    /// Blur to the currently bound framebuffer.
    void _ApplyBlur(PXR_NS::HgiTextureHandle const& aovTexture);

    /// Destroy shader program and the shader functions it holds.
    void _DestroyShaderProgram();

    /// Print shader compile errors.
    void _PrintCompileErrors();

    /// Swaps the color target and colorIntermediate target.
    /// This is used when a task wishes to read from the color and also write.
    /// to it. We use two color targets and ping-pong between them.
    /// \param ctx The task context holding the names of the aovs in use.
    void _ToggleRenderTarget(PXR_NS::HdTaskContext* ctx);

    PXR_NS::HgiAttachmentDesc _attachment0;
    PXR_NS::HgiBufferHandle _indexBuffer;
    PXR_NS::HgiBufferHandle _vertexBuffer;
    PXR_NS::HgiSamplerHandle _sampler;
    PXR_NS::HgiShaderProgramHandle _shaderProgram;
    PXR_NS::HgiResourceBindingsHandle _resourceBindings;
    PXR_NS::HgiGraphicsPipelineHandle _pipeline;

    BlurTaskParams _params;
};

/// VtValue requirements
/// @{
HVT_API std::ostream& operator<<(std::ostream& out, BlurTaskParams const& pv);
HVT_API bool operator==(BlurTaskParams const& lhs, BlurTaskParams const& rhs);
HVT_API bool operator!=(BlurTaskParams const& lhs, BlurTaskParams const& rhs);
/// @}

} // namespace HVT_NS
