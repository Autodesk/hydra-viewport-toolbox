// Copyright 2026 Autodesk, Inc.
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

#include <hvt/engine/depthGuideBuilder.h>

#include <hvt/engine/framePass.h>
#include <hvt/tasks/resources.h>
#include <hvt/tasks/tileFramingUtils.h>

#include <pxr/base/gf/rect2i.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/hash.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hgi/computeCmds.h>
#include <pxr/imaging/hgi/computeCmdsDesc.h>
#include <pxr/imaging/hgi/computePipeline.h>
#include <pxr/imaging/hgi/enums.h>
#include <pxr/imaging/hgi/resourceBindings.h>
#include <pxr/imaging/hgi/sampler.h>
#include <pxr/imaging/hgi/shaderFunction.h>
#include <pxr/imaging/hgi/shaderFunctionDesc.h>
#include <pxr/imaging/hgi/shaderProgram.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hio/glslfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{

// The binding slots of the reduction compute shader: the constants, then the textures. HgiGL
// numbers the shader's texture bindings by declaration order, so the textures are declared in
// slot order, with no gap.
enum
{
    BufferBinding_Uniforms   = 0,
    BufferBinding_TileDepth  = 0,
    BufferBinding_DepthGuide = 1,
};

// The local size of the reduction compute shader, in threads per axis.
constexpr int kLocalSize = 8;

// The reduction compute shader constants, in the std140 layout of the generated uniform block.
// NOTE: The ivec2 members are 8-byte aligned and the int member is 4-byte aligned, so the C++
// layout matches the shader block.
struct ReduceUniforms
{
    GfVec2i pixelOffset; // 8 bytes: full-image pixel at the tile buffer origin
    GfVec2i imageSize;   // 8 bytes: full image size
    GfVec2i guideOrigin; // 8 bytes: first owned guide texel of the tile
    GfVec2i guideCount;  // 8 bytes: owned guide texel count of the tile
    int scale;           // 4 bytes: guide downsampling scale
};

// The size of the reduction constants as uploaded: ReduceUniforms rounded up to a multiple of
// 16 bytes. WebGPU pads the uniform block to 16 bytes and rejects a constants buffer smaller
// than the padded size; the other backends ignore the tail.
constexpr size_t kReduceConstantsSize = (sizeof(ReduceUniforms) + 15) / 16 * 16;

// The maximum texture dimension the builder assumes, as Hgi does not report a limit.
constexpr int kMaxGuideDimension = 16384;

// The candidate scales of ChooseScale, smallest (best quality) first.
constexpr int kCandidateScales[] = { 1, 2, 4 };

// Divides and rounds up. The divisor must be positive.
int CeilDiv(int value, int divisor)
{
    return (value + divisor - 1) / divisor;
}

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((reduceShader, "DepthGuideBuilder::Reduce")));

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

} // namespace

DepthGuideBuilder::DepthGuideBuilder(Hgi* hgi, Reduction reduction) :
    _hgi(hgi), _reduction(reduction)
{
    if (!_hgi)
    {
        TF_CODING_ERROR("DepthGuideBuilder: the Hgi instance must not be null.");
    }
}

DepthGuideBuilder::~DepthGuideBuilder()
{
    _DestroyGpuResources();
    _ResetGuide();
}

size_t DepthGuideBuilder::GetByteSize(GfVec2i const& imageSize, int scale)
{
    if (imageSize[0] <= 0 || imageSize[1] <= 0 || scale <= 0)
    {
        return 0;
    }

    return static_cast<size_t>(CeilDiv(imageSize[0], scale)) * CeilDiv(imageSize[1], scale) *
        sizeof(float);
}

int DepthGuideBuilder::ChooseScale(GfVec2i const& imageSize, size_t budgetBytes)
{
    if (imageSize[0] <= 0 || imageSize[1] <= 0)
    {
        return 0;
    }

    for (int scale : kCandidateScales)
    {
        if (CeilDiv(imageSize[0], scale) > kMaxGuideDimension ||
            CeilDiv(imageSize[1], scale) > kMaxGuideDimension)
        {
            continue;
        }

        if (GetByteSize(imageSize, scale) <= budgetBytes)
        {
            return scale;
        }
    }

    return 0;
}

