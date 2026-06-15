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

#include <hvt/tasks/outline/outlineMaskTask.h>

#include <hvt/tasks/resources.h>

#include <pxr/base/arch/hash.h>
#include <pxr/base/tf/debug.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hdSt/glslProgram.h>
#include <pxr/imaging/hdx/hgiConversions.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hgi/enums.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hio/glslfx.h>

#include <filesystem>
#include <mutex>
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEBUG_CODES(
    HVT_OUTLINE_MASK_TASK,
    HVT_OUTLINE_MASK_CACHE,
    HVT_OUTLINE_MASK_PARAMS,
    HVT_OUTLINE_MASK_RESOURCES,
    HVT_OUTLINE_MASK_SHADERCODE
);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma GCC diagnostic ignored "-Wc++20-extensions"
#endif

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(HVT_OUTLINE_MASK_TASK, "outline mask task execution");
    TF_DEBUG_ENVIRONMENT_SYMBOL(HVT_OUTLINE_MASK_CACHE, "outline mask resource caching");
    TF_DEBUG_ENVIRONMENT_SYMBOL(HVT_OUTLINE_MASK_PARAMS, "outline mask configuration params");
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        HVT_OUTLINE_MASK_SHADERCODE, "outline mask shader code before and after compilation");
    TF_DEBUG_ENVIRONMENT_SYMBOL(HVT_OUTLINE_MASK_RESOURCES, "outline mask resources");
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE;

namespace HVT_NS
{
namespace
{

enum
{
    BufferBinding_Uniforms             = 0, // Uniform buffer for shader parameters
    BufferBinding_DefaultPrimIdTexture = 0, // Input texture (default primIds)
    BufferBinding_DefaultDepthTexture  = 1, // Input texture (default depth)
    BufferBinding_BasePrimIdTexture    = 2, // Input texture (base primIds)
    BufferBinding_BaseDepthTexture     = 3, // Input texture (base depth)
    BufferBinding_OverlayPrimIdTexture = 4, // Input texture (overlay primIds)
    BufferBinding_OverlayDepthTexture  = 5, // Input texture (overlay depth)
    BufferBinding_OutputTexture        = 6, // Output texture (color)
    BufferBinding_OverlayIdValues      = 1, // Overlay ID values array
    BufferBinding_HoverIdValues        = 2, // Hover ID values array
    BufferBinding_ActiveIdValues       = 3, // Active (lead) ID values array
};

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((outlineMaskTexture, "outlineMaskTexture"))
    ((shaderPrimIds, "OutlineMaskTask::PrimIds"))
    ((shaderDepth, "OutlineMaskTask::Depth"))
    ((shaderMask3x3, "OutlineMaskTask::Mask3x3"))
    ((shaderMask5x5, "OutlineMaskTask::Mask5x5"))
);

static bool _IsStormRenderer(HdRenderDelegate* renderDelegate)
{
    if (!dynamic_cast<HdStRenderDelegate*>(renderDelegate))
    {
        return false;
    }

    return true;
}

static int _MakeMultipleOf(int dim, int localSize)
{
    return ((dim + localSize - 1) / localSize) * localSize;
}

uint64_t ComputeHash(TfToken const& sourceFile, std::string const& defines = std::string())
{
    HD_TRACE_FUNCTION();

    uint64_t hash               = 0;
    std::string const& filename = sourceFile.GetString();
    hash                        = ArchHash64(filename.c_str(), filename.size(), hash);
    hash                        = ArchHash64(defines.c_str(), defines.size(), hash);

    return hash;
}

} // namespace

static std::mutex s_cacheMutex;
static HdStGLSLProgramSharedPtr s_cachedComputeProgram;
static uint64_t s_cachedComputeProgramHash = 0;
static HgiResourceBindingsSharedPtr s_cachedResourceBindings;
static HgiSamplerHandle s_cachedSampler;
static bool s_samplerInitialized = false;
static uint64_t s_cachedResourceBindingsHash = 0;
static HgiComputePipelineSharedPtr s_cachedPipeline;
static uint64_t s_cachedPipelineHash = 0;
static int s_taskInstanceCount = 0;

OutlineMaskTask::OutlineMaskTask(HdSceneDelegate* /* delegate */, SdfPath const& id) :
    HdxTask(id), _renderIndex(nullptr), _isStormRenderer(false), _vpChanged(false)
{
    {
        std::scoped_lock lock(s_cacheMutex);
        s_taskInstanceCount++;
    }

    TfDebug::Disable(HVT_OUTLINE_MASK_TASK);
    TfDebug::Disable(HVT_OUTLINE_MASK_CACHE);
    TfDebug::Disable(HVT_OUTLINE_MASK_PARAMS);
    TfDebug::Disable(HVT_OUTLINE_MASK_SHADERCODE);
    TfDebug::Disable(HVT_OUTLINE_MASK_RESOURCES);

    _workGroupCount = GfVec3i(LOCAL_SIZE, LOCAL_SIZE, 1);

    TF_DEBUG(HVT_OUTLINE_MASK_TASK)
        .Msg("(TASK CREATED) OutlineMaskTask: %s (instance: %p, total instances: %d)\n",
            id.GetString().c_str(), static_cast<void*>(this), s_taskInstanceCount);
}

OutlineMaskTask::~OutlineMaskTask()
{
    HgiSamplerHandle samplerToDestroy;
    bool destroySampler = false;
    HdRenderIndex* renderIndex = _renderIndex;

    {
        std::scoped_lock lock(s_cacheMutex);
        s_taskInstanceCount--;

        TF_DEBUG(HVT_OUTLINE_MASK_TASK)
            .Msg("(TASK DESTROYED) OutlineMaskTask: (instance: %p, remaining instances: %d)\n",
                static_cast<void*>(this), s_taskInstanceCount);

        if (s_taskInstanceCount == 0)
        {
            TF_DEBUG(HVT_OUTLINE_MASK_CACHE)
                .Msg(
                    "(CACHE DESTROYED) OutlineMaskTask: last instance destroyed, clearing persistent "
                    "cache\n");

            s_cachedComputeProgram.reset();
            s_cachedComputeProgramHash = 0;
            s_cachedResourceBindings.reset();
            s_cachedResourceBindingsHash = 0;
            s_cachedPipeline.reset();
            s_cachedPipelineHash = 0;

            if (s_samplerInitialized && s_cachedSampler)
            {
                samplerToDestroy = s_cachedSampler;
                destroySampler = true;
            }

            s_cachedSampler      = {};
            s_samplerInitialized = false;
        }
    }

    if (destroySampler && renderIndex)
    {
        HdStResourceRegistrySharedPtr resourceRegistry =
            std::static_pointer_cast<HdStResourceRegistry>(renderIndex->GetResourceRegistry());
        if (resourceRegistry)
        {
            Hgi* hgi = resourceRegistry->GetHgi();
            if (hgi)
            {
                hgi->DestroySampler(&samplerToDestroy);
            }
        }
    }

    _CleanupAovBindings();
}

