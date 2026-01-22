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

#include "visualizeAovCompute.h"
#include <hvt/tasks/resources.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/computeCmds.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hgi/types.h>
#include <pxr/imaging/hio/glslfx.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

#include <algorithm>
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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    // shader mixins
    ((depthMinMaxTexToBuffer, "DepthMinMaxTexToBuffer"))
    ((depthMinMaxBufferToBuffer, "DepthMinMaxBufferToBuffer"))
    ((depthMinMaxBufferToTex, "DepthMinMaxBufferToTex"))

    // texture/buffer names
    (depthIn)
    (minMaxBuffer)
    (minMaxBufferIn)
    (minMaxBufferOut)
    (minMaxTexOut)

    // uniform names
    (inputWidth)
    (inputHeight)
    (outputWidth)
    (outputHeight)
    (tileSize));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

} // namespace

VisualizeAovCompute::VisualizeAovCompute(Hgi* hgi) : _hgi(hgi)
{
}

VisualizeAovCompute::~VisualizeAovCompute()
{
    _DestroyResources();
}

bool VisualizeAovCompute::IsValid() const
{
    return _shaderProgramTexToBuffer && _shaderProgramBufferToBuffer && 
           _shaderProgramBufferToTex && _resultTexture;
}

void VisualizeAovCompute::_DestroyShaderPrograms()
{
    auto destroyProgram = [this](HgiShaderProgramHandle& program) {
        if (program)
        {
            for (HgiShaderFunctionHandle fn : program->GetShaderFunctions())
            {
                _hgi->DestroyShaderFunction(&fn);
            }
            _hgi->DestroyShaderProgram(&program);
        }
    };

    destroyProgram(_shaderProgramTexToBuffer);
    destroyProgram(_shaderProgramBufferToBuffer);
    destroyProgram(_shaderProgramBufferToTex);
}

void VisualizeAovCompute::_DestroyResources()
{
    _DestroyShaderPrograms();

    if (_pipelineTexToBuffer)
    {
        _hgi->DestroyComputePipeline(&_pipelineTexToBuffer);
    }
    if (_pipelineBufferToBuffer)
    {
        _hgi->DestroyComputePipeline(&_pipelineBufferToBuffer);
    }
    if (_pipelineBufferToTex)
    {
        _hgi->DestroyComputePipeline(&_pipelineBufferToTex);
    }
    if (_resourceBindings)
    {
        _hgi->DestroyResourceBindings(&_resourceBindings);
    }
    if (_buffer[0])
    {
        _hgi->DestroyBuffer(&_buffer[0]);
    }
    if (_buffer[1])
    {
        _hgi->DestroyBuffer(&_buffer[1]);
    }
    if (_resultTexture)
    {
        _hgi->DestroyTexture(&_resultTexture);
    }
    if (_sampler)
    {
        _hgi->DestroySampler(&_sampler);
    }
}

