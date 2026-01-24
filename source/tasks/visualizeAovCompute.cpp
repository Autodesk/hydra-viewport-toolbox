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

#include "tasks/visualizeAovCompute.h"

#include <hvt/tasks/resources.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hgi/graphicsCmds.h>
#include <pxr/imaging/hgi/graphicsCmdsDesc.h>
#include <pxr/imaging/hio/glslfx.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{

const TfToken& _GetShaderPath()
{
    static TfToken shader { GetShaderPath("visualizeAovDepthMinMax.glslfx").generic_u8string() };
    return shader;
}

// Reduction factor per pass (each output pixel covers NxN input pixels)
constexpr int kReductionFactor = 8;

// Final texture size (1x1 for direct GPU sampling, no CPU readback needed)
constexpr int kFinalTextureSize = 1;

// Token definitions
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((depthMinMaxVertex, "VisualizeAovDepthMinMaxVertex"))
    ((depthMinMaxFragment, "VisualizeAovDepthMinMaxFragment"))
    ((depthMinMaxReductionFragment, "VisualizeAovDepthMinMaxReductionFragment")));

// Uniforms for the shaders
struct Uniforms
{
    GfVec2f screenSize;       // Input texture size
    GfVec2f outputScreenSize; // Output texture size
};

} // namespace

VisualizeAovCompute::VisualizeAovCompute(Hgi* hgi) : _hgi(hgi) {}

VisualizeAovCompute::~VisualizeAovCompute()
{
    _DestroyResources();
}

void VisualizeAovCompute::_DestroyResources()
{
    // Destroy shader programs
    _DestroyShaderProgram(_firstPassShaderProgram);
    _DestroyShaderProgram(_reductionShaderProgram);

    // Destroy pipelines
    if (_firstPassPipeline)
    {
        _hgi->DestroyGraphicsPipeline(&_firstPassPipeline);
    }
    if (_reductionPipeline)
    {
        _hgi->DestroyGraphicsPipeline(&_reductionPipeline);
    }

    // Destroy buffers
    if (_vertexBuffer)
    {
        _hgi->DestroyBuffer(&_vertexBuffer);
    }
    if (_indexBuffer)
    {
        _hgi->DestroyBuffer(&_indexBuffer);
    }

    // Destroy resource bindings
    if (_firstPassResourceBindings)
    {
        _hgi->DestroyResourceBindings(&_firstPassResourceBindings);
    }
    if (_reductionResourceBindings)
    {
        _hgi->DestroyResourceBindings(&_reductionResourceBindings);
    }

    // Destroy sampler
    if (_reductionSampler)
    {
        _hgi->DestroySampler(&_reductionSampler);
    }

    // Destroy reduction textures
    for (auto& tex : _reductionTextures)
    {
        if (tex)
        {
            _hgi->DestroyTexture(&tex);
        }
    }
    _reductionTextures.clear();

    // Reset cached dimensions
    _lastInputDimensions = GfVec3i(0, 0, 0);
}

void VisualizeAovCompute::_DestroyShaderProgram(HgiShaderProgramHandle& program)
{
    if (!program)
    {
        return;
    }
    for (HgiShaderFunctionHandle fn : program->GetShaderFunctions())
    {
        _hgi->DestroyShaderFunction(&fn);
    }
    _hgi->DestroyShaderProgram(&program);
}

bool VisualizeAovCompute::_CreateBufferResources()
{
    if (_vertexBuffer && _indexBuffer)
    {
        return true;
    }

    // A larger-than screen triangle made to fit the screen
    constexpr float vertData[][6] = {
        { -1, 3, 0, 1, 0, 2 }, { -1, -1, 0, 1, 0, 0 }, { 3, -1, 0, 1, 2, 0 }
    };

    HgiBufferDesc vboDesc;
    vboDesc.debugName    = "VisualizeAovDepthMinMax VertexBuffer";
    vboDesc.usage        = HgiBufferUsageVertex;
    vboDesc.initialData  = vertData;
    vboDesc.byteSize     = sizeof(vertData);
    vboDesc.vertexStride = sizeof(vertData[0]);
    _vertexBuffer        = _hgi->CreateBuffer(vboDesc);

    static const int32_t indices[3] = { 0, 1, 2 };

    HgiBufferDesc iboDesc;
    iboDesc.debugName   = "VisualizeAovDepthMinMax IndexBuffer";
    iboDesc.usage       = HgiBufferUsageIndex32;
    iboDesc.initialData = indices;
    iboDesc.byteSize    = sizeof(indices);
    _indexBuffer        = _hgi->CreateBuffer(iboDesc);

    return _vertexBuffer && _indexBuffer;
}

