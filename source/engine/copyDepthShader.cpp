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

#include "copyDepthShader.h"

#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/imaging/hgi/graphicsCmds.h>
#include <pxr/imaging/hgi/graphicsPipeline.h>
#include <pxr/imaging/hgi/shaderFunction.h>
#include <pxr/imaging/hgi/shaderProgram.h>
#include <pxr/imaging/hgi/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{
static const std::string vsCode = R"(
void main(void) {
    uvOut = vec2((hd_VertexID << 1) & 2, hd_VertexID & 2);
    gl_Position = vec4(uvOut * 2.0f + -1.0f, 0.0f, 1.0f);
})";

static const std::string fsCode = R"(
void main(void) {
    vec2 fragCoord = uvOut * screenSize;
    float depth = HgiTexelFetch_depthIn(ivec2(fragCoord)).x;
    gl_FragDepth = depth;
})";

// Prepare uniform buffer for GPU computation.
struct Uniforms
{
    GfVec2f screenSize;
};

} // namespace

CopyDepthShader::CopyDepthShader(Hgi* hgi) : _hgi(hgi) {}
CopyDepthShader::~CopyDepthShader()
{
    _Cleanup();
}

bool CopyDepthShader::_CreateShaderProgram(HgiTextureDesc const& inputTextureDesc)
{
    if (_shaderProgram)
    {
        return true;
    }

    // Vertex Shader.

    HgiShaderFunctionDesc vertDesc;
    vertDesc.debugName   = "CopyDepthShader Vertex";
    vertDesc.shaderStage = HgiShaderStageVertex;

    HgiShaderFunctionAddStageInput(
        &vertDesc, "hd_VertexID", "uint", HgiShaderKeywordTokens->hdVertexID);
    HgiShaderFunctionAddStageOutput(&vertDesc, "gl_Position", "vec4", "position");

    HgiShaderFunctionAddStageOutput(&vertDesc, "uvOut", "vec2");

    vertDesc.shaderCode            = vsCode.c_str();
    HgiShaderFunctionHandle vertFn = _hgi->CreateShaderFunction(vertDesc);

    // Check for error.
    if (!vertFn->IsValid())
    {
        TF_CODING_ERROR("%s", vertFn->GetCompileErrors().c_str());
        _Cleanup();
        return false;
    }

    // Fragment Shader.

    HgiShaderFunctionDesc fragDesc;
    fragDesc.debugName   = "CopyDepthShader Fragment";
    fragDesc.shaderStage = HgiShaderStageFragment;

    HgiShaderFunctionAddStageInput(&fragDesc, "uvOut", "vec2");

    HgiShaderFunctionAddTexture(&fragDesc, "depthIn",
        /*bindIndex = */ 0,
        /*dimensions = */ 2, inputTextureDesc.format);

    HgiShaderFunctionAddStageOutput(&fragDesc, "gl_FragDepth", "float", "depth(any)");
    HgiShaderFunctionAddConstantParam(&fragDesc, "screenSize", "vec2");

    fragDesc.shaderCode            = fsCode.c_str();
    HgiShaderFunctionHandle fragFn = _hgi->CreateShaderFunction(fragDesc);

    // Check for error.
    if (!fragFn->IsValid())
    {
        TF_CODING_ERROR("%s", fragFn->GetCompileErrors().c_str());
        _Cleanup();
        return false;
    }

    // Shader program.

    HgiShaderProgramDesc programDesc;
    programDesc.debugName = "CopyDepthShader Program";
    programDesc.shaderFunctions.push_back(std::move(vertFn));
    programDesc.shaderFunctions.push_back(std::move(fragFn));
    _shaderProgram = _hgi->CreateShaderProgram(programDesc);

    // Check for error.
    if (!_shaderProgram->IsValid())
    {
        TF_CODING_ERROR("%s", _shaderProgram->GetCompileErrors().c_str());
        _Cleanup();
        return false;
    }

    return true;
}

bool CopyDepthShader::_CreateResourceBindings(HgiTextureHandle const& inputTexture)
{
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "CopyDepthShader Resources";

    HgiTextureBindDesc texBind;
    texBind.bindingIndex = 0;
    texBind.stageUsage   = HgiShaderStageFragment;
    texBind.textures.push_back(inputTexture);
    texBind.samplers.push_back(_sampler);

    resourceDesc.textures.push_back(std::move(texBind));

    // If nothing has changed in the descriptor we avoid re-creating the resource bindings object.
    if (_resourceBindings)
    {
        HgiResourceBindingsDesc const& desc = _resourceBindings->GetDescriptor();
        if (desc == resourceDesc)
        {
            return true;
        }
        else
        {
            _hgi->DestroyResourceBindings(&_resourceBindings);
        }
    }

    _resourceBindings = _hgi->CreateResourceBindings(resourceDesc);

    return true;
}

