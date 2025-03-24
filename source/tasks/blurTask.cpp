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

#include <hvt/tasks/blurTask.h>

#include <hvt/tasks/resources.h>

// clang-format off
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/base/tf/getenv.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/imaging/hio/glslfx.h>

#include <pxr/imaging/hgi/graphicsCmds.h>
#include <pxr/imaging/hgi/graphicsCmdsDesc.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif _MSC_VER
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(
    _tokens, ((vtbBlurVertex, "BlurVertex"))((vtbBlurFragment, "BlurFragment"))(vtbBlurShader));

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

// Prepare uniform buffer for GPU computation.
struct Uniforms
{
    GfVec2f screenSize;
    float blurAmount = 0.5f;
};

BlurTask::BlurTask(HdSceneDelegate* /* delegate */, SdfPath const& uid) :
    HdxTask(uid),
    _indexBuffer(),
    _vertexBuffer(),
    _sampler(),
    _shaderProgram(),
    _resourceBindings(),
    _pipeline()
{
}

BlurTask::~BlurTask()
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

bool BlurTask::_CreateShaderResources()
{
    if (_shaderProgram)
    {
        return true;
    }

    const HioGlslfx glslfx(_BlurShaderPath(), HioGlslfxTokens->defVal);

    // Setup the vertex shader
    std::string vsCode;
    HgiShaderFunctionDesc vertDesc;
    vertDesc.debugName   = _tokens->vtbBlurVertex.GetString();
    vertDesc.shaderStage = HgiShaderStageVertex;
    HgiShaderFunctionAddStageInput(&vertDesc, "position", "vec4");
    HgiShaderFunctionAddStageInput(&vertDesc, "uvIn", "vec2");
    HgiShaderFunctionAddStageOutput(&vertDesc, "gl_Position", "vec4", "position");
    HgiShaderFunctionAddStageOutput(&vertDesc, "uvOut", "vec2");
    vsCode += glslfx.GetSource(_tokens->vtbBlurVertex);
    vertDesc.shaderCode            = vsCode.c_str();
    HgiShaderFunctionHandle vertFn = _GetHgi()->CreateShaderFunction(vertDesc);

    // Setup the fragment shader
    std::string fsCode;
    HgiShaderFunctionDesc fragDesc;
    HgiShaderFunctionAddStageInput(&fragDesc, "uvOut", "vec2");
    HgiShaderFunctionAddTexture(&fragDesc, "colorIn");
    HgiShaderFunctionAddStageOutput(&fragDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddConstantParam(&fragDesc, "screenSize", "vec2");
    HgiShaderFunctionAddConstantParam(&fragDesc, "blurAmount", "float");
    fragDesc.debugName   = _tokens->vtbBlurFragment.GetString();
    fragDesc.shaderStage = HgiShaderStageFragment;
    fsCode += glslfx.GetSource(_tokens->vtbBlurFragment);

    fragDesc.shaderCode            = fsCode.c_str();
    HgiShaderFunctionHandle fragFn = _GetHgi()->CreateShaderFunction(fragDesc);

    // Setup the shader program
    HgiShaderProgramDesc programDesc;
    programDesc.debugName = _tokens->vtbBlurFragment.GetString();
    programDesc.shaderFunctions.push_back(std::move(vertFn));
    programDesc.shaderFunctions.push_back(std::move(fragFn));
    _shaderProgram = _GetHgi()->CreateShaderProgram(programDesc);

    if (!_shaderProgram->IsValid() || !vertFn->IsValid() || !fragFn->IsValid())
    {
        TF_CODING_ERROR("Failed to create blur shader");
        _PrintCompileErrors();
        _DestroyShaderProgram();
        return false;
    }

    return true;
}

bool BlurTask::_CreateBufferResources()
{
    if (_vertexBuffer)
    {
        return true;
    }

    // A larger-than screen triangle made to fit the screen.
    constexpr float vertData[][6] = { { -1, 3, 0, 1, 0, 2 }, { -1, -1, 0, 1, 0, 0 },
        { 3, -1, 0, 1, 2, 0 } };

    HgiBufferDesc vboDesc;
    vboDesc.debugName    = "BlurTask VertexBuffer";
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
    iboDesc.debugName   = "BlurTask IndexBuffer";
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

bool BlurTask::_CreateResourceBindings(HgiTextureHandle const& aovTexture)
{
    // Begin the resource set
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "Blur";

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

bool BlurTask::_CreatePipeline(HgiTextureHandle const& aovTexture)
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
    desc.debugName     = "Blur Pipeline";
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
    desc.multiSampleState.sampleCount = aovTexture->GetDescriptor().sampleCount;

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

bool BlurTask::_CreateSampler()
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

void BlurTask::_ApplyBlur(HgiTextureHandle const& aovTexture)
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
    uniform.blurAmount    = _params.blurAmount;

    // Begin rendering
    HgiGraphicsCmdsUniquePtr gfxCmds = _GetHgi()->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("Blur");
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

void BlurTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        BlurTaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            _params = params;

            // Rebuild Hgi objects when ColorCorrection params change
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

void BlurTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */) {}

void BlurTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // We currently only color correct the color aov.
    if (_params.aovName != HdAovTokens->color)
    {
        return;
    }

    // The color aov has the rendered results and we wish to
    // blur into colorIntermediate aov to ensure we do not
    // read from the same color target that we write into.
    if (!_HasTaskContextData(ctx, HdAovTokens->color) ||
        !_HasTaskContextData(ctx, HdxAovTokens->colorIntermediate))
    {
        return;
    }

    HgiTextureHandle aovTexture, aovTextureIntermediate;
    _GetTaskContextData(ctx, HdAovTokens->color, &aovTexture);
    _GetTaskContextData(ctx, HdxAovTokens->colorIntermediate, &aovTextureIntermediate);

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
    if (!TF_VERIFY(_CreateResourceBindings(aovTexture), "Resource binding failed."))
    {
        return;
    }
    if (!TF_VERIFY(_CreatePipeline(aovTextureIntermediate), "Pipeline creation failed."))
    {
        return;
    }

    _ApplyBlur(aovTextureIntermediate);

    // Toggle color and colorIntermediate
    _ToggleRenderTarget(ctx);
}

void BlurTask::_DestroyShaderProgram()
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

void BlurTask::_PrintCompileErrors()
{
    if (!_shaderProgram)
        return;

    for (HgiShaderFunctionHandle fn : _shaderProgram->GetShaderFunctions())
    {
        std::string errors = fn->GetCompileErrors();
        if (errors.size() > 0)
        {
            TF_CODING_ERROR(errors);
        }
    }
    std::string errors = _shaderProgram->GetCompileErrors();
    if (errors.size() > 0)
    {
        TF_CODING_ERROR(errors);
    }
}

void BlurTask::_ToggleRenderTarget(HdTaskContext* ctx)
{
    if (!_HasTaskContextData(ctx, HdAovTokens->color))
    {
        return;
    }

    HgiTextureHandle aovTexture, aovTextureIntermediate;

    if (_HasTaskContextData(ctx, HdxAovTokens->colorIntermediate))
    {
        _GetTaskContextData(ctx, HdAovTokens->color, &aovTexture);
        _GetTaskContextData(ctx, HdxAovTokens->colorIntermediate, &aovTextureIntermediate);
        (*ctx)[HdAovTokens->color]              = VtValue(aovTextureIntermediate);
        (*ctx)[HdxAovTokens->colorIntermediate] = VtValue(aovTexture);
    }
}

const TfToken& BlurTask::_BlurShaderPath()
{
    static const TfToken shader { hvt::GetShaderPath("blur.glslfx").generic_u8string() };
    return shader;
}

const TfToken& BlurTask::GetToken()
{
    static const TfToken token { "blurTask" };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const BlurTaskParams& pv)
{
    out << "BlurTask Params: " << pv.blurAmount << " " << pv.aovName;
    return out;
}

bool operator==(const BlurTaskParams& lhs, const BlurTaskParams& rhs)
{
    return lhs.blurAmount == rhs.blurAmount && lhs.aovName == rhs.aovName;
}

bool operator!=(const BlurTaskParams& lhs, const BlurTaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace hvt
