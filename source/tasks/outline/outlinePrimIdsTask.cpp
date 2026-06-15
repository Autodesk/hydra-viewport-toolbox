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

#include <hvt/tasks/outline/outlinePrimIdsTask.h>

#include <hvt/tasks/resources.h>

#include <pxr/base/tf/debug.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/renderPassShader.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdSt/volume.h>
#include <pxr/imaging/hgi/capabilities.h>

#include <filesystem>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEBUG_CODES(
    HVT_OUTLINE_PRIM_IDS_PARAMS,
    HVT_OUTLINE_PRIM_IDS_RESOURCES,
    HVT_OUTLINE_PRIM_IDS_VALIDATE
);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma GCC diagnostic ignored "-Wc++20-extensions"
#endif

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        HVT_OUTLINE_PRIM_IDS_PARAMS, "outline primIds configuration params");
    TF_DEBUG_ENVIRONMENT_SYMBOL(HVT_OUTLINE_PRIM_IDS_RESOURCES, "outline primIds resources");
    TF_DEBUG_ENVIRONMENT_SYMBOL(HVT_OUTLINE_PRIM_IDS_VALIDATE, "outline primIds validate results");
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{

bool _IsStormRenderer(HdRenderDelegate* renderDelegate)
{
    return dynamic_cast<HdStRenderDelegate*>(renderDelegate) != nullptr;
}

SdfPath _GetAovPath(TfToken const& aovName)
{
    std::string identifier =
        std::string("aov_outlinePrimIds_") + TfMakeValidIdentifier(aovName.GetString());
    return SdfPath(identifier);
}

HdRenderPassStateSharedPtr _InitIdRenderPassState(HdRenderIndex* index, TfToken const& shaderPath)
{
    HdRenderPassStateSharedPtr rps = index->GetRenderDelegate()->CreateRenderPassState();

    if (HdStRenderPassState* extendedState = dynamic_cast<HdStRenderPassState*>(rps.get()))
    {
        if (shaderPath.IsEmpty())
        {
            TF_CODING_ERROR("Cannot initialize render pass state: picking shader path is empty");
            return rps;
        }
        auto pickGlslfx = std::make_shared<HioGlslfx>(shaderPath, HioGlslfxTokens->defVal);
        extendedState->SetRenderPassShader(std::make_shared<HdStRenderPassShader>(pickGlslfx));
    }
    return rps;
}

TfToken _GetOutlineTextureToken(std::string const& prefix, char const* suffix)
{
    return TfToken("outline" + prefix + suffix);
}

} // anonymous namespace

OutlinePrimIdsTask::OutlinePrimIdsTask(HdSceneDelegate* /* delegate */, SdfPath const& id) :
    HdxTask(id), _renderIndex(nullptr), _isStormRenderer(false), _vpChanged(false)
{
    TfDebug::Disable(HVT_OUTLINE_PRIM_IDS_PARAMS);
    TfDebug::Disable(HVT_OUTLINE_PRIM_IDS_RESOURCES);
    TfDebug::Disable(HVT_OUTLINE_PRIM_IDS_VALIDATE);
}

OutlinePrimIdsTask::~OutlinePrimIdsTask()
{
    _CleanupAovBindings();
}

bool OutlinePrimIdsTask::_Enabled() const
{
    return _isStormRenderer;
}

bool OutlinePrimIdsTask::_InitIfNeeded()
{
    if (_vpChanged || _aovBuffers.empty())
    {
        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
            .Msg(
                "(RESOURCES) OutlinePrimIdsTask: Viewport changed or buffers need creation: "
                "%dx%d\n",
                _params.size[0], _params.size[1]);

        _CreateAovBindings();
        _vpChanged = false;
    }

    if (!_renderPass)
    {
        // The collection created below is just for satisfying the HdRenderPass
        // constructor. The collections for the render passes are set in Query.
        HdRprimCollection col(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));

        _renderPass = _renderIndex->GetRenderDelegate()->CreateRenderPass(&*_renderIndex, col);
        if (!_renderPass)
        {
            TF_CODING_ERROR("Failed to create render pass");
            return false;
        }

        _renderPassState = _InitIdRenderPassState(_renderIndex, _GetShaderFilePath());
        if (!_renderPassState)
        {
            TF_CODING_ERROR("Failed to create render pass state");
            return false;
        }
    }
    return true;
}