int DepthGuideBuilder::GetSweepMargin(int scale)
{
    if (scale <= 1)
    {
        return 0;
    }

    const int margin = scale - 1;
    return margin + (margin & 1);
}

DepthGuidePtr DepthGuideBuilder::Build(FramePass& sweepFramePass, GfVec2i const& imageSize,
    int scale, GfVec2i const& sweepTileSizeHint, RenderTileFn const& renderTile)
{
    if (!renderTile)
    {
        TF_CODING_ERROR("DepthGuideBuilder: the render-tile callback must not be empty.");
        return nullptr;
    }

    if (sweepTileSizeHint[0] <= 0 || sweepTileSizeHint[1] <= 0)
    {
        TF_CODING_ERROR("DepthGuideBuilder: the sweep tile size (%d, %d) must be positive.",
            sweepTileSizeHint[0], sweepTileSizeHint[1]);
        return nullptr;
    }

    // The render outputs of the pass are overwritten below: on a shaded pass, that would
    // silently stop its color rendering.
    if (sweepFramePass.GetPresetTaskLists() != FramePass::PresetTaskLists::DepthOnly)
    {
        TF_CODING_ERROR(
            "DepthGuideBuilder: the sweep frame pass must be created with the DepthOnly preset "
            "task list.");
        return nullptr;
    }

    if (!Begin(imageSize, scale))
    {
        return nullptr; // Begin posted the coding error.
    }

    TileIterator sweep;
    if (!sweep.Initialize(imageSize, GetSweepMargin(scale), sweepTileSizeHint))
    {
        TF_CODING_ERROR(
            "DepthGuideBuilder: the sweep tile size (%d, %d) is invalid for the image "
            "size (%d, %d) and the maximum tile size.",
            sweepTileSizeHint[0], sweepTileSizeHint[1], imageSize[0], imageSize[1]);
        _ResetGuide();
        return nullptr;
    }

    // The color AOV would only cost render buffer memory and resolve time.
    sweepFramePass.params().renderOutputs = { HdAovTokens->depth };

    const GfVec2i sweepTileSize = sweep.GetTileSize();
    while (!sweep.IsDone())
    {
        const TileContext tile = sweep.Next();

        // The callback only sets the view and renders: the tile placement is the builder's.
        sweepFramePass.params().renderBufferSize = sweepTileSize;
        sweepFramePass.params().viewInfo.framing = tile.framing;

        renderTile(tile);

        // The reduction maps tile texels to image texels with the tile framing, so a tile
        // rendered with another one would land at the wrong place in the guide.
        if (sweepFramePass.params().viewInfo.framing != tile.framing)
        {
            TF_CODING_ERROR(
                "DepthGuideBuilder: the render-tile callback changed the sweep tile framing "
                "of the sweep frame pass (see RenderTileFn).");
            _ResetGuide();
            return nullptr;
        }

        if (!AccumulateTile(tile, sweepFramePass.GetRenderTexture(HdAovTokens->depth)))
        {
            _ResetGuide();
            return nullptr; // AccumulateTile posted the coding error.
        }
    }

    if (!IsComplete())
    {
        const size_t guideTexelCount = static_cast<size_t>(_guideSize[0]) * _guideSize[1];
        TF_CODING_ERROR("DepthGuideBuilder: the sweep left %zu of %zu guide texels unwritten.",
            guideTexelCount - std::min(_accumulatedTexels, guideTexelCount), guideTexelCount);
        _ResetGuide();
        return nullptr;
    }

    // The guide is only ever read from here on, so leave it in the shader-read layout. The
    // Vulkan backend needs the explicit transition; the others ignore it.
    if (_reduction == Reduction::Gpu)
    {
        _guideTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
    }

    return GetGuide();
}