bool VisualizeAovCompute::_CreateShaderPrograms()
{
    if (_shaderProgramTexToBuffer && _shaderProgramBufferToBuffer && _shaderProgramBufferToTex)
    {
        return true;
    }

    const HioGlslfx glslfx(_GetShaderPath(), HioGlslfxTokens->defVal);
    if (!glslfx.IsValid())
    {
        TF_CODING_ERROR("Failed to load glslfx: %s", _GetShaderPath().GetText());
        return false;
    }

    // Helper to create a compute shader program
    auto createProgram = [this, &glslfx](
        const TfToken& mixin,
        HgiShaderProgramHandle& outProgram,
        bool hasInputTexture,
        bool hasInputBuffer,
        bool hasOutputBuffer,
        bool hasOutputTexture) -> bool
    {
        if (outProgram) return true;

        HgiShaderFunctionDesc computeDesc;
        computeDesc.debugName = mixin.GetString();
        computeDesc.shaderStage = HgiShaderStageCompute;
        // Required for Metal: specify local workgroup size
        computeDesc.computeDescriptor.localSize = GfVec3i(1, 1, 1);

        // Input texture (first pass only)
        if (hasInputTexture)
        {
            // Declare as depth texture for proper shader code generation.
#if defined(ADSK_OPENUSD_PENDING)
            // HgiShaderTextureTypeDepth generates proper depth2d<float> on Metal.
            HgiShaderFunctionAddTexture(&computeDesc, _tokens->depthIn.GetString(),
                /*bindIndex=*/0, /*dimensions=*/2, HgiFormatFloat32, HgiShaderTextureTypeDepth);
#else
            HgiShaderFunctionAddTexture(&computeDesc, _tokens->depthIn.GetString(),
                /*bindIndex=*/0, /*dimensions=*/2, HgiFormatFloat32);
#endif
        }

        // Input buffer (subsequent passes)
        if (hasInputBuffer)
        {
            HgiShaderFunctionAddBuffer(&computeDesc, _tokens->minMaxBufferIn.GetString(),
                HdStTokens->_float, /*bindIndex=*/0, HgiBindingTypePointer);
        }

        // Output buffer
        if (hasOutputBuffer)
        {
            HgiShaderFunctionAddWritableBuffer(&computeDesc, _tokens->minMaxBufferOut.GetString(),
                HdStTokens->_float, /*bindIndex=*/1);
        }

        // Output texture (last pass only)
        if (hasOutputTexture)
        {
            HgiShaderFunctionTextureDesc texDesc;
            texDesc.nameInShader = _tokens->minMaxTexOut.GetString();
            texDesc.bindIndex = 1;
            texDesc.dimensions = 2;
            texDesc.format = HgiFormatFloat32Vec2;
            texDesc.writable = true;
            computeDesc.textures.push_back(texDesc);
        }

        // Uniform parameters
        HgiShaderFunctionAddConstantParam(&computeDesc, _tokens->inputWidth.GetString(),
            HdStTokens->_int);
        HgiShaderFunctionAddConstantParam(&computeDesc, _tokens->inputHeight.GetString(),
            HdStTokens->_int);
        HgiShaderFunctionAddConstantParam(&computeDesc, _tokens->outputWidth.GetString(),
            HdStTokens->_int);
        HgiShaderFunctionAddConstantParam(&computeDesc, _tokens->outputHeight.GetString(),
            HdStTokens->_int);
        HgiShaderFunctionAddConstantParam(&computeDesc, _tokens->tileSize.GetString(),
            HdStTokens->_int);

        // Global invocation ID
        HgiShaderFunctionAddStageInput(&computeDesc, "hd_GlobalInvocationID", "uvec3",
            HgiShaderKeywordTokens->hdGlobalInvocationID);

        std::string csCode = glslfx.GetSource(mixin);
        if (csCode.empty())
        {
            TF_CODING_ERROR("Failed to get shader source for: %s", mixin.GetText());
            return false;
        }
        computeDesc.shaderCode = csCode.c_str();

        // Allow capturing the generated code for debugging
        std::string generatedCode;
        computeDesc.generatedShaderCodeOut = &generatedCode;

        HgiShaderFunctionHandle computeFn = _hgi->CreateShaderFunction(computeDesc);
        if (!computeFn)
        {
            TF_CODING_ERROR("Failed to create shader function for: %s\nGenerated code:\n%s", 
                mixin.GetText(), generatedCode.c_str());
            return false;
        }
        if (!computeFn->IsValid())
        {
            TF_CODING_ERROR("Shader function is invalid for: %s\nCompile errors: %s\nGenerated code:\n%s", 
                mixin.GetText(), computeFn->GetCompileErrors().c_str(), generatedCode.c_str());
            _hgi->DestroyShaderFunction(&computeFn);
            return false;
        }

        HgiShaderProgramDesc programDesc;
        programDesc.debugName = mixin.GetString();
        programDesc.shaderFunctions.push_back(std::move(computeFn));
        outProgram = _hgi->CreateShaderProgram(programDesc);

        if (!outProgram || !outProgram->IsValid())
        {
            TF_CODING_ERROR("Failed to create visualize AOV compute shader: %s", mixin.GetText());
            if (outProgram)
            {
                for (HgiShaderFunctionHandle fn : outProgram->GetShaderFunctions())
                {
                    std::cout << fn->GetCompileErrors() << std::endl;
                }
                std::cout << outProgram->GetCompileErrors() << std::endl;
            }
            return false;
        }

        return true;
    };

    // Create all three shader variants
    if (!createProgram(_tokens->depthMinMaxTexToBuffer, _shaderProgramTexToBuffer,
            /*hasInputTexture=*/true, /*hasInputBuffer=*/false,
            /*hasOutputBuffer=*/true, /*hasOutputTexture=*/false))
    {
        _DestroyShaderPrograms();
        return false;
    }

    if (!createProgram(_tokens->depthMinMaxBufferToBuffer, _shaderProgramBufferToBuffer,
            /*hasInputTexture=*/false, /*hasInputBuffer=*/true,
            /*hasOutputBuffer=*/true, /*hasOutputTexture=*/false))
    {
        _DestroyShaderPrograms();
        return false;
    }

    if (!createProgram(_tokens->depthMinMaxBufferToTex, _shaderProgramBufferToTex,
            /*hasInputTexture=*/false, /*hasInputBuffer=*/true,
            /*hasOutputBuffer=*/false, /*hasOutputTexture=*/true))
    {
        _DestroyShaderPrograms();
        return false;
    }

    return true;
}

