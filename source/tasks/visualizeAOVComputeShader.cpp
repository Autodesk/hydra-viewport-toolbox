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

#include "tasks/visualizeAOVComputeShader.h"

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
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
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
    static TfToken shader { GetShaderPath("depthMinMax.glslfx").generic_u8string() };
    return shader;
}

// Reduction factor per pass (each output pixel covers NxN input pixels)
constexpr int kReductionFactor = 8;

// Minimum texture size before CPU readback
constexpr int kMinTextureSize = 4;

// Token definitions
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((depthMinMaxVertex, "DepthMinMaxVertex"))
    ((depthMinMaxFragment, "DepthMinMaxFragment"))
    ((depthMinMaxReductionFragment, "DepthMinMaxReductionFragment")));

// Uniforms for the shaders
struct Uniforms
{
    GfVec2f screenSize;       // Input texture size
    GfVec2f outputScreenSize; // Output texture size
};

} // namespace

VisualizeAOVComputeShader::VisualizeAOVComputeShader(Hgi* hgi) : _hgi(hgi) {}

VisualizeAOVComputeShader::~VisualizeAOVComputeShader()
{
    _DestroyResources();
}

void VisualizeAOVComputeShader::_DestroyResources()
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
}

void VisualizeAOVComputeShader::_DestroyShaderProgram(HgiShaderProgramHandle& program)
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

bool VisualizeAOVComputeShader::_CreateBufferResources()
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
    vboDesc.debugName    = "DepthMinMax VertexBuffer";
    vboDesc.usage        = HgiBufferUsageVertex;
    vboDesc.initialData  = vertData;
    vboDesc.byteSize     = sizeof(vertData);
    vboDesc.vertexStride = sizeof(vertData[0]);
    _vertexBuffer        = _hgi->CreateBuffer(vboDesc);

    static const int32_t indices[3] = { 0, 1, 2 };

    HgiBufferDesc iboDesc;
    iboDesc.debugName   = "DepthMinMax IndexBuffer";
    iboDesc.usage       = HgiBufferUsageIndex32;
    iboDesc.initialData = indices;
    iboDesc.byteSize    = sizeof(indices);
    _indexBuffer        = _hgi->CreateBuffer(iboDesc);

    return _vertexBuffer && _indexBuffer;
}

bool VisualizeAOVComputeShader::_CreateFirstPassShaderProgram()
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
    programDesc.debugName = "DepthMinMaxFirstPassProgram";
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

bool VisualizeAOVComputeShader::_CreateReductionShaderProgram()
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
    programDesc.debugName = "DepthMinMaxReductionProgram";
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