bool DepthGuideBuilder::Begin(GfVec2i const& imageSize, int scale)
{
    _ResetGuide();

    if (!_hgi)
    {
        TF_CODING_ERROR("DepthGuideBuilder: no Hgi instance.");
        return false;
    }

    if (imageSize[0] <= 0 || imageSize[1] <= 0)
    {
        TF_CODING_ERROR("DepthGuideBuilder: the image size (%d, %d) must be positive.",
            imageSize[0], imageSize[1]);
        return false;
    }

    if (scale <= 0)
    {
        TF_CODING_ERROR("DepthGuideBuilder: the guide scale %d must be positive.", scale);
        return false;
    }

    const GfVec2i guideSize(CeilDiv(imageSize[0], scale), CeilDiv(imageSize[1], scale));
    if (guideSize[0] > kMaxGuideDimension || guideSize[1] > kMaxGuideDimension)
    {
        TF_CODING_ERROR(
            "DepthGuideBuilder: the guide size (%d, %d) exceeds the maximum texture "
            "dimension %d; increase the scale.",
            guideSize[0], guideSize[1], kMaxGuideDimension);
        return false;
    }

    // The guide is a single-channel float texture, written by the compute pass on the GPU path
    // (hence the shader-write usage) and uploaded once on the CPU path.
    HgiTextureDesc guideDesc;
    guideDesc.debugName  = "HVT Depth Guide";
    guideDesc.dimensions = GfVec3i(guideSize[0], guideSize[1], 1);
    guideDesc.format     = HgiFormatFloat32;
    guideDesc.usage      = _reduction == Reduction::Gpu
             ? (HgiTextureUsageBitsShaderRead | HgiTextureUsageBitsShaderWrite)
             : HgiTextureUsageBitsShaderRead;
    _guideTexture        = _hgi->CreateTexture(guideDesc);
    if (!_guideTexture)
    {
        TF_CODING_ERROR("DepthGuideBuilder: failed to create the guide texture.");
        return false;
    }

    // The guide owns the texture from here on: the builder and every holder of the built guide
    // share it.
    _guide = std::make_shared<DepthGuide>(_hgi, _guideTexture, imageSize, scale);

    _imageSize         = imageSize;
    _guideSize         = guideSize;
    _scale             = scale;
    _accumulatedTexels = 0;

    if (_reduction == Reduction::Cpu)
    {
        // The staging guide starts at the far plane; every texel is stored, not accumulated,
        // by its owner tile.
        _cpuGuide.assign(static_cast<size_t>(guideSize[0]) * guideSize[1], 1.0f);
    }

    return true;
}

