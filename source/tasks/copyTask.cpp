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

#include <hvt/tasks/copyTask.h>

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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
    #pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(
    _tokens, ((vtbCopyVertex, "CopyVertex"))((vtbCopyFragment, "CopyFragment"))(vtbCopyShader));

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

CopyTask::CopyTask(HdSceneDelegate* /* delegate */, SdfPath const& uid) :
    HdxTask(uid),
    _indexBuffer(),
    _vertexBuffer(),
    _sampler(),
    _shaderProgram(),
    _resourceBindings(),
    _pipeline()
{
}

CopyTask::~CopyTask()
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

bool CopyTask::_CreateShaderResources()
{
    if (_shaderProgram)
    {
        return true;
    }

    const HioGlslfx glslfx(_CopyShaderPath(), HioGlslfxTokens->defVal);

    // Setup the vertex shader
    std::string vsCode;
    HgiShaderFunctionDesc vertDesc;
    vertDesc.debugName   = _tokens->vtbCopyVertex.GetString();
    vertDesc.shaderStage = HgiShaderStageVertex;
    HgiShaderFunctionAddStageInput(&vertDesc, "position", "vec4");
    HgiShaderFunctionAddStageInput(&vertDesc, "uvIn", "vec2");
    HgiShaderFunctionAddStageOutput(&vertDesc, "gl_Position", "vec4", "position");
    HgiShaderFunctionAddStageOutput(&vertDesc, "uvOut", "vec2");
    vsCode += glslfx.GetSource(_tokens->vtbCopyVertex);
    vertDesc.shaderCode            = vsCode.c_str();
    HgiShaderFunctionHandle vertFn = _GetHgi()->CreateShaderFunction(vertDesc);

    // Setup the fragment shader
    std::string fsCode;
    HgiShaderFunctionDesc fragDesc;
    HgiShaderFunctionAddStageInput(&fragDesc, "uvOut", "vec2");
    HgiShaderFunctionAddTexture(&fragDesc, "colorIn");
    HgiShaderFunctionAddStageOutput(&fragDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddConstantParam(&fragDesc, "screenSize", "vec2");
    fragDesc.debugName   = _tokens->vtbCopyFragment.GetString();
    fragDesc.shaderStage = HgiShaderStageFragment;
    fsCode += glslfx.GetSource(_tokens->vtbCopyFragment);

    fragDesc.shaderCode            = fsCode.c_str();
    HgiShaderFunctionHandle fragFn = _GetHgi()->CreateShaderFunction(fragDesc);

    // Setup the shader program
    HgiShaderProgramDesc programDesc;
    programDesc.debugName = _tokens->vtbCopyFragment.GetString();
    programDesc.shaderFunctions.push_back(std::move(vertFn));
    programDesc.shaderFunctions.push_back(std::move(fragFn));
    _shaderProgram = _GetHgi()->CreateShaderProgram(programDesc);

    if (!_shaderProgram->IsValid() || !vertFn->IsValid() || !fragFn->IsValid())
    {
        TF_CODING_ERROR("Failed to create Copy shader");
        _PrintCompileErrors();
        _DestroyShaderProgram();
        return false;
    }

    return true;
}

bool CopyTask::_CreateBufferResources()
{
    if (_vertexBuffer)
    {
        return true;
    }

    // A larger-than screen triangle made to fit the screen.
    constexpr float vertData[][6] = { { -1, 3, 0, 1, 0, 2 }, { -1, -1, 0, 1, 0, 0 },
        { 3, -1, 0, 1, 2, 0 } };

    HgiBufferDesc vboDesc;
    vboDesc.debugName    = "CopyTask VertexBuffer";
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
    iboDesc.debugName   = "CopyTask IndexBuffer";
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

bool CopyTask::_CreateResourceBindings(HgiTextureHandle const& aovTexture)
{
    // Begin the resource set
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "Copy";

    HgiTextureBindDesc texBind0;
    texBind0.bindingIndex = 0;
    texBind0.stageUsage   = HgiShaderStageFragment;
    texBind0.textures.push_back(aovTexture);
    texBind0.samplers.push_back(_sampler);
    resourceDesc.textures.push_back(std::move(texBind0));

    // If nothing has changed in the descriptor we avoid re-creating the
    // resource bindings object.
    if (_resourceBindings)
    {
        HgiResourceBindingsDesc const& desc = _resourceBindings->GetDescriptor();
        if (desc == resourceDesc)
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

bool CopyTask::_CreatePipeline(HgiTextureHandle const& aovTexture)
{
    if (_pipeline)
    {
        if (_attachment0.format == aovTexture->GetDescriptor().format)
        {
            return true;
        }

        _GetHgi()->DestroyGraphicsPipeline(&_pipeline);
    }

    HgiGraphicsPipelineDesc desc;
    desc.debugName     = "Copy Pipeline";
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

    // Depth test and write can be off since we only colorcorrect the color aov.
    desc.depthState.depthTestEnabled  = false;
    desc.depthState.depthWriteEnabled = false;

    // We don't use the stencil mask in this task.
    desc.depthState.stencilTestEnabled = false;

    // Alpha to coverage would prevent any pixels that have an alpha of 0.0 from
    // being written. We want to color correct all pixels. Even background
    // pixels that were set with a clearColor alpha of 0.0.
    desc.multiSampleState.alphaToCoverageEnable = false;

    // The MSAA on renderPipelineState has to match the render target.
    desc.multiSampleState.sampleCount       = aovTexture->GetDescriptor().sampleCount;
    desc.multiSampleState.multiSampleEnable = desc.multiSampleState.sampleCount > 1;

    // Setup rasterization state
    desc.rasterizationState.cullMode    = HgiCullModeBack;
    desc.rasterizationState.polygonMode = HgiPolygonModeFill;
    desc.rasterizationState.winding     = HgiWindingCounterClockwise;

    // Setup attachment descriptor
    _attachment0.blendEnabled = false;
    _attachment0.loadOp       = HgiAttachmentLoadOpDontCare;
    _attachment0.storeOp      = HgiAttachmentStoreOpStore;
    _attachment0.format       = aovTexture->GetDescriptor().format;
    _attachment0.usage        = aovTexture->GetDescriptor().usage;
    desc.colorAttachmentDescs.push_back(_attachment0);

    desc.shaderConstantsDesc.stageUsage = HgiShaderStageFragment;
    desc.shaderConstantsDesc.byteSize   = sizeof(Uniforms);

    _pipeline = _GetHgi()->CreateGraphicsPipeline(desc);

    return true;
}

bool CopyTask::_CreateSampler()
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

    _sampler = _GetHgi()->CreateSampler(sampDesc);

    return true;
}

void CopyTask::_ApplyCopy(HgiTextureHandle const& aovTexture)
{
    GfVec3i const& dimensions = aovTexture->GetDescriptor().dimensions;

    // Prepare graphics cmds.
    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.colorAttachmentDescs.push_back(_attachment0);
    gfxDesc.colorTextures.push_back(aovTexture);

    const GfVec4i viewport(0, 0, dimensions[0], dimensions[1]);
    Uniforms uniform;
    uniform.screenSize[0] = static_cast<float>(dimensions[0]);
    uniform.screenSize[1] = static_cast<float>(dimensions[1]);

    // Begin rendering
    HgiGraphicsCmdsUniquePtr gfxCmds = _GetHgi()->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("Copy");
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

void CopyTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        CopyTaskParams params;
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

void CopyTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */) {}

void CopyTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // We currently only support the color aov.
    if (_params.aovName != HdAovTokens->color)
    {
        return;
    }

    static const TfToken msaaToken("colorMSAA");

    // The color aov has the rendered results and we wish to
    // copy into colorMSAA aov to ensure we do not
    // read from the same color target that we write into.
    if (!_HasTaskContextData(ctx, HdAovTokens->color) ||
        !_HasTaskContextData(ctx, msaaToken))
    {
        return;
    }

    HgiTextureHandle aovTexture, aovTextureIntermediate;
    _GetTaskContextData(ctx, HdAovTokens->color, &aovTexture);
    _GetTaskContextData(ctx, msaaToken, &aovTextureIntermediate);

    if (aovTexture == aovTextureIntermediate)
    {
        return;
    }

    // The CopyPass is copying the aovTexture onto the aovTextureIntermediate,
    // thus aovTexture need to a shader read state.
    aovTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    if (!TF_VERIFY(_CreateBufferResources(), "Resource creation failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreateSampler(), "Samnpler creation failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreateShaderResources(), "Shader creation failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreateResourceBindings(aovTexture), "Resource binding failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreatePipeline(aovTextureIntermediate), "Pipeline creation failed."))
    {
        return;
    }

    // However we are swapping avoTexture and aovTextureIntermediate, so the
    // avoTexture need to be in ColorTarget again, this close the image layout
    // loop of this pass.
    _ApplyCopy(aovTextureIntermediate);
    aovTexture->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);

    // Toggle color and MSAA buffers.
    _ToggleRenderTarget(ctx);
}

void CopyTask::_DestroyShaderProgram()
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

void CopyTask::_PrintCompileErrors()
{
    if (!_shaderProgram)
    {
        return;
    }

    for (HgiShaderFunctionHandle fn : _shaderProgram->GetShaderFunctions())
    {
        const std::string errors = fn->GetCompileErrors();
        if (errors.size() > 0)
        {
            TF_CODING_ERROR(errors);
        }
    }
    const std::string errors = _shaderProgram->GetCompileErrors();
    if (errors.size() > 0)
    {
        TF_CODING_ERROR(errors);
    }
}

void CopyTask::_ToggleRenderTarget(HdTaskContext* ctx)
{
    if (!_HasTaskContextData(ctx, HdAovTokens->color))
    {
        return;
    }

    HgiTextureHandle aovTextureIntermediate;
    _GetTaskContextData(ctx, TfToken("colorMSAA"), &aovTextureIntermediate);

    // As this is the end of pipeline the code only must copy the intermediate buffer in
    // the color buffer.
    (*ctx)[HdAovTokens->color] = VtValue(aovTextureIntermediate);
}

const TfToken& CopyTask::_CopyShaderPath()
{
    static const TfToken shader { GetShaderPath("copy.glslfx").generic_u8string() };
    return shader;
}

const TfToken& CopyTask::GetToken()
{
    static const TfToken token { "copyTask" };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const CopyTaskParams& pv)
{
    out << "CopyTask Params: " << pv.aovName;
    return out;
}

bool operator==(const CopyTaskParams& lhs, const CopyTaskParams& rhs)
{
    return lhs.aovName == rhs.aovName;
}

bool operator!=(const CopyTaskParams& lhs, const CopyTaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