bool VisualizeAOVComputeShader::_CreateFirstPassPipeline()
{
    if (_firstPassPipeline)
    {
        return true;
    }

    HgiGraphicsPipelineDesc desc;
    desc.debugName     = "DepthMinMax FirstPass Pipeline";
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

bool VisualizeAOVComputeShader::_CreateReductionPipeline()
{
    if (_reductionPipeline)
    {
        return true;
    }

    HgiGraphicsPipelineDesc desc;
    desc.debugName     = "DepthMinMax Reduction Pipeline";
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

HgiTextureHandle VisualizeAOVComputeShader::_CreateReductionTexture(GfVec3i const& dimensions)
{
    HgiTextureDesc texDesc;
    texDesc.debugName   = "DepthMinMax Reduction Texture";
    texDesc.dimensions  = dimensions;
    texDesc.format      = HgiFormatFloat32Vec4;
    texDesc.layerCount  = 1;
    texDesc.mipLevels   = 1;
    texDesc.sampleCount = HgiSampleCount1;
    texDesc.usage       = HgiTextureUsageBitsColorTarget | HgiTextureUsageBitsShaderRead;

    return _hgi->CreateTexture(texDesc);
}

bool VisualizeAOVComputeShader::_CreateFirstPassResourceBindings(
    HgiTextureHandle const& depthTexture, HgiSamplerHandle const& sampler)
{
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "DepthMinMax FirstPass Bindings";

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

bool VisualizeAOVComputeShader::_CreateReductionResourceBindings(
    HgiTextureHandle const& inputTexture, HgiSamplerHandle const& sampler)
{
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "DepthMinMax Reduction Bindings";

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

void VisualizeAOVComputeShader::_ExecuteFirstPass(HgiTextureHandle const& /*depthTexture*/,
    HgiTextureHandle const& outputTexture, GfVec3i const& inputDims, GfVec3i const& outputDims)
{
    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.colorAttachmentDescs.push_back(_attachmentDesc);
    gfxDesc.colorTextures.push_back(outputTexture);

    Uniforms uniforms;
    uniforms.screenSize       = GfVec2f(inputDims[0], inputDims[1]);
    uniforms.outputScreenSize = GfVec2f(outputDims[0], outputDims[1]);

    HgiGraphicsCmdsUniquePtr gfxCmds = _hgi->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("DepthMinMax FirstPass");
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

void VisualizeAOVComputeShader::_ExecuteReductionPass(HgiTextureHandle const& /*inputTexture*/,
    HgiTextureHandle const& outputTexture, GfVec3i const& inputDims, GfVec3i const& outputDims)
{
    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.colorAttachmentDescs.push_back(_attachmentDesc);
    gfxDesc.colorTextures.push_back(outputTexture);

    Uniforms uniforms;
    uniforms.screenSize       = GfVec2f(inputDims[0], inputDims[1]);
    uniforms.outputScreenSize = GfVec2f(outputDims[0], outputDims[1]);

    HgiGraphicsCmdsUniquePtr gfxCmds = _hgi->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("DepthMinMax Reduction");
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

GfVec2f VisualizeAOVComputeShader::ComputeMinMaxDepth(HgiTextureHandle const& depthTexture,
    HgiSamplerHandle const& sampler)
{
    const HgiTextureDesc& textureDesc = depthTexture.Get()->GetDescriptor();
    if (textureDesc.format != HgiFormatFloat32)
    {
        TF_WARN("Non-floating point depth AOVs aren't supported yet.");
        return GfVec2f(0.0f, 1.0f);
    }

    // Create resources if needed
    if (!TF_VERIFY(_CreateBufferResources(), "Failed to create buffer resources"))
    {
        return GfVec2f(0.0f, 1.0f);
    }
    if (!TF_VERIFY(_CreateFirstPassShaderProgram(), "Failed to create first pass shader"))
    {
        return GfVec2f(0.0f, 1.0f);
    }
    if (!TF_VERIFY(_CreateReductionShaderProgram(), "Failed to create reduction shader"))
    {
        return GfVec2f(0.0f, 1.0f);
    }
    if (!TF_VERIFY(_CreateFirstPassPipeline(), "Failed to create first pass pipeline"))
    {
        return GfVec2f(0.0f, 1.0f);
    }
    if (!TF_VERIFY(_CreateReductionPipeline(), "Failed to create reduction pipeline"))
    {
        return GfVec2f(0.0f, 1.0f);
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

    // Clean up old reduction textures
    for (auto& tex : _reductionTextures)
    {
        if (tex)
        {
            _hgi->DestroyTexture(&tex);
        }
    }
    _reductionTextures.clear();

    // Calculate reduction sizes
    const int inputWidth  = textureDesc.dimensions[0];
    const int inputHeight = textureDesc.dimensions[1];

    // First pass output size
    int outWidth  = std::max(1, (inputWidth + kReductionFactor - 1) / kReductionFactor);
    int outHeight = std::max(1, (inputHeight + kReductionFactor - 1) / kReductionFactor);

    // Create first output texture
    GfVec3i outputDims(outWidth, outHeight, 1);
    HgiTextureHandle outputTex = _CreateReductionTexture(outputDims);
    _reductionTextures.push_back(outputTex);

    // Execute first pass
    if (!_CreateFirstPassResourceBindings(depthTexture, sampler))
    {
        return GfVec2f(0.0f, 1.0f);
    }
    _ExecuteFirstPass(depthTexture, outputTex, textureDesc.dimensions, outputDims);

    // Continue reduction until small enough
    HgiTextureHandle currentTex = outputTex;
    GfVec3i currentDims         = outputDims;

    while (currentDims[0] > kMinTextureSize || currentDims[1] > kMinTextureSize)
    {
        int newWidth  = std::max(1, (currentDims[0] + kReductionFactor - 1) / kReductionFactor);
        int newHeight = std::max(1, (currentDims[1] + kReductionFactor - 1) / kReductionFactor);
        GfVec3i newDims(newWidth, newHeight, 1);

        HgiTextureHandle newTex = _CreateReductionTexture(newDims);
        _reductionTextures.push_back(newTex);

        if (!_CreateReductionResourceBindings(currentTex, _reductionSampler))
        {
            return GfVec2f(0.0f, 1.0f);
        }
        _ExecuteReductionPass(currentTex, newTex, currentDims, newDims);

        currentTex  = newTex;
        currentDims = newDims;
    }

    // Read back the small texture
    size_t readbackSize = currentDims[0] * currentDims[1] * 4 * sizeof(float);
    std::vector<float> readbackData(currentDims[0] * currentDims[1] * 4);

    HgiBlitCmdsUniquePtr blitCmds = _hgi->CreateBlitCmds();
    HgiTextureGpuToCpuOp readOp;
    readOp.gpuSourceTexture      = currentTex;
    readOp.sourceTexelOffset     = GfVec3i(0, 0, 0);
    readOp.mipLevel              = 0;
    readOp.cpuDestinationBuffer  = readbackData.data();
    readOp.destinationByteOffset = 0;
    readOp.destinationBufferByteSize = readbackSize;
    blitCmds->CopyTextureGpuToCpu(readOp);
    _hgi->SubmitCmds(blitCmds.get(), HgiSubmitWaitTypeWaitUntilCompleted);

    // Find min/max from the readback data
    float minDepth = 1.0f;
    float maxDepth = 0.0f;

    for (int i = 0; i < currentDims[0] * currentDims[1]; i++)
    {
        float localMin = readbackData[i * 4 + 0]; // R channel = min
        float localMax = readbackData[i * 4 + 1]; // G channel = max

        minDepth = std::min(minDepth, localMin);
        maxDepth = std::max(maxDepth, localMax);
    }

    return GfVec2f(minDepth, maxDepth);
}

} // namespace HVT_NS