bool VisualizeAovCompute::_CreateFirstPassShaderProgram()
{
    if (_firstPassShaderProgram)
    {
        return true;
    }

    const HioGlslfx glslfx(_GetShaderPath(), HioGlslfxTokens->defVal);

    // Vertex shader
    HgiShaderFunctionDesc vertDesc;
    vertDesc.debugName   = _tokens->depthMinMaxVertex.GetString();
    vertDesc.shaderStage = HgiShaderStageVertex;
    HgiShaderFunctionAddStageInput(&vertDesc, "position", "vec4");
    HgiShaderFunctionAddStageInput(&vertDesc, "uvIn", "vec2");
    HgiShaderFunctionAddStageOutput(&vertDesc, "gl_Position", "vec4", "position");
    HgiShaderFunctionAddStageOutput(&vertDesc, "uvOut", "vec2");
    std::string vsCode   = glslfx.GetSource(_tokens->depthMinMaxVertex);
    vertDesc.shaderCode = vsCode.c_str();
    HgiShaderFunctionHandle vertFn = _hgi->CreateShaderFunction(vertDesc);

    // Fragment shader
    HgiShaderFunctionDesc fragDesc;
    fragDesc.debugName   = _tokens->depthMinMaxFragment.GetString();
    fragDesc.shaderStage = HgiShaderStageFragment;
    HgiShaderFunctionAddStageInput(&fragDesc, "uvOut", "vec2");
    HgiShaderFunctionAddTexture(&fragDesc, "depthIn", /*bindIndex=*/0, /*dimensions=*/2,
        HgiFormatFloat32);
    HgiShaderFunctionAddStageOutput(&fragDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddConstantParam(&fragDesc, "screenSize", "vec2");
    HgiShaderFunctionAddConstantParam(&fragDesc, "outputScreenSize", "vec2");
    std::string fsCode   = glslfx.GetSource(_tokens->depthMinMaxFragment);
    fragDesc.shaderCode = fsCode.c_str();
    HgiShaderFunctionHandle fragFn = _hgi->CreateShaderFunction(fragDesc);

    // Create shader program
    HgiShaderProgramDesc programDesc;
    programDesc.debugName = "VisualizeAovDepthMinMaxFirstPassProgram";
    programDesc.shaderFunctions.push_back(std::move(vertFn));
    programDesc.shaderFunctions.push_back(std::move(fragFn));
    _firstPassShaderProgram = _hgi->CreateShaderProgram(programDesc);

    if (!_firstPassShaderProgram->IsValid())
    {
        TF_CODING_ERROR("Failed to create depth min/max first pass shader");
        for (HgiShaderFunctionHandle fn : _firstPassShaderProgram->GetShaderFunctions())
        {
            std::cout << fn->GetCompileErrors() << std::endl;
        }
        std::cout << _firstPassShaderProgram->GetCompileErrors() << std::endl;
        return false;
    }

    return true;
}

bool VisualizeAovCompute::_CreateReductionShaderProgram()
{
    if (_reductionShaderProgram)
    {
        return true;
    }

    const HioGlslfx glslfx(_GetShaderPath(), HioGlslfxTokens->defVal);

    // Vertex shader (same as first pass)
    HgiShaderFunctionDesc vertDesc;
    vertDesc.debugName   = _tokens->depthMinMaxVertex.GetString();
    vertDesc.shaderStage = HgiShaderStageVertex;
    HgiShaderFunctionAddStageInput(&vertDesc, "position", "vec4");
    HgiShaderFunctionAddStageInput(&vertDesc, "uvIn", "vec2");
    HgiShaderFunctionAddStageOutput(&vertDesc, "gl_Position", "vec4", "position");
    HgiShaderFunctionAddStageOutput(&vertDesc, "uvOut", "vec2");
    std::string vsCode   = glslfx.GetSource(_tokens->depthMinMaxVertex);
    vertDesc.shaderCode = vsCode.c_str();
    HgiShaderFunctionHandle vertFn = _hgi->CreateShaderFunction(vertDesc);

    // Fragment shader for reduction
    HgiShaderFunctionDesc fragDesc;
    fragDesc.debugName   = _tokens->depthMinMaxReductionFragment.GetString();
    fragDesc.shaderStage = HgiShaderStageFragment;
    HgiShaderFunctionAddStageInput(&fragDesc, "uvOut", "vec2");
    HgiShaderFunctionAddTexture(&fragDesc, "minMaxIn", /*bindIndex=*/0, /*dimensions=*/2,
        HgiFormatFloat32Vec4);
    HgiShaderFunctionAddStageOutput(&fragDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddConstantParam(&fragDesc, "screenSize", "vec2");
    HgiShaderFunctionAddConstantParam(&fragDesc, "outputScreenSize", "vec2");
    std::string fsCode   = glslfx.GetSource(_tokens->depthMinMaxReductionFragment);
    fragDesc.shaderCode = fsCode.c_str();
    HgiShaderFunctionHandle fragFn = _hgi->CreateShaderFunction(fragDesc);

    // Create shader program
    HgiShaderProgramDesc programDesc;
    programDesc.debugName = "VisualizeAovDepthMinMaxReductionProgram";
    programDesc.shaderFunctions.push_back(std::move(vertFn));
    programDesc.shaderFunctions.push_back(std::move(fragFn));
    _reductionShaderProgram = _hgi->CreateShaderProgram(programDesc);

    if (!_reductionShaderProgram->IsValid())
    {
        TF_CODING_ERROR("Failed to create depth min/max reduction shader");
        for (HgiShaderFunctionHandle fn : _reductionShaderProgram->GetShaderFunctions())
        {
            std::cout << fn->GetCompileErrors() << std::endl;
        }
        std::cout << _reductionShaderProgram->GetCompileErrors() << std::endl;
        return false;
    }

    return true;
}

bool VisualizeAovCompute::_CreateFirstPassPipeline()
{
    if (_firstPassPipeline)
    {
        return true;
    }

    HgiGraphicsPipelineDesc desc;
    desc.debugName     = "VisualizeAovDepthMinMax FirstPass Pipeline";
    desc.shaderProgram = _firstPassShaderProgram;

    // Vertex attributes
    HgiVertexAttributeDesc posAttr;
    posAttr.format             = HgiFormatFloat32Vec3;
    posAttr.offset             = 0;
    posAttr.shaderBindLocation = 0;

    HgiVertexAttributeDesc uvAttr;
    uvAttr.format             = HgiFormatFloat32Vec2;
    uvAttr.offset             = sizeof(float) * 4;
    uvAttr.shaderBindLocation = 1;

    HgiVertexBufferDesc vboDesc;
    vboDesc.bindingIndex = 0;
    vboDesc.vertexStride = sizeof(float) * 6;
    vboDesc.vertexAttributes.push_back(posAttr);
    vboDesc.vertexAttributes.push_back(uvAttr);
    desc.vertexBuffers.push_back(std::move(vboDesc));

    // No depth test
    desc.depthState.depthTestEnabled  = false;
    desc.depthState.depthWriteEnabled = false;

    // Rasterization
    desc.rasterizationState.cullMode    = HgiCullModeBack;
    desc.rasterizationState.polygonMode = HgiPolygonModeFill;
    desc.rasterizationState.winding     = HgiWindingCounterClockwise;

    // Color attachment (RG for min/max)
    _attachmentDesc.blendEnabled = false;
    _attachmentDesc.loadOp       = HgiAttachmentLoadOpDontCare;
    _attachmentDesc.storeOp      = HgiAttachmentStoreOpStore;
    _attachmentDesc.format       = HgiFormatFloat32Vec4;
    _attachmentDesc.usage        = HgiTextureUsageBitsColorTarget | HgiTextureUsageBitsShaderRead;
    desc.colorAttachmentDescs.push_back(_attachmentDesc);

    desc.shaderConstantsDesc.stageUsage = HgiShaderStageFragment;
    desc.shaderConstantsDesc.byteSize   = sizeof(Uniforms);

    _firstPassPipeline = _hgi->CreateGraphicsPipeline(desc);

    return bool(_firstPassPipeline);
}

bool VisualizeAovCompute::_CreateReductionPipeline()
{
    if (_reductionPipeline)
    {
        return true;
    }

    HgiGraphicsPipelineDesc desc;
    desc.debugName     = "VisualizeAovDepthMinMax Reduction Pipeline";
    desc.shaderProgram = _reductionShaderProgram;

    // Vertex attributes
    HgiVertexAttributeDesc posAttr;
    posAttr.format             = HgiFormatFloat32Vec3;
    posAttr.offset             = 0;
    posAttr.shaderBindLocation = 0;

    HgiVertexAttributeDesc uvAttr;
    uvAttr.format             = HgiFormatFloat32Vec2;
    uvAttr.offset             = sizeof(float) * 4;
    uvAttr.shaderBindLocation = 1;

    HgiVertexBufferDesc vboDesc;
    vboDesc.bindingIndex = 0;
    vboDesc.vertexStride = sizeof(float) * 6;
    vboDesc.vertexAttributes.push_back(posAttr);
    vboDesc.vertexAttributes.push_back(uvAttr);
    desc.vertexBuffers.push_back(std::move(vboDesc));

    // No depth test
    desc.depthState.depthTestEnabled  = false;
    desc.depthState.depthWriteEnabled = false;

    // Rasterization
    desc.rasterizationState.cullMode    = HgiCullModeBack;
    desc.rasterizationState.polygonMode = HgiPolygonModeFill;
    desc.rasterizationState.winding     = HgiWindingCounterClockwise;

    // Color attachment
    desc.colorAttachmentDescs.push_back(_attachmentDesc);

    desc.shaderConstantsDesc.stageUsage = HgiShaderStageFragment;
    desc.shaderConstantsDesc.byteSize   = sizeof(Uniforms);

    _reductionPipeline = _hgi->CreateGraphicsPipeline(desc);

    return bool(_reductionPipeline);
}

HgiTextureHandle VisualizeAovCompute::_CreateReductionTexture(GfVec3i const& dimensions)
{
    HgiTextureDesc texDesc;
    texDesc.debugName   = "VisualizeAovDepthMinMax Reduction Texture";
    texDesc.dimensions  = dimensions;
    texDesc.format      = HgiFormatFloat32Vec4;
    texDesc.layerCount  = 1;
    texDesc.mipLevels   = 1;
    texDesc.sampleCount = HgiSampleCount1;
    texDesc.usage       = HgiTextureUsageBitsColorTarget | HgiTextureUsageBitsShaderRead;

    return _hgi->CreateTexture(texDesc);
}

bool VisualizeAovCompute::_CreateFirstPassResourceBindings(
    HgiTextureHandle const& depthTexture, HgiSamplerHandle const& sampler)
{
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "VisualizeAovDepthMinMax FirstPass Bindings";

    HgiTextureBindDesc texBind;
    texBind.bindingIndex = 0;
    texBind.stageUsage   = HgiShaderStageFragment;
    texBind.writable     = false;
    texBind.textures.push_back(depthTexture);
    texBind.samplers.push_back(sampler);
    resourceDesc.textures.push_back(std::move(texBind));

    if (_firstPassResourceBindings)
    {
        _hgi->DestroyResourceBindings(&_firstPassResourceBindings);
    }

    _firstPassResourceBindings = _hgi->CreateResourceBindings(resourceDesc);

    return bool(_firstPassResourceBindings);
}

bool VisualizeAovCompute::_CreateReductionResourceBindings(
    HgiTextureHandle const& inputTexture, HgiSamplerHandle const& sampler)
{
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "VisualizeAovDepthMinMax Reduction Bindings";

    HgiTextureBindDesc texBind;
    texBind.bindingIndex = 0;
    texBind.stageUsage   = HgiShaderStageFragment;
    texBind.writable     = false;
    texBind.textures.push_back(inputTexture);
    texBind.samplers.push_back(sampler);
    resourceDesc.textures.push_back(std::move(texBind));

    if (_reductionResourceBindings)
    {
        _hgi->DestroyResourceBindings(&_reductionResourceBindings);
    }

    _reductionResourceBindings = _hgi->CreateResourceBindings(resourceDesc);

    return bool(_reductionResourceBindings);
}

void VisualizeAovCompute::_ExecuteFirstPass(HgiTextureHandle const& /*depthTexture*/,
    HgiTextureHandle const& outputTexture, GfVec3i const& inputDims, GfVec3i const& outputDims)
{
    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.colorAttachmentDescs.push_back(_attachmentDesc);
    gfxDesc.colorTextures.push_back(outputTexture);

    Uniforms uniforms;
    uniforms.screenSize       = GfVec2f(static_cast<float>(inputDims[0]), static_cast<float>(inputDims[1]));
    uniforms.outputScreenSize = GfVec2f(static_cast<float>(outputDims[0]), static_cast<float>(outputDims[1]));

    HgiGraphicsCmdsUniquePtr gfxCmds = _hgi->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("VisualizeAovDepthMinMax FirstPass");
    gfxCmds->BindResources(_firstPassResourceBindings);
    gfxCmds->BindPipeline(_firstPassPipeline);
    gfxCmds->BindVertexBuffers({ { _vertexBuffer, 0, 0 } });
    gfxCmds->SetConstantValues(_firstPassPipeline, HgiShaderStageFragment, 0, sizeof(uniforms),
        &uniforms);
    gfxCmds->SetViewport(GfVec4i(0, 0, outputDims[0], outputDims[1]));
    gfxCmds->DrawIndexed(_indexBuffer, 3, 0, 0, 1, 0);
    gfxCmds->PopDebugGroup();

    _hgi->SubmitCmds(gfxCmds.get());
}

void VisualizeAovCompute::_ExecuteReductionPass(HgiTextureHandle const& /*inputTexture*/,
    HgiTextureHandle const& outputTexture, GfVec3i const& inputDims, GfVec3i const& outputDims)
{
    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.colorAttachmentDescs.push_back(_attachmentDesc);
    gfxDesc.colorTextures.push_back(outputTexture);

    Uniforms uniforms;
    uniforms.screenSize       = GfVec2f(static_cast<float>(inputDims[0]), static_cast<float>(inputDims[1]));
    uniforms.outputScreenSize = GfVec2f(static_cast<float>(outputDims[0]), static_cast<float>(outputDims[1]));

    HgiGraphicsCmdsUniquePtr gfxCmds = _hgi->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("VisualizeAovDepthMinMax Reduction");
    gfxCmds->BindResources(_reductionResourceBindings);
    gfxCmds->BindPipeline(_reductionPipeline);
    gfxCmds->BindVertexBuffers({ { _vertexBuffer, 0, 0 } });
    gfxCmds->SetConstantValues(_reductionPipeline, HgiShaderStageFragment, 0, sizeof(uniforms),
        &uniforms);
    gfxCmds->SetViewport(GfVec4i(0, 0, outputDims[0], outputDims[1]));
    gfxCmds->DrawIndexed(_indexBuffer, 3, 0, 0, 1, 0);
    gfxCmds->PopDebugGroup();

    _hgi->SubmitCmds(gfxCmds.get());
}

HgiTextureHandle VisualizeAovCompute::ComputeMinMaxDepth(HgiTextureHandle const& depthTexture,
    HgiSamplerHandle const& sampler)
{
    const HgiTextureDesc& textureDesc = depthTexture.Get()->GetDescriptor();
    if (textureDesc.format != HgiFormatFloat32)
    {
        TF_WARN("Non-floating point depth AOVs aren't supported yet.");
        return HgiTextureHandle();
    }

    // Create resources if needed
    if (!TF_VERIFY(_CreateBufferResources(), "Failed to create buffer resources"))
    {
        return HgiTextureHandle();
    }
    if (!TF_VERIFY(_CreateFirstPassShaderProgram(), "Failed to create first pass shader"))
    {
        return HgiTextureHandle();
    }
    if (!TF_VERIFY(_CreateReductionShaderProgram(), "Failed to create reduction shader"))
    {
        return HgiTextureHandle();
    }
    if (!TF_VERIFY(_CreateFirstPassPipeline(), "Failed to create first pass pipeline"))
    {
        return HgiTextureHandle();
    }
    if (!TF_VERIFY(_CreateReductionPipeline(), "Failed to create reduction pipeline"))
    {
        return HgiTextureHandle();
    }

    // Create sampler for reduction passes if needed
    if (!_reductionSampler)
    {
        HgiSamplerDesc sampDesc;
        sampDesc.magFilter    = HgiSamplerFilterNearest;
        sampDesc.minFilter    = HgiSamplerFilterNearest;
        sampDesc.addressModeU = HgiSamplerAddressModeClampToEdge;
        sampDesc.addressModeV = HgiSamplerAddressModeClampToEdge;
        _reductionSampler     = _hgi->CreateSampler(sampDesc);
    }

    // Check if dimensions changed - only recreate textures if needed
    const bool dimensionsChanged = (textureDesc.dimensions != _lastInputDimensions);

    if (dimensionsChanged)
    {
        // Clean up old reduction textures
        for (auto& tex : _reductionTextures)
        {
            if (tex)
            {
                _hgi->DestroyTexture(&tex);
            }
        }
        _reductionTextures.clear();

        _lastInputDimensions = textureDesc.dimensions;

        // Pre-calculate all reduction texture sizes and create them
        // Reduce all the way to 1x1 for direct GPU sampling (no CPU readback!)
        int width  = textureDesc.dimensions[0];
        int height = textureDesc.dimensions[1];

        while (width > kFinalTextureSize || height > kFinalTextureSize)
        {
            width  = std::max(1, (width + kReductionFactor - 1) / kReductionFactor);
            height = std::max(1, (height + kReductionFactor - 1) / kReductionFactor);

            GfVec3i dims(width, height, 1);
            HgiTextureHandle tex = _CreateReductionTexture(dims);
            _reductionTextures.push_back(tex);
        }

        // Edge case: if input is already 1x1, we still need one output texture
        // for the first pass to write min/max values
        if (_reductionTextures.empty())
        {
            GfVec3i dims(kFinalTextureSize, kFinalTextureSize, 1);
            HgiTextureHandle tex = _CreateReductionTexture(dims);
            _reductionTextures.push_back(tex);
        }
    }

    // Execute first pass (depth texture -> first reduction texture)
    if (!_CreateFirstPassResourceBindings(depthTexture, sampler))
    {
        return HgiTextureHandle();
    }
    GfVec3i firstOutputDims = _reductionTextures[0]->GetDescriptor().dimensions;
    _ExecuteFirstPass(depthTexture, _reductionTextures[0], textureDesc.dimensions, firstOutputDims);

    // Continue reduction using pre-created textures until we reach 1x1
    for (size_t i = 0; i + 1 < _reductionTextures.size(); i++)
    {
        HgiTextureHandle currentTex = _reductionTextures[i];
        HgiTextureHandle nextTex    = _reductionTextures[i + 1];

        GfVec3i currentDims = currentTex->GetDescriptor().dimensions;
        GfVec3i nextDims    = nextTex->GetDescriptor().dimensions;

        if (!_CreateReductionResourceBindings(currentTex, _reductionSampler))
        {
            return HgiTextureHandle();
        }
        _ExecuteReductionPass(currentTex, nextTex, currentDims, nextDims);
    }

    // Return the final 1x1 texture - no CPU readback needed!
    // The visualization shader can sample this directly at (0,0) to get min/max.
    return _reductionTextures.back();
}

} // namespace HVT_NS
