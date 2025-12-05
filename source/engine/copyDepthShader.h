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

#include <pxr/pxr.h>

#include <pxr/imaging/hgi/attachmentDesc.h>
#include <pxr/imaging/hgi/buffer.h>
#include <pxr/imaging/hgi/graphicsPipeline.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/resourceBindings.h>
#include <pxr/imaging/hgi/sampler.h>
#include <pxr/imaging/hgi/shaderFunction.h>
#include <pxr/imaging/hgi/shaderProgram.h>
#include <pxr/imaging/hgi/texture.h>

namespace HVT_NS
{

/// Copy the depth AOV from the input to the output texture.
/// \note That's the strip version of HdxFullscreenShader which always needs the color AOV.
class CopyDepthShader
{
public:
    explicit CopyDepthShader(PXR_NS::Hgi* hgi);

    CopyDepthShader()                                  = delete;
    CopyDepthShader(const CopyDepthShader&)            = delete;
    CopyDepthShader& operator=(const CopyDepthShader&) = delete;

    ~CopyDepthShader();

    void Execute(PXR_NS::HgiTextureHandle const& inputTexture,
        PXR_NS::HgiTextureHandle const& outputTexture);

protected:
    bool _CreateShaderProgram(PXR_NS::HgiTextureDesc const& inputTextureDesc);
    bool _CreateBufferResources();
    bool _CreateResourceBindings(PXR_NS::HgiTextureHandle const& inputTexture);
    bool _CreatePipeline(PXR_NS::HgiTextureHandle const& outputTexture);
    bool _CreateSampler();
    void _Execute(PXR_NS::HgiTextureHandle const& inputTexture,
        PXR_NS::HgiTextureHandle const& outputTexture);
    void _Cleanup();

private:
    PXR_NS::Hgi* _hgi { nullptr };

    PXR_NS::HgiAttachmentDesc _depthAttachment;
    PXR_NS::HgiSamplerHandle _sampler;
    PXR_NS::HgiShaderProgramHandle _shaderProgram;
    PXR_NS::HgiResourceBindingsHandle _resourceBindings;
    PXR_NS::HgiGraphicsPipelineHandle _pipeline;
};

} // namespace HVT_NS