bool OutlineMaskTask::_Enabled() const
{
    return _isStormRenderer;
}

void OutlineMaskTask::_InitIfNeeded()
{
    if (_vpChanged || !_outputTexture)
    {
        _CreateAovBindings();
        _vpChanged = false;

        _workGroupCount = GfVec3i(_MakeMultipleOf(_params.size[0], LOCAL_SIZE),
            _MakeMultipleOf(_params.size[1], LOCAL_SIZE), 1);

        TF_DEBUG(HVT_OUTLINE_MASK_CACHE)
            .Msg("(CACHE) OutlineMaskTask: Compute work groups: {%d}x{%d}x{%d}\n",
                _workGroupCount[0], _workGroupCount[1], _workGroupCount[2]);
    }
}

bool OutlineMaskTask::_CreateBufferResources(Hgi* hgi)
{
    size_t overlayIdsCount   = _params.overlayIdValues.size();
    size_t overlayVec4Count  = overlayIdsCount == 0 ? 1 : ((overlayIdsCount + 3) / 4);
    size_t overlayBufferSize = overlayVec4Count * 16;
    size_t alignment  = 256;
    overlayBufferSize = ((overlayBufferSize + alignment - 1) / alignment) * alignment;

    if (!_overlayIdValuesBuffer ||
        _overlayIdValuesBuffer->GetDescriptor().byteSize < overlayBufferSize)
    {
        if (_overlayIdValuesBuffer)
        {
            hgi->DestroyBuffer(&_overlayIdValuesBuffer);
        }

        HgiBufferDesc bufferDesc;
        bufferDesc.debugName   = "overlayIdValues";
        bufferDesc.byteSize    = overlayBufferSize;
        bufferDesc.usage       = HgiBufferUsageStorage;
        _overlayIdValuesBuffer = hgi->CreateBuffer(bufferDesc);

        if (!_overlayIdValuesBuffer)
        {
            TF_CODING_ERROR("OutlineMaskTask: Failed to create overlayIdValues buffer");
            return false;
        }

        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg(
                "(RESOURCES) OutlineMaskTask: Created dynamic overlayIdValues buffer: %zu bytes "
                "for %zu IDs (%zu vec4s)\n",
                overlayBufferSize, overlayIdsCount, overlayVec4Count);
    }

    size_t hoverIdsCount   = _params.hoverIdValues.size();
    size_t hoverVec4Count  = hoverIdsCount == 0 ? 1 : ((hoverIdsCount + 3) / 4);
    size_t hoverBufferSize = hoverVec4Count * 16;
    hoverBufferSize = ((hoverBufferSize + alignment - 1) / alignment) * alignment;

    if (!_hoverIdValuesBuffer || _hoverIdValuesBuffer->GetDescriptor().byteSize < hoverBufferSize)
    {
        if (_hoverIdValuesBuffer)
        {
            hgi->DestroyBuffer(&_hoverIdValuesBuffer);
        }

        HgiBufferDesc bufferDesc;
        bufferDesc.debugName = "hoverIdValues";
        bufferDesc.byteSize  = hoverBufferSize;
        bufferDesc.usage     = HgiBufferUsageStorage;
        _hoverIdValuesBuffer = hgi->CreateBuffer(bufferDesc);

        if (!_hoverIdValuesBuffer)
        {
            TF_CODING_ERROR("OutlineMaskTask: Failed to create hoverIdValues buffer");
            return false;
        }

        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg(
                "(RESOURCES) OutlineMaskTask: Created dynamic hoverIdValues buffer: %zu bytes for "
                "%zu IDs (%zu vec4s)\n",
                hoverBufferSize, hoverIdsCount, hoverVec4Count);
    }

    size_t activeIdsCount   = _params.activeIdValues.size();
    size_t activeVec4Count  = activeIdsCount == 0 ? 1 : ((activeIdsCount + 3) / 4);
    size_t activeBufferSize = activeVec4Count * 16;

    activeBufferSize = ((activeBufferSize + alignment - 1) / alignment) * alignment;

    if (!_activeIdValuesBuffer ||
        _activeIdValuesBuffer->GetDescriptor().byteSize < activeBufferSize)
    {
        if (_activeIdValuesBuffer)
        {
            hgi->DestroyBuffer(&_activeIdValuesBuffer);
        }

        HgiBufferDesc bufferDesc;
        bufferDesc.debugName  = "activeIdValues";
        bufferDesc.byteSize   = activeBufferSize;
        bufferDesc.usage      = HgiBufferUsageStorage;
        _activeIdValuesBuffer = hgi->CreateBuffer(bufferDesc);

        if (!_activeIdValuesBuffer)
        {
            TF_CODING_ERROR("OutlineMaskTask: Failed to create activeIdValues buffer");
            return false;
        }

        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg(
                "(RESOURCES) OutlineMaskTask: Created dynamic activeIdValues buffer: %zu bytes for "
                "%zu IDs (%zu vec4s)\n",
                activeBufferSize, activeIdsCount, activeVec4Count);
    }

    return true;
}