bool CopyDepthShader::_CreatePipeline(HgiTextureHandle const& outputTexture)
{
    if (_pipeline)
    {
        if (_depthAttachment.format == outputTexture->GetDescriptor().format)
        {
            return true;
        }

        _hgi->DestroyGraphicsPipeline(&_pipeline);
    }

    HgiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName     = "CopyDepthShader Pipeline";
    pipelineDesc.shaderProgram = _shaderProgram;

    // Set up depth attachment.
    _depthAttachment.format  = outputTexture->GetDescriptor().format;
    _depthAttachment.usage   = outputTexture->GetDescriptor().usage;
    _depthAttachment.loadOp  = HgiAttachmentLoadOpDontCare;
    _depthAttachment.storeOp = HgiAttachmentStoreOpStore;

    pipelineDesc.depthAttachmentDesc = _depthAttachment;

    // Alpha to coverage would prevent any pixels that have an alpha of 0.0 from
    // being written. We want to color correct all pixels. Even background
    // pixels that were set with a clearColor alpha of 0.0.
    pipelineDesc.multiSampleState.alphaToCoverageEnable = false;

    // The MSAA on renderPipelineState has to match the render target.
    pipelineDesc.multiSampleState.sampleCount       = outputTexture->GetDescriptor().sampleCount;
    pipelineDesc.multiSampleState.multiSampleEnable = pipelineDesc.multiSampleState.sampleCount > 1;

    pipelineDesc.depthState.depthTestEnabled  = true;
    pipelineDesc.depthState.depthWriteEnabled = true;
    pipelineDesc.depthState.depthCompareFn    = HgiCompareFunctionAlways;

    // Uniform.
    pipelineDesc.shaderConstantsDesc.stageUsage = HgiShaderStageFragment;
    pipelineDesc.shaderConstantsDesc.byteSize   = sizeof(Uniforms);

    _pipeline = _hgi->CreateGraphicsPipeline(pipelineDesc);

    return (bool)_pipeline;
}

bool CopyDepthShader::_CreateSampler()
{
    if (_sampler)
    {
        return true;
    }

    HgiSamplerDesc sampDesc;

    sampDesc.magFilter = HgiSamplerFilterLinear;
    sampDesc.minFilter = HgiSamplerFilterLinear;

    sampDesc.addressModeU = HgiSamplerAddressModeClampToEdge;
    sampDesc.addressModeV = HgiSamplerAddressModeClampToEdge;

    _sampler = _hgi->CreateSampler(sampDesc);

    return true;
}

void CopyDepthShader::_Execute(
    HgiTextureHandle const& inputTexture, HgiTextureHandle const& outputTexture)
{
    GfVec3i const& dimensions = inputTexture->GetDescriptor().dimensions;

    // Prepare graphics cmds.

    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.depthAttachmentDesc = _depthAttachment;
    gfxDesc.depthTexture        = outputTexture;

    const GfVec4i viewport(0, 0, dimensions[0], dimensions[1]);

    Uniforms uniform;
    uniform.screenSize[0] = static_cast<float>(dimensions[0]);
    uniform.screenSize[1] = static_cast<float>(dimensions[1]);

    // Begin rendering.

    HgiGraphicsCmdsUniquePtr gfxCmds = _hgi->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("CopyDepthShader");
    gfxCmds->BindResources(_resourceBindings);
    gfxCmds->BindPipeline(_pipeline);
    gfxCmds->SetConstantValues(_pipeline, HgiShaderStageFragment, 0, sizeof(uniform), &uniform);
    gfxCmds->SetViewport(viewport);
    gfxCmds->Draw(3, 0, 1, 0);
    gfxCmds->PopDebugGroup();

    // Done recording commands, submit work.
    _hgi->SubmitCmds(gfxCmds.get());
}

void CopyDepthShader::Execute(
    HgiTextureHandle const& inputTexture, HgiTextureHandle const& outputTexture)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (inputTexture == outputTexture)
    {
        return;
    }

    class Guard
    {
    public:
        Guard(HgiTextureHandle const& inputTexture) : _inputTexture(inputTexture)
        {
            _inputTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
        }
        ~Guard() { _inputTexture->SubmitLayoutChange(HgiTextureUsageBitsDepthTarget); }

    private:
        HgiTextureHandle const& _inputTexture;
    } guard(inputTexture);

    if (!TF_VERIFY(_CreateSampler(), "Sampler creation failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreateShaderProgram(inputTexture->GetDescriptor()), "Shader creation failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreateResourceBindings(inputTexture), "Resource binding failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreatePipeline(outputTexture), "Pipeline creation failed."))
    {
        return;
    }

    _Execute(inputTexture, outputTexture);
}

void CopyDepthShader::_Cleanup()
{
    if (_sampler)
    {
        _hgi->DestroySampler(&_sampler);
    }

    if (_shaderProgram)
    {
        auto shaderFunctions = _shaderProgram->GetShaderFunctions();

        for (auto& fn : shaderFunctions)
        {
            _hgi->DestroyShaderFunction(&fn);
        }
        _hgi->DestroyShaderProgram(&_shaderProgram);
    }

    if (_resourceBindings)
    {
        _hgi->DestroyResourceBindings(&_resourceBindings);
    }

    if (_pipeline)
    {
        _hgi->DestroyGraphicsPipeline(&_pipeline);
    }
}

} // namespace HVT_NS