bool DepthGuideBuilder::AccumulateTile(
    TileContext const& tile, HgiTextureHandle const& resolvedTileDepth)
{
    if (!_guideTexture)
    {
        TF_CODING_ERROR("DepthGuideBuilder: no guide was begun.");
        return false;
    }

    CameraUtilFraming const& framing = tile.framing;
    if (!framing.IsValid() || !resolvedTileDepth)
    {
        TF_CODING_ERROR(
            "DepthGuideBuilder: the tile framing or the resolved tile depth is invalid.");
        return false;
    }

    const GfVec2i bufferSize(framing.dataWindow.GetWidth(), framing.dataWindow.GetHeight());
    const HgiTextureDesc& depthDesc = resolvedTileDepth->GetDescriptor();
    if (depthDesc.dimensions[0] != bufferSize[0] || depthDesc.dimensions[1] != bufferSize[1])
    {
        TF_CODING_ERROR(
            "DepthGuideBuilder: the tile depth texture (%d, %d) does not match the "
            "framing data window (%d, %d).",
            depthDesc.dimensions[0], depthDesc.dimensions[1], bufferSize[0], bufferSize[1]);
        return false;
    }

    // SSAOTask maps its texels to image texels with the same helper, so the guide and the
    // task's fetch of the guide cannot disagree.
    const bool isTopDown = IsTopDownTextureRowOrder(_hgi);
    GfVec2i pixelOffset, imageSize;
    ComputeImagePixelOffset(framing, bufferSize, isTopDown, &pixelOffset, &imageSize);
    if (imageSize != _imageSize)
    {
        TF_CODING_ERROR(
            "DepthGuideBuilder: the framing display window is the image (%d, %d), not "
            "the image the guide was begun for (%d, %d).",
            imageSize[0], imageSize[1], _imageSize[0], _imageSize[1]);
        return false;
    }

    // The margin the framing implies: the display window minimum is (margin - tile min).
    const int marginX =
        static_cast<int>(std::lround(framing.displayWindow.GetMin()[0])) + tile.minCoordinate[0];
    const int marginY =
        static_cast<int>(std::lround(framing.displayWindow.GetMin()[1])) + tile.minCoordinate[1];
    const int margin = std::min(marginX, marginY);
    if (margin < _scale - 1)
    {
        TF_CODING_ERROR(
            "DepthGuideBuilder: the tile margin %d is smaller than the %d a scale of %d needs.",
            margin, _scale - 1, _scale);
        return false;
    }

    // The valid region of the tile, in texture row order: the y-down convention of the tile
    // coordinates flips on a backend whose textures store the bottom row first.
    const int validMinY = isTopDown ? tile.minCoordinate[1] : _imageSize[1] - tile.maxCoordinate[1];
    const int validMaxY = isTopDown ? tile.maxCoordinate[1] : _imageSize[1] - tile.minCoordinate[1];

    // The guide texels the tile owns: those whose block of image pixels starts in the valid
    // region.
    const GfVec2i guideOrigin(CeilDiv(tile.minCoordinate[0], _scale), CeilDiv(validMinY, _scale));
    const GfVec2i guideEnd(CeilDiv(tile.maxCoordinate[0], _scale), CeilDiv(validMaxY, _scale));
    const GfVec2i guideCount = guideEnd - guideOrigin;
    if (guideCount[0] <= 0 || guideCount[1] <= 0)
    {
        return true; // The tile owns no guide texel.
    }

    const bool reduced = _reduction == Reduction::Gpu
        ? _AccumulateTileGpu(pixelOffset, guideOrigin, guideCount, resolvedTileDepth)
        : _AccumulateTileCpu(pixelOffset, guideOrigin, guideCount, bufferSize, resolvedTileDepth);
    if (!reduced)
    {
        return false; // The coding error was posted; the guide stays incomplete.
    }

    _accumulatedTexels += static_cast<size_t>(guideCount[0]) * guideCount[1];

    // The CPU staging guide is uploaded once, when the sweep completes.
    if (_reduction == Reduction::Cpu && IsComplete())
    {
        _UploadCpuGuide();
    }

    return true;
}

bool DepthGuideBuilder::IsComplete() const
{
    if (!_guideTexture)
    {
        return false;
    }

    const size_t guideTexelCount = static_cast<size_t>(_guideSize[0]) * _guideSize[1];
    return _accumulatedTexels == guideTexelCount;
}

DepthGuidePtr DepthGuideBuilder::GetGuide() const
{
    return IsComplete() ? _guide : nullptr;
}

void DepthGuideBuilder::_ResetGuide()
{
    // Only releases the builder's reference: a guide handed out stays alive with its holders.
    _guide.reset();
    _guideTexture      = HgiTextureHandle();
    _scale             = 0;
    _imageSize         = GfVec2i(0, 0);
    _guideSize         = GfVec2i(0, 0);
    _accumulatedTexels = 0;
    _cpuGuide.clear();
    _cpuGuide.shrink_to_fit();
    _cpuTileDepth.clear();
    _cpuTileDepth.shrink_to_fit();
}

void DepthGuideBuilder::_DestroyGpuResources()
{
    if (!_hgi)
    {
        return;
    }

    if (_resourceBindings)
    {
        _hgi->DestroyResourceBindings(&_resourceBindings);
        _resourceBindingsHash = 0;
    }
    if (_computePipeline)
    {
        _hgi->DestroyComputePipeline(&_computePipeline);
    }
    if (_computeProgram)
    {
        // The program does not own its shader function: both are released.
        for (HgiShaderFunctionHandle shaderFn : _computeProgram->GetShaderFunctions())
        {
            _hgi->DestroyShaderFunction(&shaderFn);
        }
        _hgi->DestroyShaderProgram(&_computeProgram);
    }
    if (_sampler)
    {
        _hgi->DestroySampler(&_sampler);
    }
}

bool DepthGuideBuilder::_PrepareGpuReduction(HgiTextureHandle const& resolvedTileDepth)
{
    if (!_sampler)
    {
        HgiSamplerDesc samplerDesc;
        samplerDesc.debugName    = "HVT Depth Guide Sampler";
        samplerDesc.magFilter    = HgiSamplerFilterNearest;
        samplerDesc.minFilter    = HgiSamplerFilterNearest;
        samplerDesc.mipFilter    = HgiMipFilterNearest;
        samplerDesc.addressModeU = HgiSamplerAddressModeClampToEdge;
        samplerDesc.addressModeV = HgiSamplerAddressModeClampToEdge;
        _sampler                 = _hgi->CreateSampler(samplerDesc);
        if (!_sampler)
        {
            TF_CODING_ERROR("DepthGuideBuilder: failed to create the sampler.");
            return false;
        }
    }

    if (!_computeProgram)
    {
        const std::filesystem::path shaderFilePath = GetShaderPath("depthGuide.glslfx");
        if (!std::filesystem::is_regular_file(shaderFilePath))
        {
            TF_CODING_ERROR(
                "DepthGuideBuilder: shader file not found: %s", shaderFilePath.string().c_str());
            return false;
        }

        // generic_u8string() is UTF-8 on every platform, unlike generic_string() which is the
        // native narrow encoding (lossy ANSI on Windows).
        auto const u8str = shaderFilePath.generic_u8string();
        std::string const shaderStr(u8str.begin(), u8str.end());

        HioGlslfx const glslfx(shaderStr, HioGlslfxTokens->defVal);
        std::string reason;
        if (!glslfx.IsValid(&reason))
        {
            TF_CODING_ERROR(
                "DepthGuideBuilder: failed to parse the shader, error: %s", reason.c_str());
            return false;
        }

        HgiShaderFunctionDesc shaderFnDesc;
        shaderFnDesc.debugName                   = "Depth Guide Reduce Compute Shader";
        shaderFnDesc.shaderStage                 = HgiShaderStageCompute;
        shaderFnDesc.computeDescriptor.localSize = GfVec3i(kLocalSize, kLocalSize, 1);

        // The tile depth is the resolved depth AOV i.e., a plain float texture, except under
        // Emscripten where depth textures need their own shader type.
#if defined(EMSCRIPTEN)
        HgiShaderFunctionAddTexture(&shaderFnDesc, "tileDepth", BufferBinding_TileDepth, 2,
            HgiFormatFloat32, HgiShaderTextureTypeDepth);
#else
        HgiShaderFunctionAddTexture(
            &shaderFnDesc, "tileDepth", BufferBinding_TileDepth, 2, HgiFormatFloat32);
#endif
        HgiShaderFunctionAddWritableTexture(
            &shaderFnDesc, "depthGuide", BufferBinding_DepthGuide, 2, HgiFormatFloat32);
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "uPixelOffset", "ivec2");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "uImageSize", "ivec2");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "uGuideOrigin", "ivec2");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "uGuideCount", "ivec2");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "uScale", "int");
        HgiShaderFunctionAddStageInput(&shaderFnDesc, "hd_GlobalInvocationID", "uvec3",
            HgiShaderKeywordTokens->hdGlobalInvocationID);

        std::string const shaderCode = glslfx.GetSource(_tokens->reduceShader);
        shaderFnDesc.shaderCode      = shaderCode.c_str();

        HgiShaderFunctionHandle shaderFn = _hgi->CreateShaderFunction(shaderFnDesc);
        if (!shaderFn || !shaderFn->IsValid())
        {
            TF_CODING_ERROR("DepthGuideBuilder: failed to compile the reduction shader: %s",
                shaderFn ? shaderFn->GetCompileErrors().c_str() : "Unknown error");
            if (shaderFn)
            {
                _hgi->DestroyShaderFunction(&shaderFn);
            }
            return false;
        }

        HgiShaderProgramDesc programDesc;
        programDesc.debugName = "Depth Guide Reduce Compute Program";
        programDesc.shaderFunctions.push_back(shaderFn);
        HgiShaderProgramHandle program = _hgi->CreateShaderProgram(programDesc);
        if (!program || !program->IsValid())
        {
            TF_CODING_ERROR("DepthGuideBuilder: failed to link the reduction shader: %s",
                program ? program->GetCompileErrors().c_str() : "Unknown error");
            if (program)
            {
                _hgi->DestroyShaderProgram(&program);
            }
            _hgi->DestroyShaderFunction(&shaderFn);
            return false;
        }

        _computeProgram = program;
    }

    // The compute pipeline; it depends only on the program and the constants size.
    if (!_computePipeline)
    {
        HgiComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName                    = "Depth Guide Reduce Compute Pipeline";
        pipelineDesc.shaderProgram                = _computeProgram;
        pipelineDesc.shaderConstantsDesc.byteSize = kReduceConstantsSize;
        _computePipeline                          = _hgi->CreateComputePipeline(pipelineDesc);
        if (!_computePipeline)
        {
            TF_CODING_ERROR("DepthGuideBuilder: failed to create the compute pipeline.");
            return false;
        }
    }

    // The resource bindings, rebuilt when a texture handle changes (e.g. the render buffers
    // were re-created between two tiles).
    const uint64_t bindingsHash = TfHash::Combine(resolvedTileDepth.GetId(), _guideTexture.GetId());
    if (!_resourceBindings || _resourceBindingsHash != bindingsHash)
    {
        if (_resourceBindings)
        {
            // The handle is a non-owning wrapper, so the previous bindings are released
            // explicitly instead of being leaked on every texture change.
            _hgi->DestroyResourceBindings(&_resourceBindings);
        }

        HgiResourceBindingsDesc resourceDesc;
        resourceDesc.debugName = "DepthGuideBuilder";

        HgiTextureBindDesc tileDepthDesc;
        tileDepthDesc.bindingIndex = BufferBinding_TileDepth;
        tileDepthDesc.stageUsage   = HgiShaderStageCompute;
        tileDepthDesc.resourceType = HgiBindResourceTypeSampledImage;
        tileDepthDesc.textures.push_back(resolvedTileDepth);
        tileDepthDesc.samplers.push_back(_sampler);
        resourceDesc.textures.push_back(tileDepthDesc);

        HgiTextureBindDesc guideDesc;
        guideDesc.bindingIndex = BufferBinding_DepthGuide;
        guideDesc.stageUsage   = HgiShaderStageCompute;
        guideDesc.resourceType = HgiBindResourceTypeStorageImage;
        guideDesc.textures.push_back(_guideTexture);
        guideDesc.samplers.push_back(_sampler);
        resourceDesc.textures.push_back(guideDesc);

        _resourceBindings = _hgi->CreateResourceBindings(resourceDesc);
        if (!_resourceBindings)
        {
            TF_CODING_ERROR("DepthGuideBuilder: failed to create the resource bindings.");
            return false;
        }
        _resourceBindingsHash = bindingsHash;
    }

    return true;
}

bool DepthGuideBuilder::_AccumulateTileGpu(GfVec2i const& pixelOffset, GfVec2i const& guideOrigin,
    GfVec2i const& guideCount, HgiTextureHandle const& resolvedTileDepth)
{
    if (!_PrepareGpuReduction(resolvedTileDepth))
    {
        return false; // The coding error was posted.
    }

    resolvedTileDepth->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
    _guideTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderWrite);

    ReduceUniforms uniforms;
    uniforms.pixelOffset = pixelOffset;
    uniforms.imageSize   = _imageSize;
    uniforms.guideOrigin = guideOrigin;
    uniforms.guideCount  = guideCount;
    uniforms.scale       = _scale;

    HgiComputeCmdsDesc cmdsDesc;
    HgiComputeCmdsUniquePtr computeCmds = _hgi->CreateComputeCmds(cmdsDesc);
    if (!computeCmds)
    {
        TF_CODING_ERROR("DepthGuideBuilder: failed to create the compute commands.");
        return false;
    }

    computeCmds->PushDebugGroup("DepthGuideBuilder Reduce");
    computeCmds->BindResources(_resourceBindings);
    computeCmds->BindPipeline(_computePipeline);

    std::array<char, kReduceConstantsSize> constants {}; // The tail stays zero.
    std::memcpy(constants.data(), &uniforms, sizeof(uniforms));
    computeCmds->SetConstantValues(
        _computePipeline, BufferBinding_Uniforms, kReduceConstantsSize, constants.data());

    // One thread per owned guide texel; the backend rounds the dispatch up to the local size.
    computeCmds->Dispatch(guideCount[0], guideCount[1]);
    computeCmds->InsertMemoryBarrier(HgiMemoryBarrierAll);
    computeCmds->PopDebugGroup();
    _hgi->SubmitCmds(computeCmds.get());

    // The render tasks of the next tile write the depth AOV as a depth target.
    resolvedTileDepth->SubmitLayoutChange(HgiTextureUsageBitsDepthTarget);

    return true;
}