HgiResourceBindingsSharedPtr OutlineMaskTask::_CreateResourceBindings(Hgi* hgi,
    HgiTextureHandle const& defaultPrimIdTexture, HgiTextureHandle const& defaultDepthTexture,
    HgiTextureHandle const& basePrimIdTexture, HgiTextureHandle const& baseDepthTexture,
    HgiTextureHandle const& overlayPrimIdTexture, HgiTextureHandle const& overlayDepthTexture,
    HgiTextureHandle const& outputTexture)
{
    std::scoped_lock lock(s_cacheMutex);
    HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "OutlineMaskTask";

    if (!s_samplerInitialized && (defaultPrimIdTexture || basePrimIdTexture || outputTexture))
    {
        HgiSamplerDesc samplerDesc;
        samplerDesc.debugName    = "OutlineSampler";
        samplerDesc.magFilter    = HgiSamplerFilterNearest;
        samplerDesc.minFilter    = HgiSamplerFilterNearest;
        samplerDesc.mipFilter    = HgiMipFilterNearest;
        samplerDesc.addressModeU = HgiSamplerAddressModeClampToEdge;
        samplerDesc.addressModeV = HgiSamplerAddressModeClampToEdge;
        HgiSamplerHandle sampler = hgi->CreateSampler(samplerDesc);

        if (!sampler)
        {
            TF_CODING_ERROR("OutlineMaskTask: Failed to create sampler");
            return nullptr;
        }

        s_cachedSampler      = sampler;
        s_samplerInitialized = true;
    }

    if (defaultPrimIdTexture)
    {
        HgiTextureBindDesc inputTextureDesc;
        inputTextureDesc.bindingIndex = BufferBinding_DefaultPrimIdTexture;
        inputTextureDesc.stageUsage   = HgiShaderStageCompute;
        inputTextureDesc.resourceType = HgiBindResourceTypeSampledImage;
        inputTextureDesc.textures.push_back(defaultPrimIdTexture);
        inputTextureDesc.samplers.push_back(s_cachedSampler);
        resourceDesc.textures.push_back(inputTextureDesc);
    }

    if (defaultDepthTexture)
    {
        HgiTextureBindDesc inputTextureDesc;
        inputTextureDesc.bindingIndex = BufferBinding_DefaultDepthTexture;
        inputTextureDesc.stageUsage   = HgiShaderStageCompute;
        inputTextureDesc.resourceType = HgiBindResourceTypeSampledImage;
        inputTextureDesc.textures.push_back(defaultDepthTexture);
        inputTextureDesc.samplers.push_back(s_cachedSampler);
        resourceDesc.textures.push_back(inputTextureDesc);
    }

    if (basePrimIdTexture)
    {
        HgiTextureBindDesc inputTextureDesc;
        inputTextureDesc.bindingIndex = BufferBinding_BasePrimIdTexture;
        inputTextureDesc.stageUsage   = HgiShaderStageCompute;
        inputTextureDesc.resourceType = HgiBindResourceTypeSampledImage;
        inputTextureDesc.textures.push_back(basePrimIdTexture);
        inputTextureDesc.samplers.push_back(s_cachedSampler);
        resourceDesc.textures.push_back(inputTextureDesc);
    }

    if (baseDepthTexture)
    {
        HgiTextureBindDesc inputTextureDesc;
        inputTextureDesc.bindingIndex = BufferBinding_BaseDepthTexture;
        inputTextureDesc.stageUsage   = HgiShaderStageCompute;
        inputTextureDesc.resourceType = HgiBindResourceTypeSampledImage;
        inputTextureDesc.textures.push_back(baseDepthTexture);
        inputTextureDesc.samplers.push_back(s_cachedSampler);
        resourceDesc.textures.push_back(inputTextureDesc);
    }

    if (overlayPrimIdTexture)
    {
        HgiTextureBindDesc inputTextureDesc;
        inputTextureDesc.bindingIndex = BufferBinding_OverlayPrimIdTexture;
        inputTextureDesc.stageUsage   = HgiShaderStageCompute;
        inputTextureDesc.resourceType = HgiBindResourceTypeSampledImage;
        inputTextureDesc.textures.push_back(overlayPrimIdTexture);
        inputTextureDesc.samplers.push_back(s_cachedSampler);
        resourceDesc.textures.push_back(inputTextureDesc);
    }

    if (overlayDepthTexture)
    {
        HgiTextureBindDesc inputTextureDesc;
        inputTextureDesc.bindingIndex = BufferBinding_OverlayDepthTexture;
        inputTextureDesc.stageUsage   = HgiShaderStageCompute;
        inputTextureDesc.resourceType = HgiBindResourceTypeSampledImage;
        inputTextureDesc.textures.push_back(overlayDepthTexture);
        inputTextureDesc.samplers.push_back(s_cachedSampler);
        resourceDesc.textures.push_back(inputTextureDesc);
    }

    if (outputTexture)
    {
        HgiTextureBindDesc outputTextureDesc;
        outputTextureDesc.bindingIndex = BufferBinding_OutputTexture;
        outputTextureDesc.stageUsage   = HgiShaderStageCompute;
        outputTextureDesc.resourceType = HgiBindResourceTypeStorageImage;
        outputTextureDesc.textures.push_back(outputTexture);
        outputTextureDesc.samplers.push_back(s_cachedSampler);
        resourceDesc.textures.push_back(outputTextureDesc);
    }

    if (_overlayIdValuesBuffer)
    {
        HgiBufferBindDesc bufferBindDesc;
        bufferBindDesc.bindingIndex = BufferBinding_OverlayIdValues;
        bufferBindDesc.stageUsage   = HgiShaderStageCompute;
        bufferBindDesc.resourceType = HgiBindResourceTypeStorageBuffer;
        bufferBindDesc.buffers.push_back(_overlayIdValuesBuffer);
        bufferBindDesc.offsets.push_back(0);
        bufferBindDesc.sizes.push_back(0);
        resourceDesc.buffers.push_back(std::move(bufferBindDesc));
    }

    if (_hoverIdValuesBuffer)
    {
        HgiBufferBindDesc bufferBindDesc;
        bufferBindDesc.bindingIndex = BufferBinding_HoverIdValues;
        bufferBindDesc.stageUsage   = HgiShaderStageCompute;
        bufferBindDesc.resourceType = HgiBindResourceTypeStorageBuffer;
        bufferBindDesc.buffers.push_back(_hoverIdValuesBuffer);
        bufferBindDesc.offsets.push_back(0);
        bufferBindDesc.sizes.push_back(0);
        resourceDesc.buffers.push_back(std::move(bufferBindDesc));
    }

    if (_activeIdValuesBuffer)
    {
        HgiBufferBindDesc bufferBindDesc;
        bufferBindDesc.bindingIndex = BufferBinding_ActiveIdValues;
        bufferBindDesc.stageUsage   = HgiShaderStageCompute;
        bufferBindDesc.resourceType = HgiBindResourceTypeStorageBuffer;
        bufferBindDesc.buffers.push_back(_activeIdValuesBuffer);
        bufferBindDesc.offsets.push_back(0);
        bufferBindDesc.sizes.push_back(0);
        resourceDesc.buffers.push_back(std::move(bufferBindDesc));
    }

    return std::make_shared<HgiResourceBindingsHandle>(hgi->CreateResourceBindings(resourceDesc));
}

HgiComputePipelineSharedPtr OutlineMaskTask::_CreatePipeline(
    Hgi* hgi, uint32_t constantValuesSize, HgiShaderProgramHandle const& program)
{
    HgiComputePipelineDesc desc;
    desc.debugName                    = "OutlineMaskTask compute pipeline";
    desc.shaderProgram                = program;
    desc.shaderConstantsDesc.byteSize = constantValuesSize;
    return std::make_shared<HgiComputePipelineHandle>(hgi->CreateComputePipeline(desc));
}