void OutlinePrimIdsTask::_CreateAovBindings()
{
    if (!_renderIndex)
    {
        TF_CODING_ERROR("No render index available for AOV creation");
        return;
    }

    _CleanupAovBindings();

    if (_params.size[0] <= 0 || _params.size[1] <= 0)
    {
        TF_CODING_ERROR("Invalid buffer dimensions: %dx%d", _params.size[0], _params.size[1]);
        return;
    }

    HdStResourceRegistrySharedPtr resourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(_renderIndex->GetResourceRegistry());

    if (!resourceRegistry)
    {
        TF_CODING_ERROR("No resource registry available");
        return;
    }

    try
    {
        TfTokenVector aovOutputs;
        aovOutputs.push_back(HdAovTokens->primId);

        bool const stencilReadback = _GetHgi() &&
            _GetHgi()->GetCapabilities()->IsSet(HgiDeviceCapabilitiesBitsStencilReadback);
        TfToken depthToken = stencilReadback ? HdAovTokens->depthStencil : HdAovTokens->depth;
        aovOutputs.push_back(depthToken);

        _aovBindings.clear();

        // Create AOV buffers
        for (size_t i = 0; i < aovOutputs.size(); ++i)
        {
            TfToken const& aovOutput = aovOutputs[i];
            SdfPath const aovId      = _GetAovPath(aovOutput);

            // Create the render buffer for this AOV
            auto aovBuffer = std::make_unique<HdStRenderBuffer>(resourceRegistry.get(), aovId);
            if (!aovBuffer)
            {
                TF_CODING_ERROR("AOV buffer not allocated for %s", aovOutput.GetText());
                return;
            }

            HdAovDescriptor aovDesc =
                _renderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(aovOutput);

            bool success = aovBuffer->Allocate(
                GfVec3i(_params.size[0], _params.size[1], 1), aovDesc.format, false);

            if (!success)
            {
                TF_CODING_ERROR("Failed to allocate AOV buffer for %s", aovOutput.GetText());
                aovBuffer.reset();
                return;
            }

            _aovBuffers.push_back(std::move(aovBuffer));

            HdRenderPassAovBinding binding;
            binding.aovName        = aovOutput;
            binding.renderBufferId = aovId;
            binding.renderBuffer   = _aovBuffers.back().get();
            binding.aovSettings    = aovDesc.aovSettings;
            binding.clearValue     = aovDesc.clearValue;

            _aovBindings.push_back(binding);

            if (aovOutput == HdAovTokens->primId)
            {
                _primIdBinding = binding;
            }

            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
                .Msg("(RESOURCES) OutlinePrimIdsTask: Created AOV buffer for %s (%dx%d)\n",
                    aovOutput.GetText(), _params.size[0], _params.size[1]);
        }

        _primIdBindingIndex = 0;
        _depthBindingIndex  = 1;

        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
            .Msg(
                "(RESOURCES) OutlinePrimIdsTask: Successfully created %s primId + depth AOV "
                "buffers %dx%d\n",
                _params.bufferPrefix.c_str(), _params.size[0], _params.size[1]);
    }
    catch (std::exception const& e)
    {
        TF_CODING_ERROR("Exception during primId AOV creation: %s", e.what());
        _CleanupAovBindings();
    }
    catch (...)
    {
        TF_CODING_ERROR("Unknown exception during primId AOV creation");
        _CleanupAovBindings();
    }
}

void OutlinePrimIdsTask::_CleanupAovBindings()
{
    if (_renderIndex)
    {
        HdRenderParam* renderParam = _renderIndex->GetRenderDelegate()->GetRenderParam();
        for (auto const& aovBuffer : _aovBuffers)
        {
            aovBuffer->Finalize(renderParam);
        }
    }
    _aovBuffers.clear();
    _aovBindings.clear();
}

