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

#include <hvt/tasks/copyDepthToDepthMsaaTask.h>

#include <hvt/tasks/resources.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/base/tf/getenv.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/imaging/hio/glslfx.h>

#include <pxr/imaging/hgi/graphicsCmds.h>
#include <pxr/imaging/hgi/graphicsCmdsDesc.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
    #pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

TF_DEFINE_PRIVATE_TOKENS(
    _tokens, 
    ((vtbCopyDepthVertex, "CopyDepthVertex"))
    ((vtbCopyDepthFragment, "CopyDepthFragment"))
    (vtbCopyDepthShader));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

// Prepare uniform buffer for GPU computation.
struct Uniforms
{
    GfVec2f screenSize;
};

CopyDepthToDepthMsaaTask::CopyDepthToDepthMsaaTask(HdSceneDelegate* /* delegate */, SdfPath const& uid) :
    HdxTask(uid),
    _indexBuffer(),
    _vertexBuffer(),
    _sampler(),
    _shaderProgram(),
    _resourceBindings(),
    _pipeline()
{
}

CopyDepthToDepthMsaaTask::~CopyDepthToDepthMsaaTask()
{
    if (_sampler)
    {
        _GetHgi()->DestroySampler(&_sampler);
    }

    if (_vertexBuffer)
    {
        _GetHgi()->DestroyBuffer(&_vertexBuffer);
    }

    if (_indexBuffer)
    {
        _GetHgi()->DestroyBuffer(&_indexBuffer);
    }

    if (_shaderProgram)
    {
        _DestroyShaderProgram();
    }

    if (_resourceBindings)
    {
        _GetHgi()->DestroyResourceBindings(&_resourceBindings);
    }

    if (_pipeline)
    {
        _GetHgi()->DestroyGraphicsPipeline(&_pipeline);
    }
}