void OutlineMaskTask::_CreateAovBindings()
{
    if (!_renderIndex)
    {
        TF_CODING_ERROR("no render index available for AOV creation");
        return;
    }

    _CleanupAovBindings();

    if (_params.size[0] <= 0 || _params.size[1] <= 0)
    {
        TF_CODING_ERROR("Invalid buffer dimensions: %dx%d", _params.size[0], _params.size[1]);
        return;
    }

    try
    {
        auto format = HdFormatFloat16Vec4;
        HgiTextureDesc texDesc;
        texDesc.debugName      = "OutlineMaskTask Output Texture";
        texDesc.dimensions     = GfVec3i(_params.size[0], _params.size[1], 1);
        texDesc.format         = HdxHgiConversions::GetHgiFormat(format);
        texDesc.layerCount     = 1;
        texDesc.mipLevels      = 1;
        texDesc.pixelsByteSize = _params.size[0] * _params.size[1] * HdDataSizeOfFormat(format);
        texDesc.sampleCount    = HgiSampleCount1;
        texDesc.usage          = HgiTextureUsageBitsShaderWrite;
        _outputTexture         = _GetHgi()->CreateTexture(texDesc);

        if (!_outputTexture)
        {
            TF_CODING_ERROR("Output texture was not allocated");
            return;
        }

        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg("(RESOURCES) OutlineMaskTask: Successfully created AOV buffer %dx%d\n",
                _params.size[0], _params.size[1]);
    }
    catch (std::exception const& e)
    {
        TF_CODING_ERROR("Exception during AOV creation: %s", e.what());
        _CleanupAovBindings();
    }
    catch (...)
    {
        TF_CODING_ERROR("Unknown exception during AOV creation");
        _CleanupAovBindings();
    }
}

void OutlineMaskTask::_CleanupAovBindings()
{
    if (_outputTexture)
    {
        _GetHgi()->DestroyTexture(&_outputTexture);
    }

    if (_overlayIdValuesBuffer)
    {
        _GetHgi()->DestroyBuffer(&_overlayIdValuesBuffer);
    }

    if (_hoverIdValuesBuffer)
    {
        _GetHgi()->DestroyBuffer(&_hoverIdValuesBuffer);
    }

    if (_activeIdValuesBuffer)
    {
        _GetHgi()->DestroyBuffer(&_activeIdValuesBuffer);
    }
}

void OutlineMaskTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    _renderIndex     = &(delegate->GetRenderIndex());
    _isStormRenderer = _IsStormRenderer(_renderIndex->GetRenderDelegate());

    if (!_Enabled())
    {
        return;
    }

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        OutlineMaskTaskParams params;
        if (!_GetTaskParams(delegate, &params))
        {
            return;
        }

        if (_params.size != params.size)
        {
            _vpChanged = true;
        }

        _params = params;
    }

    if (!_params.enabled)
    {
        return;
    }

    _InitIfNeeded();

    _params.style.activeIdsCount  = 0;
    _params.style.overlayIdsCount = 0;
    _params.style.hoverIdsCount   = 0;

    if (_renderIndex)
    {
        _params.hoverIdValues.clear();
        _params.hoverIdValues.reserve(_params.hoverPaths.size());

        for (SdfPath const& path : _params.hoverPaths)
        {
            HdRprim const* rprim = _renderIndex->GetRprim(path);
            if (rprim)
            {
                int primId = rprim->GetPrimId();
                if (primId >= 0)
                {
                    _params.hoverIdValues.push_back(primId);
                    _params.style.hoverIdsCount++;
                }
            }
            else
            {
                SdfPathVector subtree = _renderIndex->GetRprimSubtree(path);
                for (SdfPath const& childPath : subtree)
                {
                    HdRprim const* childRprim = _renderIndex->GetRprim(childPath);
                    if (childRprim)
                    {
                        int primId = childRprim->GetPrimId();
                        if (primId >= 0)
                        {
                            _params.hoverIdValues.push_back(primId);
                            _params.style.hoverIdsCount++;
                        }
                    }
                }
                if (subtree.empty())
                {
                    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
                        .Msg("(PARAMS) OutlineMaskTask: Hover path %s not found in render index\n",
                            path.GetText());
                }
            }
        }

        _params.activeIdValues.clear();
        if (!_params.activePath.IsEmpty())
        {
            HdRprim const* rprim = _renderIndex->GetRprim(_params.activePath);
            if (rprim)
            {
                int primId = rprim->GetPrimId();
                if (primId >= 0)
                {
                    _params.activeIdValues.push_back(primId);
                    _params.style.activeIdsCount++;
                }
            }
            else
            {
                SdfPathVector subtree = _renderIndex->GetRprimSubtree(_params.activePath);
                for (SdfPath const& childPath : subtree)
                {
                    HdRprim const* childRprim = _renderIndex->GetRprim(childPath);
                    if (childRprim)
                    {
                        int primId = childRprim->GetPrimId();
                        if (primId >= 0)
                        {
                            _params.activeIdValues.push_back(primId);
                            _params.style.activeIdsCount++;
                        }
                    }
                }
            }
        }

        TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
            .Msg(
                "(PARAMS) OutlineMaskTask: Processed %zu hover paths, found %zu valid hover "
                "primitive IDs\n",
                _params.hoverPaths.size(), _params.hoverIdValues.size());

        _params.overlayIdValues.clear();
        _params.overlayIdValues.reserve(_params.overlayPaths.size());

        for (SdfPath const& path : _params.overlayPaths)
        {
            HdRprim const* rprim = _renderIndex->GetRprim(path);
            if (rprim)
            {
                int primId = rprim->GetPrimId();
                if (primId >= 0)
                {
                    _params.overlayIdValues.push_back(primId);
                    _params.style.overlayIdsCount++;
                }
            }
            else
            {
                TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
                    .Msg("(PARAMS) OutlineMaskTask: Overlay path %s not found in render index\n",
                        path.GetText());
            }
        }

        TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
            .Msg(
                "(PARAMS) OutlineMaskTask: Processed %zu overlay paths, found %zu valid primitive "
                "IDs\n",
                _params.overlayPaths.size(), _params.overlayIdValues.size());

        TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
            .Msg(
                "(PARAMS) OutlineMaskTask: Processed active path, found %d valid active primitive "
                "IDs\n",
                _params.style.activeIdsCount);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void OutlineMaskTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */)
{
}

void OutlineMaskTask::SetVisualizationMode(VisualizationMode mode)
{
    _params.maskVisualizationMode = mode;

    if (_renderIndex)
    {
        _renderIndex->GetChangeTracker().MarkTaskDirty(GetId(), HdChangeTracker::DirtyParams);
    }
}

void OutlineMaskTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();

    HdStGLSLProgramSharedPtr computeProgram = _GetComputeProgram();
    if (!computeProgram)
    {
        TF_CODING_ERROR("No compute instance available");
        return;
    }

    if (!_Enabled() || !_params.enabled)
    {
        return;
    }

    if (!_renderIndex)
    {
        TF_CODING_ERROR("No render index available");
        return;
    }

    HdStResourceRegistrySharedPtr resourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(_renderIndex->GetResourceRegistry());

    Hgi* hgi = resourceRegistry->GetHgi();
    if (!hgi)
    {
        TF_CODING_ERROR("No Hgi instance available");
        return;
    }

    if (!_outputTexture)
    {
        TF_CODING_ERROR("Output texture not allocated");
        return;
    }

    if (_params.defaultPrimIdsTexture.empty() || _params.defaultDepthTexture.empty() ||
        _params.basePrimIdsTexture.empty() || _params.baseDepthTexture.empty() ||
        _params.overlayPrimIdsTexture.empty() || _params.overlayDepthTexture.empty())
    {
        TF_CODING_ERROR("OutlineMaskTask: Input texture names must be configured");
        return;
    }

    bool overlayDistinct = (_params.overlayPrimIdsTexture != _params.basePrimIdsTexture) ||
        (_params.overlayDepthTexture != _params.baseDepthTexture);
    _params.style.hasDistinctOverlay = overlayDistinct ? 1 : 0;

    bool baseDistinct = (_params.defaultPrimIdsTexture != _params.basePrimIdsTexture) ||
        (_params.defaultDepthTexture != _params.baseDepthTexture);
    _params.style.hasDistinctDefault = baseDistinct ? 1 : 0;

    HgiTextureHandle inputDefaultPrimIds =
        _GetInputTexture(ctx, TfToken(_params.defaultPrimIdsTexture));
    if (!inputDefaultPrimIds)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg(
                "(RESOURCES) OutlineMaskTask: No default primId texture '%s' available, skipping\n",
                _params.defaultPrimIdsTexture.c_str());
        return;
    }

    HgiTextureHandle inputDefaultDepth =
        _GetInputTexture(ctx, TfToken(_params.defaultDepthTexture));
    if (!inputDefaultDepth)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg("(RESOURCES) OutlineMaskTask: No default depth texture '%s' available, skipping\n",
                _params.defaultDepthTexture.c_str());
        return;
    }

    HgiTextureHandle inputBasePrimIds = _GetInputTexture(ctx, TfToken(_params.basePrimIdsTexture));
    if (!inputBasePrimIds)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg("(RESOURCES) OutlineMaskTask: No base primId texture '%s' available, skipping\n",
                _params.basePrimIdsTexture.c_str());
        return;
    }

    HgiTextureHandle inputBaseDepth = _GetInputTexture(ctx, TfToken(_params.baseDepthTexture));
    if (!inputBaseDepth)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg("(RESOURCES) OutlineMaskTask: No base depth texture '%s' available, skipping\n",
                _params.baseDepthTexture.c_str());
        return;
    }

    HgiTextureHandle inputOverlayPrimIds =
        _GetInputTexture(ctx, TfToken(_params.overlayPrimIdsTexture));
    if (!inputOverlayPrimIds)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg(
                "(RESOURCES) OutlineMaskTask: No overlay primId texture '%s' available, skipping\n",
                _params.overlayPrimIdsTexture.c_str());
        return;
    }

    HgiTextureHandle inputOverlayDepth =
        _GetInputTexture(ctx, TfToken(_params.overlayDepthTexture));
    if (!inputOverlayDepth)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg("(RESOURCES) OutlineMaskTask: No overlay depth texture '%s' available, skipping\n",
                _params.overlayDepthTexture.c_str());
        return;
    }

    if (!_CreateBufferResources(hgi))
    {
        return;
    }

    uint64_t rbHash = (uint64_t)TfHash::Combine(inputDefaultPrimIds.GetId(),
        inputDefaultDepth.GetId(), inputBasePrimIds.GetId(), inputBaseDepth.GetId(),
        inputOverlayPrimIds.GetId(), inputOverlayDepth.GetId(), _outputTexture.GetId(),
        _overlayIdValuesBuffer ? _overlayIdValuesBuffer.GetId() : 0,
        _overlayIdValuesBuffer ? _overlayIdValuesBuffer->GetDescriptor().byteSize : 0,
        _hoverIdValuesBuffer ? _hoverIdValuesBuffer.GetId() : 0,
        _hoverIdValuesBuffer ? _hoverIdValuesBuffer->GetDescriptor().byteSize : 0,
        _activeIdValuesBuffer ? _activeIdValuesBuffer.GetId() : 0,
        _activeIdValuesBuffer ? _activeIdValuesBuffer->GetDescriptor().byteSize : 0);

    HgiResourceBindingsSharedPtr resourceBindings = nullptr;
    {
        std::scoped_lock lock(s_cacheMutex);
        if (s_cachedResourceBindings && s_cachedResourceBindingsHash == rbHash)
        {
            resourceBindings = s_cachedResourceBindings;

            TF_DEBUG(HVT_OUTLINE_MASK_CACHE)
                .Msg("(CACHE HIT) OutlineMaskTask: Found resource bindings (hash = %llu)\n", rbHash);
        }
    }

    if (!resourceBindings)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_CACHE)
            .Msg("(CACHE MISS) OutlineMaskTask: Create resource bindings (hash = %llu)\n", rbHash);

        resourceBindings =
            _CreateResourceBindings(hgi, inputDefaultPrimIds, inputDefaultDepth, inputBasePrimIds,
                inputBaseDepth, inputOverlayPrimIds, inputOverlayDepth, _outputTexture);
        if (!resourceBindings)
        {
            TF_CODING_ERROR("Failed to create resource bindings");
            return;
        }

        {
            std::scoped_lock lock(s_cacheMutex);
            s_cachedResourceBindings     = resourceBindings;
            s_cachedResourceBindingsHash = rbHash;
        }
    }

    uint64_t pHash = (uint64_t)TfHash::Combine(
        computeProgram->GetProgram().Get(), sizeof(OutlineMaskStyleParams));

    HgiComputePipelineSharedPtr pipeline = nullptr;
    {
        std::scoped_lock lock(s_cacheMutex);
        if (s_cachedPipeline && s_cachedPipelineHash == pHash)
        {
            pipeline = s_cachedPipeline;
            if (!pipeline)
            {
                TF_CODING_ERROR("Failed to create pipeline");
                return;
            }

            TF_DEBUG(HVT_OUTLINE_MASK_CACHE)
                .Msg("(CACHE HIT) OutlineMaskTask: Found pipeline (hash = %llu)\n", pHash);
        }
    }

    if (!pipeline)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_CACHE)
            .Msg("(CACHE MISS) OutlineMaskTask: Create pipeline (hash = %llu)\n", pHash);

        pipeline =
            _CreatePipeline(hgi, sizeof(OutlineMaskStyleParams), computeProgram->GetProgram());

        {
            std::scoped_lock lock(s_cacheMutex);
            s_cachedPipeline     = pipeline;
            s_cachedPipelineHash = pHash;
        }
    }

    auto defaultPrimIdsDesc = inputDefaultPrimIds->GetDescriptor();
    auto defaultDepthDesc   = inputDefaultDepth->GetDescriptor();
    auto basePrimIdsDesc    = inputBasePrimIds->GetDescriptor();
    auto baseDepthDesc      = inputBaseDepth->GetDescriptor();
    auto overlayPrimIdsDesc = inputOverlayPrimIds->GetDescriptor();
    auto overlayDepthDesc   = inputOverlayDepth->GetDescriptor();
    auto outputDesc         = _outputTexture->GetDescriptor();

    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg("(RESOURCES) OutlineMaskTask: === TEXTURE RESOURCES ===\n");
    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg("(RESOURCES) OutlineMaskTask: _params.size: {%d}x{%d}\n", _params.size[0],
            _params.size[1]);
    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg(
            "(RESOURCES) OutlineMaskTask: Default primIds texture (%s): {%d}x{%d}x{%d}, format: "
            "{%d}\n",
            _params.defaultPrimIdsTexture.c_str(), defaultPrimIdsDesc.dimensions[0],
            defaultPrimIdsDesc.dimensions[1], defaultPrimIdsDesc.dimensions[2],
            static_cast<int>(defaultPrimIdsDesc.format));
    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg(
            "(RESOURCES) OutlineMaskTask: Default depth texture (%s): {%d}x{%d}x{%d}, format: "
            "{%d}\n",
            _params.defaultDepthTexture.c_str(), defaultDepthDesc.dimensions[0],
            defaultDepthDesc.dimensions[1], defaultDepthDesc.dimensions[2],
            static_cast<int>(defaultDepthDesc.format));
    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg(
            "(RESOURCES) OutlineMaskTask: Base primIds texture (%s): {%d}x{%d}x{%d}, format: "
            "{%d}\n",
            _params.basePrimIdsTexture.c_str(), basePrimIdsDesc.dimensions[0],
            basePrimIdsDesc.dimensions[1], basePrimIdsDesc.dimensions[2],
            static_cast<int>(basePrimIdsDesc.format));
    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg("(RESOURCES) OutlineMaskTask: Base depth texture (%s): {%d}x{%d}x{%d}, format: {%d}\n",
            _params.baseDepthTexture.c_str(), baseDepthDesc.dimensions[0],
            baseDepthDesc.dimensions[1], baseDepthDesc.dimensions[2],
            static_cast<int>(baseDepthDesc.format));
    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg(
            "(RESOURCES) OutlineMaskTask: Overlay primIds texture (%s): {%d}x{%d}x{%d}, format: "
            "{%d}\n",
            _params.overlayPrimIdsTexture.c_str(), overlayPrimIdsDesc.dimensions[0],
            overlayPrimIdsDesc.dimensions[1], overlayPrimIdsDesc.dimensions[2],
            static_cast<int>(overlayPrimIdsDesc.format));
    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg(
            "(RESOURCES) OutlineMaskTask: Overlay depth texture (%s): {%d}x{%d}x{%d}, format: "
            "{%d}\n",
            _params.overlayDepthTexture.c_str(), overlayDepthDesc.dimensions[0],
            overlayDepthDesc.dimensions[1], overlayDepthDesc.dimensions[2],
            static_cast<int>(overlayDepthDesc.format));
    TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
        .Msg("(RESOURCES) OutlineMaskTask: Output texture: {%d}x{%d}x{%d}, format: {%d}\n",
            outputDesc.dimensions[0], outputDesc.dimensions[1], outputDesc.dimensions[2],
            static_cast<int>(outputDesc.format));

    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS).Msg("(PARAMS) OutlineMaskTask: === SHADER PARAMS ===\n");
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: selectedColor: RGBA(%.2f, %.2f, %.2f, %.2f)\n",
            _params.style.selectedColor[0], _params.style.selectedColor[1],
            _params.style.selectedColor[2], _params.style.selectedColor[3]);
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: selectedHoverColor: RGBA(%.2f, %.2f, %.2f, %.2f)\n",
            _params.style.selectedHoverColor[0], _params.style.selectedHoverColor[1],
            _params.style.selectedHoverColor[2], _params.style.selectedHoverColor[3]);
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: selectionLeadColor: RGBA(%.2f, %.2f, %.2f, %.2f)\n",
            _params.style.selectionLeadColor[0], _params.style.selectionLeadColor[1],
            _params.style.selectionLeadColor[2], _params.style.selectionLeadColor[3]);
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: selectionLeadHoverColor: RGBA(%.2f, %.2f, %.2f, %.2f)\n",
            _params.style.selectionLeadHoverColor[0], _params.style.selectionLeadHoverColor[1],
            _params.style.selectionLeadHoverColor[2], _params.style.selectionLeadHoverColor[3]);
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: overlayColor: RGBA(%.2f, %.2f, %.2f, %.2f)\n",
            _params.style.overlayColor[0], _params.style.overlayColor[1],
            _params.style.overlayColor[2], _params.style.overlayColor[3]);
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: overlayHoverColor: RGBA(%.2f, %.2f, %.2f, %.2f)\n",
            _params.style.overlayHoverColor[0], _params.style.overlayHoverColor[1],
            _params.style.overlayHoverColor[2], _params.style.overlayHoverColor[3]);
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: unselectedHoverColor: RGBA(%.2f, %.2f, %.2f, %.2f)\n",
            _params.style.unselectedHoverColor[0], _params.style.unselectedHoverColor[1],
            _params.style.unselectedHoverColor[2], _params.style.unselectedHoverColor[3]);
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: activeIdsCount: %d\n", _params.style.activeIdsCount);
    if (TfDebug::IsEnabled(HVT_OUTLINE_MASK_PARAMS))
    {
        for (size_t i = 0; i < _params.activeIdValues.size(); ++i)
        {
            TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
                .Msg("(PARAMS) OutlineMaskTask: > activeId[%zu]: %d\n", i,
                    _params.activeIdValues[i]);
        }
    }
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: isHoverSelected: %s\n",
            (_params.style.isHoverSelected) ? "YES" : "NO");
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: hoverIdsCount: %d\n", _params.style.hoverIdsCount);
    if (TfDebug::IsEnabled(HVT_OUTLINE_MASK_PARAMS))
    {
        for (size_t i = 0; i < _params.hoverIdValues.size(); ++i)
        {
            TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
                .Msg("(PARAMS) OutlineMaskTask: > hoverId[%zu]: %d\n", i, _params.hoverIdValues[i]);
        }
    }
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: overlayPrimIdsCount: %d\n", _params.style.overlayIdsCount);
    if (TfDebug::IsEnabled(HVT_OUTLINE_MASK_PARAMS))
    {
        for (size_t i = 0; i < _params.overlayIdValues.size(); ++i)
        {
            TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
                .Msg("(PARAMS) OutlineMaskTask: > overlayId[%zu]: %d\n", i,
                    _params.overlayIdValues[i]);
        }
    }

    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: softnessStrength: %.2f\n", _params.style.softnessStrength);
    TF_DEBUG(HVT_OUTLINE_MASK_PARAMS)
        .Msg("(PARAMS) OutlineMaskTask: softnessFalloff: %.2f\n", _params.style.softnessFalloff);

    HgiComputeCmdsDesc shaderFnDesc;
    HgiComputeCmdsUniquePtr computeCmds = hgi->CreateComputeCmds(shaderFnDesc);
    if (!computeCmds)
    {
        TF_CODING_ERROR("Failed to create compute commands");
        return;
    }

    computeCmds->PushDebugGroup("OutlineMaskTask Compute");

    void* overlayIdsStaging  = _overlayIdValuesBuffer->GetCPUStagingAddress();
    size_t overlayBufferSize = _overlayIdValuesBuffer->GetDescriptor().byteSize;

    if (_params.overlayIdValues.size() > 0)
    {
        memcpy(overlayIdsStaging, _params.overlayIdValues.data(),
            _params.overlayIdValues.size() * sizeof(int));
    }

    HgiBufferCpuToGpuOp overlayIdsBlit;
    overlayIdsBlit.cpuSourceBuffer       = overlayIdsStaging;
    overlayIdsBlit.sourceByteOffset      = 0;
    overlayIdsBlit.gpuDestinationBuffer  = _overlayIdValuesBuffer;
    overlayIdsBlit.destinationByteOffset = 0;
    overlayIdsBlit.byteSize              = overlayBufferSize;

    void* hoverIdsStaging  = _hoverIdValuesBuffer->GetCPUStagingAddress();
    size_t hoverBufferSize = _hoverIdValuesBuffer->GetDescriptor().byteSize;

    if (_params.hoverIdValues.size() > 0)
    {
        memcpy(hoverIdsStaging, _params.hoverIdValues.data(),
            _params.hoverIdValues.size() * sizeof(int));
    }

    HgiBufferCpuToGpuOp hoverIdsBlit;
    hoverIdsBlit.cpuSourceBuffer       = hoverIdsStaging;
    hoverIdsBlit.sourceByteOffset      = 0;
    hoverIdsBlit.gpuDestinationBuffer  = _hoverIdValuesBuffer;
    hoverIdsBlit.destinationByteOffset = 0;
    hoverIdsBlit.byteSize              = hoverBufferSize;

    void* activeIdsStaging  = _activeIdValuesBuffer->GetCPUStagingAddress();
    size_t activeBufferSize = _activeIdValuesBuffer->GetDescriptor().byteSize;

    if (_params.activeIdValues.size() > 0)
    {
        memcpy(activeIdsStaging, _params.activeIdValues.data(),
            _params.activeIdValues.size() * sizeof(int));
    }

    HgiBufferCpuToGpuOp activeIdsBlit;
    activeIdsBlit.cpuSourceBuffer       = activeIdsStaging;
    activeIdsBlit.sourceByteOffset      = 0;
    activeIdsBlit.gpuDestinationBuffer  = _activeIdValuesBuffer;
    activeIdsBlit.destinationByteOffset = 0;
    activeIdsBlit.byteSize              = activeBufferSize;

    HgiBlitCmdsUniquePtr blitCmds = hgi->CreateBlitCmds();
    blitCmds->CopyBufferCpuToGpu(overlayIdsBlit);
    blitCmds->CopyBufferCpuToGpu(hoverIdsBlit);
    blitCmds->CopyBufferCpuToGpu(activeIdsBlit);
    blitCmds->InsertMemoryBarrier(HgiMemoryBarrierAll);
    hgi->SubmitCmds(blitCmds.get());

    computeCmds->BindResources(*resourceBindings);
    computeCmds->BindPipeline(*pipeline);

    computeCmds->SetConstantValues(
        *pipeline, BufferBinding_Uniforms, sizeof(OutlineMaskStyleParams), &_params.style);

    computeCmds->Dispatch(_workGroupCount[0], _workGroupCount[1]);

    computeCmds->PopDebugGroup();
    hgi->SubmitCmds(computeCmds.get());

    (*ctx)[_tokens->outlineMaskTexture] = VtValue(_outputTexture);
}