bool DepthGuideBuilder::_AccumulateTileCpu(GfVec2i const& pixelOffset, GfVec2i const& guideOrigin,
    GfVec2i const& guideCount, GfVec2i const& bufferSize, HgiTextureHandle const& resolvedTileDepth)
{
    // Reads the tile depth back, in the row order of the backend's textures.
    std::vector<float>& tileDepth = _cpuTileDepth;
    tileDepth.resize(static_cast<size_t>(bufferSize[0]) * bufferSize[1]);
    HgiTextureGpuToCpuOp readBackOp {};
    readBackOp.gpuSourceTexture          = resolvedTileDepth;
    readBackOp.cpuDestinationBuffer      = tileDepth.data();
    readBackOp.destinationBufferByteSize = tileDepth.size() * sizeof(float);
    HgiBlitCmdsUniquePtr blitCmds        = _hgi->CreateBlitCmds();
    blitCmds->CopyTextureGpuToCpu(readBackOp);
    _hgi->SubmitCmds(blitCmds.get(), HgiSubmitWaitTypeWaitUntilCompleted);

    // Min-reduces the owned guide texels into the staging guide, with the same ownership math
    // as the compute pass: each texel stores the depth of the closest surface of its block,
    // skipping the pixels beyond the image (a partial last block).
    for (int gy = guideOrigin[1]; gy < guideOrigin[1] + guideCount[1]; ++gy)
    {
        for (int gx = guideOrigin[0]; gx < guideOrigin[0] + guideCount[0]; ++gx)
        {
            float depth = 1.0f;
            for (int j = 0; j < _scale; ++j)
            {
                for (int i = 0; i < _scale; ++i)
                {
                    const GfVec2i imagePixel(gx * _scale + i, gy * _scale + j);
                    if (imagePixel[0] >= _imageSize[0] || imagePixel[1] >= _imageSize[1])
                    {
                        continue;
                    }

                    const GfVec2i bufferPixel = imagePixel - pixelOffset;
                    depth                     = std::min(depth,
                                            tileDepth[static_cast<size_t>(bufferPixel[1]) * bufferSize[0] +
                            bufferPixel[0]]);
                }
            }
            _cpuGuide[static_cast<size_t>(gy) * _guideSize[0] + gx] = depth;
        }
    }

    return true;
}

void DepthGuideBuilder::_UploadCpuGuide()
{
    HgiTextureCpuToGpuOp uploadOp;
    uploadOp.gpuDestinationTexture = _guideTexture;
    uploadOp.cpuSourceBuffer       = _cpuGuide.data();
    uploadOp.bufferByteSize        = _cpuGuide.size() * sizeof(float);
    HgiBlitCmdsUniquePtr blitCmds  = _hgi->CreateBlitCmds();
    blitCmds->CopyTextureCpuToGpu(uploadOp);
    _hgi->SubmitCmds(blitCmds.get(), HgiSubmitWaitTypeWaitUntilCompleted);
}

} // namespace HVT_NS