bool VisualizeAovCompute::_CreateBuffers(int inputWidth, int inputHeight)
{
    // Calculate the maximum buffer size needed for the reduction chain
    int pass1Width = (inputWidth + TILE_SIZE - 1) / TILE_SIZE;
    int pass1Height = (inputHeight + TILE_SIZE - 1) / TILE_SIZE;
    int maxElements = pass1Width * pass1Height * 2; // *2 for min and max

    if (_buffer[0] && _bufferSize >= maxElements)
    {
        return true;
    }

    // Destroy existing buffers
    if (_buffer[0])
    {
        _hgi->DestroyBuffer(&_buffer[0]);
    }
    if (_buffer[1])
    {
        _hgi->DestroyBuffer(&_buffer[1]);
    }

    _bufferSize = maxElements;

    HgiBufferDesc bufferDesc;
    bufferDesc.usage = HgiBufferUsageStorage;
    bufferDesc.byteSize = maxElements * sizeof(float);

    bufferDesc.debugName = "VisualizeAov Buffer 0";
    _buffer[0] = _hgi->CreateBuffer(bufferDesc);

    bufferDesc.debugName = "VisualizeAov Buffer 1";
    _buffer[1] = _hgi->CreateBuffer(bufferDesc);

    return _buffer[0] && _buffer[1];
}

bool VisualizeAovCompute::_CreatePipelines()
{
    auto createPipeline = [this](
        HgiComputePipelineHandle& pipeline,
        HgiShaderProgramHandle const& program,
        const char* debugName) -> bool
    {
        if (pipeline) return true;

        HgiComputePipelineDesc desc;
        desc.debugName = debugName;
        desc.shaderProgram = program;
        desc.shaderConstantsDesc.byteSize = 5 * sizeof(int);

        pipeline = _hgi->CreateComputePipeline(desc);
        return bool(pipeline);
    };

    if (!createPipeline(_pipelineTexToBuffer, _shaderProgramTexToBuffer,
            "VisualizeAov Pipeline (Tex->Buffer)"))
        return false;

    if (!createPipeline(_pipelineBufferToBuffer, _shaderProgramBufferToBuffer,
            "VisualizeAov Pipeline (Buffer->Buffer)"))
        return false;

    if (!createPipeline(_pipelineBufferToTex, _shaderProgramBufferToTex,
            "VisualizeAov Pipeline (Buffer->Tex)"))
        return false;

    return true;
}

bool VisualizeAovCompute::_CreateResultTexture()
{
    if (_resultTexture && _sampler)
    {
        return true;
    }

    if (!_resultTexture)
    {
        HgiTextureDesc texDesc;
        texDesc.debugName = "VisualizeAov Result";
        texDesc.dimensions = GfVec3i(1, 1, 1);
        texDesc.format = HgiFormatFloat32Vec2;
        texDesc.layerCount = 1;
        texDesc.mipLevels = 1;
        texDesc.sampleCount = HgiSampleCount1;
        texDesc.usage = HgiTextureUsageBitsShaderRead | HgiTextureUsageBitsShaderWrite;

        _resultTexture = _hgi->CreateTexture(texDesc);
        if (!_resultTexture)
        {
            return false;
        }
    }

    if (!_sampler)
    {
        HgiSamplerDesc samplerDesc;
        samplerDesc.debugName = "VisualizeAov Sampler";
        samplerDesc.magFilter = HgiSamplerFilterNearest;
        samplerDesc.minFilter = HgiSamplerFilterNearest;
        samplerDesc.addressModeU = HgiSamplerAddressModeClampToEdge;
        samplerDesc.addressModeV = HgiSamplerAddressModeClampToEdge;

        _sampler = _hgi->CreateSampler(samplerDesc);
        if (!_sampler)
        {
            return false;
        }
    }

    return true;
}