bool CopyDepthToDepthMsaaTask::_CreateShaderResources()
{
    if (_shaderProgram)
    {
        return true;
    }

    const HioGlslfx glslfx(_CopyDepthShaderPath(), HioGlslfxTokens->defVal);

    // Setup the vertex shader
    std::string vsCode;
    HgiShaderFunctionDesc vertDesc;
    vertDesc.debugName   = _tokens->vtbCopyDepthVertex.GetString();
    vertDesc.shaderStage = HgiShaderStageVertex;
    HgiShaderFunctionAddStageInput(&vertDesc, "position", "vec4");
    HgiShaderFunctionAddStageInput(&vertDesc, "uvIn", "vec2");
    HgiShaderFunctionAddStageOutput(&vertDesc, "gl_Position", "vec4", "position");
    HgiShaderFunctionAddStageOutput(&vertDesc, "uvOut", "vec2");
    vsCode += glslfx.GetSource(_tokens->vtbCopyDepthVertex);
    vertDesc.shaderCode            = vsCode.c_str();
    HgiShaderFunctionHandle vertFn = _GetHgi()->CreateShaderFunction(vertDesc);

    // Setup the fragment shader
    std::string fsCode;
    HgiShaderFunctionDesc fragDesc;
    HgiShaderFunctionAddStageInput(&fragDesc, "uvOut", "vec2");
    HgiShaderFunctionAddTexture(&fragDesc, "depthIn");
    HgiShaderFunctionAddStageOutput(&fragDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddStageOutput(&fragDesc, "gl_FragDepth", "float", "depth(any)");
    HgiShaderFunctionAddConstantParam(&fragDesc, "screenSize", "vec2");
    fragDesc.debugName   = _tokens->vtbCopyDepthFragment.GetString();
    fragDesc.shaderStage = HgiShaderStageFragment;
    fsCode += glslfx.GetSource(_tokens->vtbCopyDepthFragment);

    fragDesc.shaderCode            = fsCode.c_str();
    HgiShaderFunctionHandle fragFn = _GetHgi()->CreateShaderFunction(fragDesc);

    // Setup the shader program
    HgiShaderProgramDesc programDesc;
    programDesc.debugName = _tokens->vtbCopyDepthFragment.GetString();
    programDesc.shaderFunctions.push_back(std::move(vertFn));
    programDesc.shaderFunctions.push_back(std::move(fragFn));
    _shaderProgram = _GetHgi()->CreateShaderProgram(programDesc);

    if (!_shaderProgram->IsValid() || !vertFn->IsValid() || !fragFn->IsValid())
    {
        _PrintCompileErrors();
        _DestroyShaderProgram();
        return false;
    }

    return true;
}

bool CopyDepthToDepthMsaaTask::_CreateBufferResources()
{
    if (_vertexBuffer)
    {
        return true;
    }

    // A larger-than screen triangle made to fit the screen.
    constexpr float vertData[][6] = { { -1, 3, 0, 1, 0, 2 }, { -1, -1, 0, 1, 0, 0 },
        { 3, -1, 0, 1, 2, 0 } };

    HgiBufferDesc vboDesc;
    vboDesc.debugName    = "CopyDepthToDepthMsaaTask VertexBuffer";
    vboDesc.usage        = HgiBufferUsageVertex;
    vboDesc.initialData  = vertData;
    vboDesc.byteSize     = sizeof(vertData);
    vboDesc.vertexStride = sizeof(vertData[0]);
    _vertexBuffer        = _GetHgi()->CreateBuffer(vboDesc);
    if (!_vertexBuffer)
    {
        return false;
    }

    constexpr int32_t indices[3] = { 0, 1, 2 };

    HgiBufferDesc iboDesc;
    iboDesc.debugName   = "CopyDepthToDepthMsaaTask IndexBuffer";
    iboDesc.usage       = HgiBufferUsageIndex32;
    iboDesc.initialData = indices;
    iboDesc.byteSize    = sizeof(indices);
    _indexBuffer        = _GetHgi()->CreateBuffer(iboDesc);
    if (!_indexBuffer)
    {
        return false;
    }

    return true;
}

bool CopyDepthToDepthMsaaTask::_CreateResourceBindings(HgiTextureHandle const& sourceDepthTexture)
{
    // Begin the resource set
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "CopyDepthToDepthMsaa";

    HgiTextureBindDesc texBind0;
    texBind0.bindingIndex = 0;
    texBind0.stageUsage   = HgiShaderStageFragment;
    texBind0.textures.push_back(sourceDepthTexture);
    texBind0.samplers.push_back(_sampler);
    resourceDesc.textures.push_back(std::move(texBind0));

    // If nothing has changed in the descriptor we avoid re-creating the
    // resource bindings object.
    if (_resourceBindings)
    {
        const HgiResourceBindingsDesc oldResourceDesc =
            _resourceBindings->GetDescriptor();
        if (resourceDesc == oldResourceDesc)
        {
            return true;
        }
        else
        {
            _GetHgi()->DestroyResourceBindings(&_resourceBindings);
        }
    }

    _resourceBindings = _GetHgi()->CreateResourceBindings(resourceDesc);

    return true;
}

bool CopyDepthToDepthMsaaTask::_CreatePipeline(HgiTextureHandle const& targetDepthTexture)
{
    if (_pipeline)
    {
        const HgiGraphicsPipelineDesc oldPipeDesc = _pipeline->GetDescriptor();
        const bool msaaCountMatches =
            oldPipeDesc.multiSampleState.sampleCount == targetDepthTexture->GetDescriptor().sampleCount;
        if (msaaCountMatches)
        {
            return true;
        }
        _GetHgi()->DestroyGraphicsPipeline(&_pipeline);
    }

    HgiGraphicsPipelineDesc desc;
    desc.debugName     = "CopyDepthToDepthMsaa Pipeline";
    desc.shaderProgram = _shaderProgram;

    // Describe the vertex buffer
    HgiVertexAttributeDesc posAttr;
    posAttr.format             = HgiFormatFloat32Vec3;
    posAttr.offset             = 0;
    posAttr.shaderBindLocation = 0;

    HgiVertexAttributeDesc uvAttr;
    uvAttr.format             = HgiFormatFloat32Vec2;
    uvAttr.offset             = sizeof(float) * 4; // after posAttr
    uvAttr.shaderBindLocation = 1;

    uint32_t bindSlots = 0;

    HgiVertexBufferDesc vboDesc;

    vboDesc.bindingIndex = bindSlots++;
    vboDesc.vertexStride = sizeof(float) * 6; // pos, uv
    vboDesc.vertexAttributes.clear();
    vboDesc.vertexAttributes.push_back(posAttr);
    vboDesc.vertexAttributes.push_back(uvAttr);

    desc.vertexBuffers.push_back(std::move(vboDesc));

    // Enable depth test and write since we're writing to the depth buffer
    desc.depthState.depthTestEnabled  = true;  // We want to write all pixels
    desc.depthState.depthCompareFn    = HgiCompareFunctionAlways; // Always write depth
    desc.depthState.depthWriteEnabled = true;   // We need to write depth

    // We don't use the stencil mask in this task.
    desc.depthState.stencilTestEnabled = false;

    // Alpha to coverage would prevent any pixels that have an alpha of 0.0 from
    // being written. We want to copy all depth pixels.
    desc.multiSampleState.alphaToCoverageEnable = false;

    // The MSAA on renderPipelineState has to match the render target.
    desc.multiSampleState.sampleCount       = targetDepthTexture->GetDescriptor().sampleCount;
    desc.multiSampleState.multiSampleEnable = desc.multiSampleState.sampleCount > 1;

    // Setup rasterization state
    desc.rasterizationState.cullMode    = HgiCullModeBack;
    desc.rasterizationState.polygonMode = HgiPolygonModeFill;
    desc.rasterizationState.winding     = HgiWindingCounterClockwise;

    // Setup depth attachment descriptor
    _depthAttachment.blendEnabled = false;
    _depthAttachment.loadOp       = HgiAttachmentLoadOpDontCare;
    _depthAttachment.storeOp      = HgiAttachmentStoreOpStore;
    _depthAttachment.format       = targetDepthTexture->GetDescriptor().format;
    _depthAttachment.usage        = targetDepthTexture->GetDescriptor().usage;
    desc.depthAttachmentDesc      = _depthAttachment;

    desc.shaderConstantsDesc.stageUsage = HgiShaderStageFragment;
    desc.shaderConstantsDesc.byteSize   = sizeof(Uniforms);

    _pipeline = _GetHgi()->CreateGraphicsPipeline(desc);

    return true;
}

bool CopyDepthToDepthMsaaTask::_CreateSampler()
{
    if (_sampler)
    {
        return true;
    }

    HgiSamplerDesc sampDesc;

    sampDesc.magFilter = HgiSamplerFilterNearest; // Use nearest for depth to avoid interpolation
    sampDesc.minFilter = HgiSamplerFilterNearest;

    sampDesc.addressModeU = HgiSamplerAddressModeClampToEdge;
    sampDesc.addressModeV = HgiSamplerAddressModeClampToEdge;

    _sampler = _GetHgi()->CreateSampler(sampDesc);

    return true;
}

void CopyDepthToDepthMsaaTask::_ApplyCopyDepth(HgiTextureHandle const& /*sourceDepthTexture*/,
                                             HgiTextureHandle const& targetDepthTexture)
{
    GfVec3i const& dimensions = targetDepthTexture->GetDescriptor().dimensions;

    // Prepare graphics cmds.
    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.depthAttachmentDesc = _depthAttachment;
    gfxDesc.depthTexture = targetDepthTexture;

    const GfVec4i viewport(0, 0, dimensions[0], dimensions[1]);
    Uniforms uniform;
    uniform.screenSize[0] = static_cast<float>(dimensions[0]);
    uniform.screenSize[1] = static_cast<float>(dimensions[1]);

    // Begin rendering
    HgiGraphicsCmdsUniquePtr gfxCmds = _GetHgi()->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("CopyDepthToDepthMsaa");
    gfxCmds->BindResources(_resourceBindings);
    gfxCmds->BindPipeline(_pipeline);
    gfxCmds->BindVertexBuffers({ { _vertexBuffer, 0, 0 } });
    gfxCmds->SetConstantValues(_pipeline, HgiShaderStageFragment, 0, sizeof(uniform), &uniform);
    gfxCmds->SetViewport(viewport);
    gfxCmds->DrawIndexed(_indexBuffer, 3, 0, 0, 1, 0);
    gfxCmds->PopDebugGroup();

    // Done recording commands, submit work.
    _GetHgi()->SubmitCmds(gfxCmds.get());
}

void CopyDepthToDepthMsaaTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        CopyDepthToDepthMsaaTaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            _params = params;

            // Rebuild Hgi objects when CopyTask params change.

            _DestroyShaderProgram();

            if (_resourceBindings)
            {
                _GetHgi()->DestroyResourceBindings(&_resourceBindings);
            }
            if (_pipeline)
            {
                _GetHgi()->DestroyGraphicsPipeline(&_pipeline);
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void CopyDepthToDepthMsaaTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */) {}

void CopyDepthToDepthMsaaTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // Check that both source and target depth AOVs exist
    if (!_HasTaskContextData(ctx, _params.sourceDepthAovName) ||
        !_HasTaskContextData(ctx, _params.targetDepthAovName))
    {
        return;
    }

    HgiTextureHandle sourceDepthTexture, targetDepthTexture;
    _GetTaskContextData(ctx, _params.sourceDepthAovName, &sourceDepthTexture);
    _GetTaskContextData(ctx, _params.targetDepthAovName, &targetDepthTexture);

    if (sourceDepthTexture == targetDepthTexture)
    {
        return; // Nothing to do
    }

    // The source depth texture needs to be in shader read state.
    sourceDepthTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    if (!TF_VERIFY(_CreateBufferResources(), "Resource creation failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreateSampler(), "Sampler creation failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreateShaderResources(), "Shader creation failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreateResourceBindings(sourceDepthTexture), "Resource binding failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreatePipeline(targetDepthTexture), "Pipeline creation failed."))
    {
        return;
    }

    // Copy depth from source to target
    _ApplyCopyDepth(sourceDepthTexture, targetDepthTexture);

    // Restore the source depth texture layout
    sourceDepthTexture->SubmitLayoutChange(HgiTextureUsageBitsDepthTarget);
}