void OutlinePrimIdsTask::_Sync(
    HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
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
        OutlinePrimIdsTaskParams params;
        if (!_GetTaskParams(delegate, &params))
        {
            return;
        }

        if (_params.size != params.size)
        {
            _vpChanged = true;
        }

        _params = params;

        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_PARAMS)
            .Msg("(PARAMS) OutlinePrimIdsTask: enabled=%s, size=%dx%d, vpChanged=%s\n",
                params.enabled ? "YES" : "NO", params.size[0], params.size[1],
                _vpChanged ? "YES" : "NO");
    }

    if (!_params.enabled)
    {
        return;
    }

    if (!_InitIfNeeded())
    {
        return;
    }

    GfVec4i viewport(0, 0, _params.size[0], _params.size[1]);

    HdCamera const* camera = static_cast<HdCamera const*>(
        _renderIndex->GetSprim(HdPrimTypeTokens->camera, _params.camera));

    if (!camera)
    {
        TF_CODING_ERROR("Failed to get camera");
        return;
    }

    // Get the volume steps sizes in case there is any volume rendering.
    float const stepSize = delegate->GetRenderIndex().GetRenderDelegate()->GetRenderSetting<float>(
        HdStRenderSettingsTokens->volumeRaymarchingStepSize, HdStVolume::defaultStepSize);
    float const stepSizeLighting =
        delegate->GetRenderIndex().GetRenderDelegate()->GetRenderSetting<float>(
            HdStRenderSettingsTokens->volumeRaymarchingStepSizeLighting,
            HdStVolume::defaultStepSizeLighting);

    // Update the render pass states.
    HdRenderPassStateSharedPtr states[] = { _renderPassState };
    for (auto& state : states)
    {
        state->SetStencilEnabled(false);

        state->SetEnableDepthTest(true);
        state->SetEnableDepthMask(true);
        state->SetDepthFunc(HdCmpFuncLEqual);
        // Set alpha threshold, to potentially discard translucent pixels.
        // The default value of 0.0001 allows semi-transparent pixels to be picked,
        // but discards fully transparent ones.
        state->SetAlphaThreshold(0.0001f);
        state->SetAlphaToCoverageEnabled(false);
        state->SetBlendEnabled(false);
        state->SetCullStyle(_params.cullStyle);
        state->SetLightingEnabled(false);
        state->SetVolumeRenderingConstants(stepSize, stepSizeLighting);
        // Disable conservative rasterization to avoid depth artifacts
        // Conservative rasterization can cause Z-fighting at object boundaries
        state->SetConservativeRasterizationEnabled(false);

        if (camera && _params.framing.IsValid())
        {
            state->SetCamera(camera);
            state->SetFraming(_params.framing);
            state->SetOverrideWindowPolicy(_params.overrideWindowPolicy);
        }
        else if (camera)
        {
            state->SetCamera(camera);
            state->SetViewport(viewport);
        }
    }

    _renderPass->SetRprimCollection(_params.collection);

    if (TfDebug::IsEnabled(HVT_OUTLINE_PRIM_IDS_PARAMS))
    {
        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_PARAMS)
            .Msg("(RESOURCES) OutlinePrimIdsTask: Collection prims (count: %zu):\n",
                _params.collection.GetRootPaths().size());
        auto rootPaths = _params.collection.GetRootPaths();
        for (SdfPath const& path : rootPaths)
        {
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_PARAMS)
                .Msg("(RESOURCES) OutlinePrimIdsTask: > path: %s\n", path.GetString().c_str());
        }
    }

    _renderPass->Sync();

    *dirtyBits = HdChangeTracker::Clean;
}

void OutlinePrimIdsTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* renderIndex)
{
    if (!_Enabled() || !_params.enabled)
    {
        return;
    }

    _renderPassState->SetAovBindings(_aovBindings);
    _renderPassState->Prepare(renderIndex->GetResourceRegistry());
}

HgiTextureHandle OutlinePrimIdsTask::_GetTextureHandleForBinding(size_t bindingIndex) const
{
    if (_aovBindings.empty())
    {
        TF_CODING_ERROR("No AOV bindings available");
        return HgiTextureHandle();
    }

    if (bindingIndex >= _aovBindings.size())
    {
        TF_CODING_ERROR("Binding index out of bounds: %zu", bindingIndex);
        return HgiTextureHandle();
    }

    HdRenderPassAovBinding const& aovBinding = _aovBindings[bindingIndex];
    if (!aovBinding.renderBuffer)
    {
        TF_CODING_ERROR("No render buffer available for binding index %zu", bindingIndex);
        return HgiTextureHandle();
    }

    VtValue resource = aovBinding.renderBuffer->GetResource(false);
    if (!resource.IsHolding<HgiTextureHandle>())
    {
        TF_CODING_ERROR(
            "Resource is not a valid texture handle for binding index %zu", bindingIndex);
        return HgiTextureHandle();
    }

    HgiTextureHandle textureHandle = resource.UncheckedGet<HgiTextureHandle>();
    if (!textureHandle)
    {
        TF_CODING_ERROR("Null texture handle in resource for binding index %zu", bindingIndex);
        return HgiTextureHandle();
    }

    return textureHandle;
}

void OutlinePrimIdsTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!ctx)
    {
        TF_CODING_ERROR("No task context available");
        return;
    }

    // When disabled, clear our textures from the task context so downstream
    // tasks don't use stale data from previous frames
    if (!_Enabled() || !_params.enabled)
    {
        TfToken primIdsToken = _GetOutlineTextureToken(_params.bufferPrefix, "PrimIdsTexture");
        TfToken depthToken   = _GetOutlineTextureToken(_params.bufferPrefix, "DepthTexture");
        ctx->erase(primIdsToken);
        ctx->erase(depthToken);
        return;
    }

    if (!_renderIndex)
    {
        TF_CODING_ERROR("No render index available");
        return;
    }

    _renderPassState->SetAovBindings(_aovBindings);
    _renderPass->Execute(_renderPassState, GetRenderTags());

    // Export the rendered primId texture for other tasks to consume
    HgiTextureHandle textureHandle = _GetTextureHandleForBinding(_primIdBindingIndex);
    if (textureHandle)
    {
        HdRenderPassAovBinding const& aovBinding = _aovBindings[_primIdBindingIndex];
        VtValue resource                         = aovBinding.renderBuffer->GetResource(false);

        TfToken primIdsToken = _GetOutlineTextureToken(_params.bufferPrefix, "PrimIdsTexture");
        (*ctx)[primIdsToken] = resource;

        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
            .Msg("(RESOURCES) OutlinePrimIdsTask: Successfully exported %s\n",
                primIdsToken.GetText());

#ifndef __EMSCRIPTEN__
        // Note: this option is not exposed for web as it requires getting the buffer
        // from GPU to CPU and would require adopting the async texture readback API.
        // This is for debugging purposes and can be used in a desktop build.
        if (TfDebug::IsEnabled(HVT_OUTLINE_PRIM_IDS_VALIDATE))
        {
            // Validate the primId buffer to ensure correct integer values
            _ValidatePrimIdBuffer(aovBinding, resource);
        }
#endif
    }

    if (_depthBindingIndex < _aovBindings.size())
    {
        textureHandle = _GetTextureHandleForBinding(_depthBindingIndex);
        if (textureHandle)
        {
            HdRenderPassAovBinding const& aovBinding = _aovBindings[_depthBindingIndex];
            VtValue resource                         = aovBinding.renderBuffer->GetResource(false);

            TfToken depthToken = _GetOutlineTextureToken(_params.bufferPrefix, "DepthTexture");
            (*ctx)[depthToken] = resource;

            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
                .Msg("(RESOURCES) OutlinePrimIdsTask: Successfully exported %s\n",
                    depthToken.GetText());
        }
    }
}

TfToken const& OutlinePrimIdsTask::GetToken(const std::string& prefix)
{
    static std::mutex mutex;
    static std::unordered_map<std::string, TfToken> tokens;

    const std::string name = "outline" + prefix + "PrimIdsTask";

    std::lock_guard<std::mutex> lock(mutex);
    return tokens.try_emplace(name, name, TfToken::Immortal).first->second;
}