bool VisualizeAovCompute::_CreateResourceBindings(
    HgiTextureHandle const& depthTexture,
    bool firstPass,
    bool lastPass)
{
    if (_resourceBindings)
    {
        _hgi->DestroyResourceBindings(&_resourceBindings);
    }

    HgiResourceBindingsDesc resourceDesc;

    if (firstPass)
    {
        resourceDesc.debugName = "VisualizeAov Bindings (Tex->Buffer)";

        // Input: depth texture (declared as HgiShaderTextureTypeDepth in shader)
        HgiTextureBindDesc texBind;
        texBind.bindingIndex = 0;
        texBind.stageUsage = HgiShaderStageCompute;
        texBind.writable = false;
        texBind.textures.push_back(depthTexture);
        texBind.samplers.push_back(_sampler);
        resourceDesc.textures.push_back(std::move(texBind));

        // Output: buffer[0]
        HgiBufferBindDesc bufBind;
        bufBind.bindingIndex = 1;
        bufBind.resourceType = HgiBindResourceTypeStorageBuffer;
        bufBind.stageUsage = HgiShaderStageCompute;
        bufBind.writable = true;
        bufBind.offsets.push_back(0);
        bufBind.buffers.push_back(_buffer[0]);
        resourceDesc.buffers.push_back(std::move(bufBind));
    }
    else if (lastPass)
    {
        resourceDesc.debugName = "VisualizeAov Bindings (Buffer->Tex)";

        // Input: buffer[0]
        HgiBufferBindDesc bufBindIn;
        bufBindIn.bindingIndex = 0;
        bufBindIn.resourceType = HgiBindResourceTypeStorageBuffer;
        bufBindIn.stageUsage = HgiShaderStageCompute;
        bufBindIn.writable = false;
        bufBindIn.offsets.push_back(0);
        bufBindIn.buffers.push_back(_buffer[0]);
        resourceDesc.buffers.push_back(std::move(bufBindIn));

        // Output: result texture (as storage image)
        HgiTextureBindDesc texBind;
        texBind.bindingIndex = 1;
        texBind.stageUsage = HgiShaderStageCompute;
        texBind.writable = true;
        texBind.resourceType = HgiBindResourceTypeStorageImage;
        texBind.textures.push_back(_resultTexture);
        texBind.samplers.push_back(_sampler);
        resourceDesc.textures.push_back(std::move(texBind));
    }
    else
    {
        resourceDesc.debugName = "VisualizeAov Bindings (Buffer->Buffer)";

        // Input: buffer[0]
        HgiBufferBindDesc bufBindIn;
        bufBindIn.bindingIndex = 0;
        bufBindIn.resourceType = HgiBindResourceTypeStorageBuffer;
        bufBindIn.stageUsage = HgiShaderStageCompute;
        bufBindIn.writable = false;
        bufBindIn.offsets.push_back(0);
        bufBindIn.buffers.push_back(_buffer[0]);
        resourceDesc.buffers.push_back(std::move(bufBindIn));

        // Output: buffer[1]
        HgiBufferBindDesc bufBindOut;
        bufBindOut.bindingIndex = 1;
        bufBindOut.resourceType = HgiBindResourceTypeStorageBuffer;
        bufBindOut.stageUsage = HgiShaderStageCompute;
        bufBindOut.writable = true;
        bufBindOut.offsets.push_back(0);
        bufBindOut.buffers.push_back(_buffer[1]);
        resourceDesc.buffers.push_back(std::move(bufBindOut));
    }

    _resourceBindings = _hgi->CreateResourceBindings(resourceDesc);

    return bool(_resourceBindings);
}

void VisualizeAovCompute::_ExecutePass(
    HgiComputeCmds* computeCmds,
    int inputWidth, int inputHeight,
    int outputWidth, int outputHeight,
    bool firstPass, bool lastPass)
{
    struct Uniforms
    {
        int inputWidth;
        int inputHeight;
        int outputWidth;
        int outputHeight;
        int tileSize;
    };

    Uniforms uniforms;
    uniforms.inputWidth = inputWidth;
    uniforms.inputHeight = inputHeight;
    uniforms.outputWidth = outputWidth;
    uniforms.outputHeight = outputHeight;
    uniforms.tileSize = TILE_SIZE;

    const char* debugLabel = firstPass ? "VisualizeAov (Tex->Buffer)" :
                             lastPass ? "VisualizeAov (Buffer->Tex)" :
                             "VisualizeAov (Buffer->Buffer)";
    computeCmds->PushDebugGroup(debugLabel);

    HgiComputePipelineHandle pipeline = firstPass ? _pipelineTexToBuffer :
                                        lastPass ? _pipelineBufferToTex :
                                        _pipelineBufferToBuffer;

    computeCmds->BindPipeline(pipeline);
    computeCmds->BindResources(_resourceBindings);
    computeCmds->SetConstantValues(pipeline, 0, sizeof(uniforms), &uniforms);
    computeCmds->Dispatch(outputWidth, outputHeight);

    // Insert memory barrier between compute dispatches to ensure proper ordering.
    // This ensures writes from this dispatch are visible to the next one.
    computeCmds->InsertMemoryBarrier(HgiMemoryBarrierAll);

    computeCmds->PopDebugGroup();
}

bool VisualizeAovCompute::Compute(HgiTextureHandle const& depthTexture)
{
    const HgiTextureDesc& textureDesc = depthTexture.Get()->GetDescriptor();
    
    // Accept any float format for depth textures (Float32, Float16, or depth-stencil formats
    // like Float32UInt8 which are used by some graphics backends).
    if (!HgiIsFloatFormat(textureDesc.format))
    {
        TF_WARN("VisualizeAovCompute: Non-floating point depth textures aren't supported yet. "
                "Format: %d", static_cast<int>(textureDesc.format));
        return false;
    }

    const int width = textureDesc.dimensions[0];
    const int height = textureDesc.dimensions[1];

    // Create resources if needed
    if (!_CreateShaderPrograms())
    {
        TF_WARN("VisualizeAovCompute: Failed to create shader programs");
        return false;
    }

    if (!_CreateBuffers(width, height))
    {
        TF_WARN("VisualizeAovCompute: Failed to create buffers");
        return false;
    }

    if (!_CreatePipelines())
    {
        TF_WARN("VisualizeAovCompute: Failed to create pipelines");
        return false;
    }

    if (!_CreateResultTexture())
    {
        TF_WARN("VisualizeAovCompute: Failed to create result texture");
        return false;
    }

    // Calculate reduction chain
    // We need to know upfront how many passes we need to determine the last pass
    std::vector<std::pair<int, int>> passDimensions;
    int currentWidth = width;
    int currentHeight = height;

    while (currentWidth > 1 || currentHeight > 1)
    {
        int outputWidth = std::max(1, (currentWidth + TILE_SIZE - 1) / TILE_SIZE);
        int outputHeight = std::max(1, (currentHeight + TILE_SIZE - 1) / TILE_SIZE);
        passDimensions.push_back({currentWidth, currentHeight});
        currentWidth = outputWidth;
        currentHeight = outputHeight;
    }
    // Add final 1x1 pass
    passDimensions.push_back({currentWidth, currentHeight});

    // Create a single command buffer for all reduction passes.
    // This ensures proper GPU command ordering and allows memory barriers
    // between dispatches to work correctly.
    HgiComputeCmdsDesc computeCmdsDesc;
    HgiComputeCmdsUniquePtr computeCmds = _hgi->CreateComputeCmds(computeCmdsDesc);

    computeCmds->PushDebugGroup("VisualizeAov MinMax Reduction");

    // Execute reduction passes
    for (size_t passIdx = 0; passIdx < passDimensions.size() - 1; ++passIdx)
    {
        int inW = passDimensions[passIdx].first;
        int inH = passDimensions[passIdx].second;
        int outW = std::max(1, (inW + TILE_SIZE - 1) / TILE_SIZE);
        int outH = std::max(1, (inH + TILE_SIZE - 1) / TILE_SIZE);

        bool isFirstPass = (passIdx == 0);
        bool isLastPass = (outW == 1 && outH == 1);

        // Create resource bindings for this pass
        if (!_CreateResourceBindings(depthTexture, isFirstPass, isLastPass))
        {
            TF_WARN("VisualizeAovCompute: Failed to create resource bindings for pass %zu", passIdx);
            computeCmds->PopDebugGroup();
            return false;
        }

        _ExecutePass(computeCmds.get(), inW, inH, outW, outH, isFirstPass, isLastPass);

        // Swap buffers for next iteration (ping-pong) unless this was the last pass
        if (!isLastPass && !isFirstPass)
        {
            std::swap(_buffer[0], _buffer[1]);
        }
    }

    computeCmds->PopDebugGroup();

    // Submit all compute work at once
    _hgi->SubmitCmds(computeCmds.get());

    return true;
}

} // namespace HVT_NS