void CopyDepthToDepthMsaaTask::_DestroyShaderProgram()
{
    if (!_shaderProgram)
    {
        return;
    }

    for (HgiShaderFunctionHandle fn : _shaderProgram->GetShaderFunctions())
    {
        _GetHgi()->DestroyShaderFunction(&fn);
    }
    _GetHgi()->DestroyShaderProgram(&_shaderProgram);
}

void CopyDepthToDepthMsaaTask::_PrintCompileErrors()
{
    if (!_shaderProgram)
    {
        return;
    }
    
    for (HgiShaderFunctionHandle fn : _shaderProgram->GetShaderFunctions())
    {
        std::cout << fn->GetCompileErrors() << std::endl;
    }
    if (!_shaderProgram->GetCompileErrors().empty())
    {
        std::cout << _shaderProgram->GetCompileErrors() << std::endl;
    }
}

const TfToken& CopyDepthToDepthMsaaTask::_CopyDepthShaderPath()
{
    static const TfToken shader { GetShaderPath("copyDepthToDepthMsaa.glslfx").generic_u8string() };
    return shader;
}

const TfToken& CopyDepthToDepthMsaaTask::GetToken()
{
    static const TfToken token("copyDepthToDepthMsaa");
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const CopyDepthToDepthMsaaTaskParams& pv)
{
    out << "CopyDepthToDepthMsaaTask Params: (" << pv.sourceDepthAovName << ", " << pv.targetDepthAovName << ")";
    return out;
}

bool operator==(const CopyDepthToDepthMsaaTaskParams& lhs, const CopyDepthToDepthMsaaTaskParams& rhs)
{
    return lhs.sourceDepthAovName == rhs.sourceDepthAovName && lhs.targetDepthAovName == rhs.targetDepthAovName;
}

bool operator!=(const CopyDepthToDepthMsaaTaskParams& lhs, const CopyDepthToDepthMsaaTaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS