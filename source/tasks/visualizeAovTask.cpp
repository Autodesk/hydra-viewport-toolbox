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

#include <hvt/tasks/visualizeAovTask.h>

#include "visualizeAOVCompute.h"

#include <hvt/tasks/resources.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/presentTask.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hio/glslfx.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

#include <iostream>
#include <limits>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{
namespace
{

const TfToken& _GetShaderPath()
{
    static TfToken shader { GetShaderPath("visualizeAov.glslfx").generic_u8string() };
    return shader;
}

} // namespace

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    // texture identifiers
    (aovIn)(depthIn)
    (idIn)(normalIn)

    // shader mixins
    ((visualizeAovVertex, "VisualizeVertex"))
    ((visualizeAovFragmentDepth,"VisualizeFragmentDepth"))
    ((visualizeAovFragmentFallback, "VisualizeFragmentFallback"))
    ((visualizeAovFragmentId, "VisualizeFragmentId"))
    ((visualizeAovFragmentNormal, "VisualizeFragmentNormal"))
    ((empty, "")));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

VisualizeAovTask::VisualizeAovTask(HdSceneDelegate*, SdfPath const& id) :
    HdxTask(id),
    _outputTextureDimensions(0),
    _screenSize {},
    _minMaxDepth {},
    _vizKernel(VizKernelNone)
{
}