void OutlinePrimIdsTask::_ValidatePrimIdBuffer(
    HdRenderPassAovBinding /* binding */, VtValue resource)
{
    constexpr int kMaxValidationOutputCount = 10;

    HgiTextureHandle texture = resource.UncheckedGet<HgiTextureHandle>();

    if (!texture || !_renderIndex)
    {
        return;
    }

    Hgi* hgi = _GetHgi();
    if (!hgi)
    {
        TF_CODING_ERROR("No Hgi instance available\n");
        return;
    }

    SdfPathVector const& primIds = _renderIndex->GetRprimIds();

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Active prims in RenderIndex (%zu prims):\n",
            primIds.size());
    for (size_t i = 0; i < primIds.size(); ++i)
    {
        HdRprim const* rPrim = _renderIndex->GetRprim(primIds[i]);
        if (rPrim)
        {
            int32_t primId = rPrim->GetPrimId();
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > [%d]: %s\n", primId,
                    primIds[i].GetString().c_str());
        }
        else
        {
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > [<INVALID>]: %s\n",
                    primIds[i].GetString().c_str());
        }

        if (i >= kMaxValidationOutputCount)
        {
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > ... (truncated)\n");
            break;
        }
    }

    HgiTextureDesc const& texDesc = texture->GetDescriptor();
    int width                     = texDesc.dimensions[0];
    int height                    = texDesc.dimensions[1];

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: PrimId buffer dimensions: %dx%d\n", width, height);

    // Expected data size
    size_t dataSize = width * height * sizeof(int32_t);

    // Get the primId buffer using HgiTextureReadback
    size_t bufferSize = 0;
    HdStTextureUtils::AlignedBuffer<int> primIdsBuffer =
        HdStTextureUtils::HgiTextureReadback<int>(hgi, texture, &bufferSize);

    if (bufferSize != dataSize)
    {
        TF_CODING_ERROR("invalid bufferSize: %zu, expected %zu\n", bufferSize, dataSize);
        return;
    }

    int const* pixelData = primIdsBuffer.get();

    if (!pixelData)
    {
        TF_CODING_ERROR("No primIds buffer available\n");
        return;
    }

    // Count occurrences of each primId value
    std::map<int32_t, int> validPrimIdCounts;
    int invalidNegativeCount = 0;
    int invalidPositiveCount = 0;
    int validPrimIdCount     = 0;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int32_t primId = pixelData[y * width + x];

            if (primId < -1)
            {
                if (invalidNegativeCount < kMaxValidationOutputCount)
                {
                    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                        .Msg(
                            "(VALIDATE) OutlinePrimIdsTask: (%d, %d) - invalid negative value "
                            "(%d)\n",
                            x, y, primId);
                }
                invalidNegativeCount++;
            }
            else if (primId == -1)
            {
                validPrimIdCounts[primId]++;
                validPrimIdCount++;
            }
            else
            {
                SdfPath primPath = _renderIndex->GetRprimPathFromPrimId(primId);
                if (primPath.IsEmpty())
                {
                    if (invalidPositiveCount < kMaxValidationOutputCount)
                    {
                        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                            .Msg("(VALIDATE) OutlinePrimIdsTask: (%d, %d) - invalid primId (%d)\n",
                                x, y, primId);
                    }
                    invalidPositiveCount++;
                }
                else
                {
                    if (validPrimIdCount < kMaxValidationOutputCount)
                    {
                        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                            .Msg(
                                "(VALIDATE) OutlinePrimIdsTask: (%d, %d) - valid primId (%d): %s\n",
                                x, y, primId, primPath.GetString().c_str());
                    }
                    validPrimIdCounts[primId]++;
                    validPrimIdCount++;
                }
            }
        }
    }

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Count of valid pixels: %d/%d (%.4f%%)\n",
            validPrimIdCount, width * height, (validPrimIdCount * 100.0) / (width * height));

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Counts per valid primId (%d):\n", validPrimIdCount);
    for (auto const& [primId, count] : validPrimIdCounts)
    {
        if (primId == -1)
        {
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > Empty (primId -1): %d pixels (%.4f%%)\n",
                    count, (count * 100.0) / (width * height));
        }
        else
        {
            SdfPath const& primPath = _renderIndex->GetRprimPathFromPrimId(primId);
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > PrimId %d: %d pixels (%.4f%%) (%s)\n",
                    primId, count, (count * 100.0) / (width * height),
                    primPath.GetString().c_str());
        }
    }

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Count of invalid negative primIds: %d/%d (%.4f%%)\n",
            invalidNegativeCount, width * height,
            (invalidNegativeCount * 100.0) / (width * height));

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Count of invalid positive primIds: %d/%d (%.4f%%)\n",
            invalidPositiveCount, width * height,
            (invalidPositiveCount * 100.0) / (width * height));

    if (invalidNegativeCount == 0 && invalidPositiveCount == 0)
    {
        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
            .Msg("(VALIDATE) OutlinePrimIdsTask: PrimId buffer validation passed!\n");
    }
}

TfToken OutlinePrimIdsTask::_GetShaderFilePath()
{
    auto shaderFilePath = GetShaderPath("renderPassPickingShader.glslfx");
    if (!std::filesystem::is_regular_file(shaderFilePath))
    {
        TF_RUNTIME_ERROR("Shader file not found: %s", shaderFilePath.string().c_str());
        return TfToken {};
    }

    static TfToken const shader { shaderFilePath.generic_u8string(), TfToken::Immortal };
    return shader;
}

} // namespace HVT_NS