TfToken const& OutlineMaskTask::GetToken()
{
    static const TfToken token { "outlineMaskTask", TfToken::Immortal };
    return token;
}

HgiTextureHandle OutlineMaskTask::_GetInputTexture(HdTaskContext* ctx, TfToken const& textureToken)
{
    if (!ctx)
    {
        TF_CODING_ERROR("No task context provided");
        return HgiTextureHandle();
    }

    if (!_HasTaskContextData(ctx, textureToken))
    {
        TF_DEBUG(HVT_OUTLINE_MASK_RESOURCES)
            .Msg("OutlineMaskTask: Texture '%s' not found in task context\n",
                textureToken.GetText());
        return HgiTextureHandle();
    }

    VtValue textureValue;
    if (!_GetTaskContextData(ctx, textureToken, &textureValue))
    {
        TF_CODING_ERROR("Failed to extract texture '%s' from context", textureToken.GetText());
        return HgiTextureHandle();
    }

    if (!textureValue.IsHolding<HgiTextureHandle>())
    {
        TF_CODING_ERROR("Texture value '%s' is not a texture handle", textureToken.GetText());
        return HgiTextureHandle();
    }

    HgiTextureHandle inputTexture = textureValue.UncheckedGet<HgiTextureHandle>();
    if (!inputTexture)
    {
        TF_CODING_ERROR("Texture handle '%s' is null", textureToken.GetText());
        return HgiTextureHandle();
    }

    auto inputDesc = inputTexture->GetDescriptor();
    if (inputDesc.dimensions[0] != _params.size[0] || inputDesc.dimensions[1] != _params.size[1])
    {
        TF_WARN(
            "OutlineMaskTask: Input texture dimensions (%dx%d) don't match expected size (%dx%d)",
            inputDesc.dimensions[0], inputDesc.dimensions[1], _params.size[0], _params.size[1]);
    }

    return inputTexture;
}