VisualizeAovTask::~VisualizeAovTask()
{
    // Kernel independent resources
    {
        if (_vertexBuffer)
        {
            _GetHgi()->DestroyBuffer(&_vertexBuffer);
        }
        if (_indexBuffer)
        {
            _GetHgi()->DestroyBuffer(&_indexBuffer);
        }
        if (_sampler)
        {
            _GetHgi()->DestroySampler(&_sampler);
        }
    }

    // Kernel dependent resources
    {
        if (_outputTexture)
        {
            _GetHgi()->DestroyTexture(&_outputTexture);
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

    // Compute shader is automatically cleaned up by unique_ptr
}

static bool _IsIdAov(TfToken const& aovName)
{
    return aovName == HdAovTokens->primId || aovName == HdAovTokens->instanceId ||
        aovName == HdAovTokens->elementId || aovName == HdAovTokens->edgeId ||
        aovName == HdAovTokens->pointId;
}

bool VisualizeAovTask::_UpdateVizKernel(TfToken const& aovName)
{
    VizKernel vk = VizKernelFallback;

    if (aovName == HdAovTokens->color)
    {
        vk = VizKernelNone;
    }
    else if (HdAovHasDepthSemantic(aovName) || HdAovHasDepthStencilSemantic(aovName))
    {
        vk = VizKernelDepth;
    }
    else if (_IsIdAov(aovName))
    {
        vk = VizKernelId;
    }
    else if (aovName == HdAovTokens->normal)
    {
        vk = VizKernelNormal;
    }

    if (vk != _vizKernel)
    {
        _vizKernel = vk;
        return true;
    }
    return false;
}

TfToken const& VisualizeAovTask::_GetTextureIdentifierForShader() const
{
    switch (_vizKernel)
    {
    case VizKernelDepth:
        return _tokens->depthIn;
    case VizKernelId:
        return _tokens->idIn;
    case VizKernelNormal:
        return _tokens->normalIn;
    case VizKernelFallback:
        return _tokens->aovIn;
    default:
        TF_CODING_ERROR("Unhandled kernel viz enumeration");
        return _tokens->empty;
    }
}

TfToken const& VisualizeAovTask::_GetFragmentMixin() const
{
    switch (_vizKernel)
    {
    case VizKernelDepth:
        return _tokens->visualizeAovFragmentDepth;
    case VizKernelId:
        return _tokens->visualizeAovFragmentId;
    case VizKernelNormal:
        return _tokens->visualizeAovFragmentNormal;
    case VizKernelFallback:
        return _tokens->visualizeAovFragmentFallback;
    default:
        TF_CODING_ERROR("Unhandled kernel viz enumeration");
        return _tokens->empty;
    }
}

bool VisualizeAovTask::_CreateShaderResources(HgiTextureDesc const& inputAovTextureDesc)
{
    if (_shaderProgram)
    {
        return true;
    }

    const HioGlslfx glslfx(_GetShaderPath(), HioGlslfxTokens->defVal);

    // Setup the vertex shader (same for all kernels)
    HgiShaderFunctionHandle vertFn;
    {
        std::string vsCode;
        HgiShaderFunctionDesc vertDesc;
        vertDesc.debugName   = _tokens->visualizeAovVertex.GetString();
        vertDesc.shaderStage = HgiShaderStageVertex;
        HgiShaderFunctionAddStageInput(&vertDesc, "position", "vec4");
        HgiShaderFunctionAddStageInput(&vertDesc, "uvIn", "vec2");
        HgiShaderFunctionAddStageOutput(&vertDesc, "gl_Position", "vec4", "position");
        HgiShaderFunctionAddStageOutput(&vertDesc, "uvOut", "vec2");
        vsCode += glslfx.GetSource(_tokens->visualizeAovVertex);
        vertDesc.shaderCode = vsCode.c_str();

        vertFn = _GetHgi()->CreateShaderFunction(vertDesc);
    }

    // Setup the fragment shader based on the kernel used.
    HgiShaderFunctionHandle fragFn;
    TfToken const& mixin = _GetFragmentMixin();
    {
        std::string fsCode;
        HgiShaderFunctionDesc fragDesc;
        HgiShaderFunctionAddStageInput(&fragDesc, "uvOut", "vec2");

        HgiShaderFunctionAddTexture(&fragDesc, _GetTextureIdentifierForShader().GetString(),
            /*bindIndex = */ 0, /*dimensions = */ 2, inputAovTextureDesc.format);

        HgiShaderFunctionAddStageOutput(&fragDesc, "hd_FragColor", "vec4", "color");
        HgiShaderFunctionAddConstantParam(&fragDesc, "screenSize", "vec2");

        if (_vizKernel == VizKernelDepth)
        {
            HgiShaderFunctionAddConstantParam(&fragDesc, "minMaxDepth", "vec2");
        }
        TfToken const& fragMixin = _GetFragmentMixin();
        fragDesc.debugName       = fragMixin.GetString();
        fragDesc.shaderStage     = HgiShaderStageFragment;
        fsCode += glslfx.GetSource(fragMixin);
        fragDesc.shaderCode = fsCode.c_str();

        fragFn = _GetHgi()->CreateShaderFunction(fragDesc);
    }

    // Setup the shader program
    HgiShaderProgramDesc programDesc;
    programDesc.debugName = mixin.GetString();
    programDesc.shaderFunctions.push_back(std::move(vertFn));
    programDesc.shaderFunctions.push_back(std::move(fragFn));
    _shaderProgram = _GetHgi()->CreateShaderProgram(programDesc);

    if (!_shaderProgram->IsValid() || !vertFn->IsValid() || !fragFn->IsValid())
    {
        TF_CODING_ERROR("Failed to create AOV visualization shader %s", mixin.GetText());
        _PrintCompileErrors();
        _DestroyShaderProgram();
        return false;
    }

    return true;
}

bool VisualizeAovTask::_CreateBufferResources()
{
    if (_vertexBuffer && _indexBuffer)
    {
        return true;
    }

    // A larger-than screen triangle made to fit the screen.
    constexpr float vertData[][6] = { { -1, 3, 0, 1, 0, 2 }, { -1, -1, 0, 1, 0, 0 },
        { 3, -1, 0, 1, 2, 0 } };

    HgiBufferDesc vboDesc;
    vboDesc.debugName    = "VisualizeAovTask VertexBuffer";
    vboDesc.usage        = HgiBufferUsageVertex;
    vboDesc.initialData  = vertData;
    vboDesc.byteSize     = sizeof(vertData);
    vboDesc.vertexStride = sizeof(vertData[0]);
    _vertexBuffer        = _GetHgi()->CreateBuffer(vboDesc);

    static const int32_t indices[3] = { 0, 1, 2 };

    HgiBufferDesc iboDesc;
    iboDesc.debugName   = "VisualizeAovTask IndexBuffer";
    iboDesc.usage       = HgiBufferUsageIndex32;
    iboDesc.initialData = indices;
    iboDesc.byteSize    = sizeof(indices);
    _indexBuffer        = _GetHgi()->CreateBuffer(iboDesc);
    return true;
}

bool VisualizeAovTask::_CreateResourceBindings(HgiTextureHandle const& inputAovTexture)
{
    // Begin the resource set
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "VisualizeAovTask resourceDesc";

    HgiTextureBindDesc texBind0;
    texBind0.bindingIndex = 0;
    texBind0.stageUsage   = HgiShaderStageFragment;
    texBind0.writable     = false;
    texBind0.textures.push_back(inputAovTexture);
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

bool VisualizeAovTask::_CreatePipeline(HgiTextureDesc const& outputTextureDesc)
{
    if (_pipeline)
    {
        return true;
    }

    HgiGraphicsPipelineDesc desc;
    desc.debugName     = "AOV Visualization Pipeline";
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

    size_t bindSlots = 0;

    HgiVertexBufferDesc vboDesc;

    vboDesc.bindingIndex = (uint32_t)bindSlots++;
    vboDesc.vertexStride = sizeof(float) * 6; // pos, uv
    vboDesc.vertexAttributes.clear();
    vboDesc.vertexAttributes.push_back(posAttr);
    vboDesc.vertexAttributes.push_back(uvAttr);

    desc.vertexBuffers.push_back(std::move(vboDesc));

    // Depth test and write can be off since we only colorcorrect the color aov.
    desc.depthState.depthTestEnabled  = false;
    desc.depthState.depthWriteEnabled = false;

    // Setup rasterization state
    desc.rasterizationState.cullMode    = HgiCullModeBack;
    desc.rasterizationState.polygonMode = HgiPolygonModeFill;
    desc.rasterizationState.winding     = HgiWindingCounterClockwise;

    // Setup attachment descriptor
    _outputAttachmentDesc.blendEnabled = false;
    _outputAttachmentDesc.loadOp       = HgiAttachmentLoadOpDontCare;
    _outputAttachmentDesc.storeOp      = HgiAttachmentStoreOpStore;
    _outputAttachmentDesc.format       = outputTextureDesc.format;
    _outputAttachmentDesc.usage        = outputTextureDesc.usage;
    desc.colorAttachmentDescs.push_back(_outputAttachmentDesc);

    desc.shaderConstantsDesc.stageUsage = HgiShaderStageFragment;
    desc.shaderConstantsDesc.byteSize   = sizeof(_screenSize);
    if (_vizKernel == VizKernelDepth)
    {
        desc.shaderConstantsDesc.byteSize += sizeof(_minMaxDepth);
    }

    _pipeline = _GetHgi()->CreateGraphicsPipeline(desc);

    return true;
}

bool VisualizeAovTask::_CreateSampler(HgiTextureDesc const& inputAovTextureDesc)
{
    if (_sampler)
    {
        return true;
    }

    HgiSamplerDesc sampDesc;

    if (HgiIsFloatFormat(inputAovTextureDesc.format))
    {
        sampDesc.magFilter = HgiSamplerFilterLinear;
        sampDesc.minFilter = HgiSamplerFilterLinear;
    }
    else
    {
        sampDesc.magFilter = HgiSamplerFilterNearest;
        sampDesc.minFilter = HgiSamplerFilterNearest;
    }

    sampDesc.addressModeU = HgiSamplerAddressModeClampToEdge;
    sampDesc.addressModeV = HgiSamplerAddressModeClampToEdge;

    _sampler = _GetHgi()->CreateSampler(sampDesc);

    return true;
}

bool VisualizeAovTask::_CreateOutputTexture(GfVec3i const& dimensions)
{
    if (_outputTexture)
    {
        if (_outputTextureDimensions == dimensions)
        {
            return true;
        }
        _GetHgi()->DestroyTexture(&_outputTexture);
    }

    _outputTextureDimensions = dimensions;

    HgiTextureDesc texDesc;
    texDesc.debugName   = "Visualize Aov Output Texture";
    texDesc.dimensions  = dimensions;
    texDesc.format      = HgiFormatFloat32Vec4;
    texDesc.layerCount  = 1;
    texDesc.mipLevels   = 1;
    texDesc.sampleCount = HgiSampleCount1;
    texDesc.usage       = HgiTextureUsageBitsColorTarget | HgiTextureUsageBitsShaderRead;
    _outputTexture      = _GetHgi()->CreateTexture(texDesc);

    return bool(_outputTexture);
}

void VisualizeAovTask::_DestroyShaderProgram()
{
    if (!_shaderProgram)
        return;

    for (HgiShaderFunctionHandle fn : _shaderProgram->GetShaderFunctions())
    {
        _GetHgi()->DestroyShaderFunction(&fn);
    }
    _GetHgi()->DestroyShaderProgram(&_shaderProgram);
}

void VisualizeAovTask::_PrintCompileErrors()
{
    if (!_shaderProgram)
        return;

    for (HgiShaderFunctionHandle fn : _shaderProgram->GetShaderFunctions())
    {
        std::cout << fn->GetCompileErrors() << std::endl;
    }
    std::cout << _shaderProgram->GetCompileErrors() << std::endl;
}

void VisualizeAovTask::_UpdateMinMaxDepth(HgiTextureHandle const& inputAovTexture)
{
    // Create compute shader on first use
    if (!_depthMinMaxCompute)
    {
        _depthMinMaxCompute = std::make_unique<VisualizeAOVCompute>(_GetHgi());
    }

    // Use the compute shader to calculate min/max depth
    GfVec2f minMax = _depthMinMaxCompute->ComputeMinMaxDepth(inputAovTexture, _sampler);
    _minMaxDepth[0] = minMax[0];
    _minMaxDepth[1] = minMax[1];
}

void VisualizeAovTask::_ApplyVisualizationKernel(HgiTextureHandle const& outputTexture)
{
    GfVec3i const& dimensions = outputTexture->GetDescriptor().dimensions;

    // Prepare graphics cmds.
    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.colorAttachmentDescs.push_back(_outputAttachmentDesc);
    gfxDesc.colorTextures.push_back(outputTexture);

    // Begin rendering
    HgiGraphicsCmdsUniquePtr gfxCmds = _GetHgi()->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("Visualize AOV");
    gfxCmds->BindResources(_resourceBindings);
    gfxCmds->BindPipeline(_pipeline);
    gfxCmds->BindVertexBuffers({ { _vertexBuffer, 0, 0 } });
    const GfVec4i vp(0, 0, dimensions[0], dimensions[1]);
    _screenSize[0] = static_cast<float>(dimensions[0]);
    _screenSize[1] = static_cast<float>(dimensions[1]);

    if (_vizKernel == VizKernelDepth)
    {
        struct Uniform
        {
            float screenSize[2];
            float minMaxDepth[2];
        };
        Uniform data;
        data.screenSize[0]  = _screenSize[0];
        data.screenSize[1]  = _screenSize[1];
        data.minMaxDepth[0] = _minMaxDepth[0];
        data.minMaxDepth[1] = _minMaxDepth[1];

        gfxCmds->SetConstantValues(_pipeline, HgiShaderStageFragment, 0, sizeof(data), &data);
    }
    else
    {
        gfxCmds->SetConstantValues(
            _pipeline, HgiShaderStageFragment, 0, sizeof(_screenSize), &_screenSize);
    }

    gfxCmds->SetViewport(vp);
    gfxCmds->DrawIndexed(_indexBuffer, 3, 0, 0, 1, 0);
    gfxCmds->PopDebugGroup();

    // Done recording commands, submit work.
    _GetHgi()->SubmitCmds(gfxCmds.get());
}

void VisualizeAovTask::_Sync(
    HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        VisualizeAovTaskParams params;

        if (_GetTaskParams(delegate, &params))
        {
            // Rebuild necessary Hgi objects when aov to be visualized changes.
            if (_UpdateVizKernel(params.aovName))
            {
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
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void VisualizeAovTask::Prepare(HdTaskContext*, HdRenderIndex*) {}

void VisualizeAovTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (_vizKernel == VizKernelNone)
    {
        return;
    }

    // XXX: HdxAovInputTask sets the 'color' and 'colorIntermediate' texture
    // handles for the "active" AOV on the task context.
    // The naming is misleading and may be improved to
    // 'aovTexture' and 'aovTextureIntermediate' instead.
    if (!_HasTaskContextData(ctx, HdAovTokens->color) ||
        !_HasTaskContextData(ctx, HdxAovTokens->colorIntermediate))
    {
        return;
    }

    HgiTextureHandle aovTexture, aovTextureIntermediate;
    _GetTaskContextData(ctx, HdAovTokens->color, &aovTexture);
    _GetTaskContextData(ctx, HdxAovTokens->colorIntermediate, &aovTextureIntermediate);
    HgiTextureDesc const& aovTexDesc = aovTexture->GetDescriptor();

    // Transition from color target layout to shader read layout
    const auto oldLayout = aovTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    if (!TF_VERIFY(_CreateBufferResources(), "Failed to create buffer resources"))
    {
        return;
    }
    if (!TF_VERIFY(_CreateSampler(/*inputTextureDesc*/ aovTexDesc), "Failed to create sampler"))
    {
        return;
    }
    if (!TF_VERIFY(_CreateShaderResources(/*inputTextureDesc*/ aovTexDesc),
            "Failed to create shader resources"))
    {
        return;
    }
    if (!TF_VERIFY(_CreateResourceBindings(/*inputTexture*/ aovTexture),
            "Failed to create resource bindings"))
    {
        return;
    }

    bool canUseIntermediateAovTexture = false;
    // Normal AOV typically uses a 3 channel float format in which case we can
    // reuse the intermediate AOV to write the colorized results into.
    // For single channel AOVs like id or depth, colorize such that all color
    // components (R,G,B) are used.
    canUseIntermediateAovTexture = HdxPresentTask::IsFormatSupported(aovTexDesc.format) &&
        HgiGetComponentCount(aovTexDesc.format) >= 3;

    if (!canUseIntermediateAovTexture &&
        !TF_VERIFY(_CreateOutputTexture(aovTexDesc.dimensions), "Failed to create output texture"))
    {
        return;
    }

    HgiTextureHandle const& outputTexture =
        canUseIntermediateAovTexture ? aovTextureIntermediate : _outputTexture;
    if (!TF_VERIFY(_CreatePipeline(outputTexture->GetDescriptor()), "Failed to create pipeline"))
    {
        return;
    }

    if (_vizKernel == VizKernelDepth)
    {
        _UpdateMinMaxDepth(/*inputTexture*/ aovTexture);
    }

    _ApplyVisualizationKernel(outputTexture);

    // Restore the original color target layout
    aovTexture->SubmitLayoutChange(oldLayout);

    if (canUseIntermediateAovTexture)
    {
        // Swap the handles on the task context so that future downstream tasks
        // can use HdxAovTokens->color to get the output of this task.
        _ToggleRenderTarget(ctx);
    }
    else
    {
        (*ctx)[HdAovTokens->color] = VtValue(_outputTexture);
    }
}

const TfToken& VisualizeAovTask::GetToken()
{
    static const TfToken token { "visualizeAovTask" };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const VisualizeAovTaskParams& pv)
{
    out << "VisualizeAovTask Params: " << pv.aovName;
    return out;
}

bool operator==(const VisualizeAovTaskParams& lhs, const VisualizeAovTaskParams& rhs)
{
    return lhs.aovName == rhs.aovName;
}

bool operator!=(const VisualizeAovTaskParams& lhs, const VisualizeAovTaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
