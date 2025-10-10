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
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on


#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hgi/buffer.h>
#include <pxr/imaging/hgi/graphicsPipeline.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/resourceBindings.h>
#include <pxr/imaging/hgi/sampler.h>
#include <pxr/imaging/hgi/shaderProgram.h>
#include <pxr/imaging/hgi/texture.h>

#include <optional>

// clang-format off
#if __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace HVT_NS
{



struct HVT_API DepthBiasTaskParams
{
    bool depthBiasEnable { false };
    float depthBias { 0.0f };  // Generic depth bias directly applied to depth buffer
    float slopeFactor { 0.0f }; // Slope factor for depth bias
};

/// A task that implements a depth bias.
/// \note To use when there is a 'z-depth fighting' between two frame passes.
class HVT_API DepthBiasTask : public PXR_NS::HdxTask
{
public:
    DepthBiasTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& uid);
    ~DepthBiasTask() override;

    DepthBiasTask()                                = delete;
    DepthBiasTask(const DepthBiasTask&)            = delete;
    DepthBiasTask& operator=(const DepthBiasTask&) = delete;

    void Prepare(PXR_NS::HdTaskContext* /*ctx*/, PXR_NS::HdRenderIndex* /*renderIndex*/) override { /* no-op */ }
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static const PXR_NS::TfToken& GetToken();

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

    void _CreateIntermediate(PXR_NS::HgiTextureDesc const& desc, PXR_NS::HgiTextureHandle& texHandle);

private:
    DepthBiasTaskParams _params;    
    
    PXR_NS::HgiTextureHandle _depthIntermediate;

    /// Defines the uniforms for the depth bias shader.
    struct Uniforms
    {
        PXR_NS::GfVec2f screenSize;
        float slopeFactor { 0.0f };
        float bias { 0.0f };
    } _uniforms;

    // HGI resources
    PXR_NS::HgiBufferHandle _indexBuffer;
    PXR_NS::HgiBufferHandle _vertexBuffer;
    PXR_NS::HgiSamplerHandle _sampler;
    PXR_NS::HgiShaderProgramHandle _shaderProgram;
    PXR_NS::HgiResourceBindingsHandle _resourceBindings;
    PXR_NS::HgiGraphicsPipelineHandle _pipeline;
    PXR_NS::HgiAttachmentDesc _depthAttachment;

    // Private methods for HGI resource management
    bool _CreateShaderResources();
    bool _CreateBufferResources();
    bool _CreateResourceBindings(PXR_NS::HgiTextureHandle const& sourceDepthTexture);
    bool _CreatePipeline(PXR_NS::HgiTextureHandle const& targetDepthTexture);
    bool _CreateSampler();
    void _ApplyDepthBias(PXR_NS::HgiTextureHandle const& sourceDepthTexture,
                        PXR_NS::HgiTextureHandle const& targetDepthTexture);
    void _DestroyShaderProgram();
    void _PrintCompileErrors();
    static const PXR_NS::TfToken& _DepthBiasShaderPath();
};

/// VtValue requirements
/// @{
HVT_API std::ostream& operator<<(std::ostream& out, const DepthBiasTaskParams& param);
HVT_API bool operator==(const DepthBiasTaskParams& lhs, const DepthBiasTaskParams& rhs);
HVT_API bool operator!=(const DepthBiasTaskParams& lhs, const DepthBiasTaskParams& rhs);
/// @}

} // namespace HVT_NS