HdStGLSLProgramSharedPtr OutlineMaskTask::_GetComputeProgram()
{
    HdStResourceRegistrySharedPtr const& resourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(_renderIndex->GetResourceRegistry());

    if (!resourceRegistry)
    {
        TF_CODING_ERROR("No resource registry available");
        return nullptr;
    }

    Hgi* hgi = resourceRegistry->GetHgi();
    if (!hgi)
    {
        TF_CODING_ERROR("No Hgi instance available");
        return nullptr;
    }

    TfToken shaderToken;
    switch (_params.maskVisualizationMode)
    {
    case VisualizationMode::VISUALIZE_PRIM_IDS:
        shaderToken = _tokens->shaderPrimIds;
        break;
    case VisualizationMode::VISUALIZE_DEPTH:
        shaderToken = _tokens->shaderDepth;
        break;
    case VisualizationMode::VISUALIZE_MASK_3x3:
        shaderToken = _tokens->shaderMask3x3;
        break;
    case VisualizationMode::VISUALIZE_MASK_5x5:
    default:
        shaderToken = _tokens->shaderMask5x5;
        break;
    }
    uint64_t const hash = ComputeHash(shaderToken);

    HdStGLSLProgramSharedPtr computeProgram = nullptr;
    {
        std::scoped_lock lock(s_cacheMutex);
        if (s_cachedComputeProgram && s_cachedComputeProgramHash == hash)
        {
            computeProgram = s_cachedComputeProgram;

            TF_DEBUG(HVT_OUTLINE_MASK_CACHE)
                .Msg("(CACHE HIT) OutlineMaskTask: Found compute program (hash = %llu)\n", hash);
        }
    }

    if (!computeProgram)
    {
        TF_DEBUG(HVT_OUTLINE_MASK_CACHE)
            .Msg("(CACHE MISS) OutlineMaskTask: Create compute program for %s (hash = %llu)\n",
                shaderToken.GetString().c_str(), hash);

        TfToken const shaderPath = _GetShaderFilePath();
        if (shaderPath.IsEmpty())
        {
            return nullptr;
        }
        HioGlslfx const glslfx(shaderPath, HioGlslfxTokens->defVal);

        std::string reason;
        if (!glslfx.IsValid(&reason))
        {
            TF_CODING_ERROR("Failed to parse shader %s, error: %s\n",
                shaderPath.GetString().c_str(), reason.c_str());
            return nullptr;
        }

        HgiShaderFunctionDesc shaderFnDesc;
        shaderFnDesc.debugName                   = "Outline Mask Compute Shader";
        shaderFnDesc.shaderStage                 = HgiShaderStageCompute;
        shaderFnDesc.computeDescriptor.localSize = GfVec3i(LOCAL_SIZE, LOCAL_SIZE, 1);

        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineDefaultPrimIdsTexture",
            BufferBinding_DefaultPrimIdTexture, 2, HgiFormatInt32);

#if defined(EMSCRIPTEN)
        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineDefaultDepthTexture",
            BufferBinding_DefaultDepthTexture, 2, HgiFormatFloat32, HgiShaderTextureTypeDepth);
#else
        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineDefaultDepthTexture",
            BufferBinding_DefaultDepthTexture, 2, HgiFormatFloat32);
#endif

        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineBasePrimIdsTexture",
            BufferBinding_BasePrimIdTexture, 2, HgiFormatInt32);

#if defined(EMSCRIPTEN)
        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineBaseDepthTexture",
            BufferBinding_BaseDepthTexture, 2, HgiFormatFloat32, HgiShaderTextureTypeDepth);
#else
        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineBaseDepthTexture",
            BufferBinding_BaseDepthTexture, 2, HgiFormatFloat32);
#endif

        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineOverlayPrimIdsTexture",
            BufferBinding_OverlayPrimIdTexture, 2, HgiFormatInt32);

#if defined(EMSCRIPTEN)
        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineOverlayDepthTexture",
            BufferBinding_OverlayDepthTexture, 2, HgiFormatFloat32, HgiShaderTextureTypeDepth);
#else
        HgiShaderFunctionAddTexture(&shaderFnDesc, "outlineOverlayDepthTexture",
            BufferBinding_OverlayDepthTexture, 2, HgiFormatFloat32);
#endif

        HgiShaderFunctionAddWritableTexture(&shaderFnDesc, "outlineMaskTexture",
            BufferBinding_OutputTexture, 2, HgiFormatFloat16Vec4);

        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "selectedColor", "vec4");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "selectedHoverColor", "vec4");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "selectionLeadColor", "vec4");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "selectionLeadHoverColor", "vec4");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "overlayColor", "vec4");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "overlayHoverColor", "vec4");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "unselectedHoverColor", "vec4");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "defaultColor", "vec4");

        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "activeIdsCount", "int");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "isHoverSelected", "int");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "overlayIdsCount", "int");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "hoverIdsCount", "int");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "hasDistinctOverlay", "int");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "hasDistinctDefault", "int");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "softnessStrength", "float");
        HgiShaderFunctionAddConstantParam(&shaderFnDesc, "softnessFalloff", "float");

        HgiShaderFunctionAddBuffer(&shaderFnDesc, "overlayIdValues", "ivec4",
            BufferBinding_OverlayIdValues, HgiBindingTypeArray);

        HgiShaderFunctionAddBuffer(&shaderFnDesc, "hoverIdValues", "ivec4",
            BufferBinding_HoverIdValues, HgiBindingTypeArray);

        HgiShaderFunctionAddBuffer(&shaderFnDesc, "activeIdValues", "ivec4",
            BufferBinding_ActiveIdValues, HgiBindingTypeArray);

        HgiShaderFunctionAddStageInput(&shaderFnDesc, "hd_GlobalInvocationID", "uvec3",
            HgiShaderKeywordTokens->hdGlobalInvocationID);

        std::string const shaderCode = glslfx.GetSource(shaderToken);
        shaderFnDesc.shaderCode      = shaderCode.c_str();

        TF_DEBUG(HVT_OUTLINE_MASK_SHADERCODE)
            .Msg("(SHADERCODE) OutlineMaskTask: Original shader code:\n%s\n", shaderCode.c_str());

        std::string generatedCode;
        shaderFnDesc.generatedShaderCodeOut = &generatedCode;

        HdStGLSLProgramSharedPtr newProgram =
            std::make_shared<HdStGLSLProgram>(HdTokens->computeShader, resourceRegistry.get());

        bool result = newProgram->CompileShader(shaderFnDesc);
        if (!result)
        {
            HgiShaderProgramHandle programHandle = newProgram->GetProgram();
            if (programHandle)
            {
                TF_CODING_ERROR("Failed to compile compute shader: %s",
                    programHandle->GetCompileErrors().c_str());
            }
            else
            {
                TF_CODING_ERROR("Failed to compile compute shader: Unknown error");
            }
            return nullptr;
        }

        result = newProgram->Link();
        if (!result)
        {
            TF_CODING_ERROR("Failed to link compute shader");
            return nullptr;
        }

        if (!generatedCode.empty())
        {
            TF_DEBUG(HVT_OUTLINE_MASK_SHADERCODE)
                .Msg("(SHADERCODE) OutlineMaskTask: Generated shader code:\n%s\n",
                    generatedCode.c_str());
        }
        else
        {
            TF_CODING_ERROR("No generated code available!\n");
        }

        {
            std::scoped_lock lock(s_cacheMutex);
            s_cachedComputeProgram     = newProgram;
            s_cachedComputeProgramHash = hash;
        }

        computeProgram = s_cachedComputeProgram;

        if (!computeProgram)
        {
            TF_CODING_ERROR("GetComputeProgram failed!\n");
        }
    }

    return computeProgram;
}

TfToken OutlineMaskTask::_GetShaderFilePath()
{
    auto shaderFilePath = GetShaderPath("outlineMask.glslfx");
    if (!std::filesystem::is_regular_file(shaderFilePath))
    {
        static std::once_flag warnedOnce;
        std::call_once(warnedOnce, [&shaderFilePath]() {
            TF_WARN("OutlineMaskTask: shader file not found: %s",
                shaderFilePath.string().c_str());
        });
        return TfToken{};
    }

    static TfToken const shader { shaderFilePath.generic_u8string(), TfToken::Immortal };
    return shader;
}

} // namespace HVT_NS