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

#include "outlineTextureNames.h"

#include <hvt/tasks/resources.h>

#include <pxr/base/tf/debug.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/renderPassShader.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdSt/volume.h>

#include <filesystem>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEBUG_CODES(
    HVT_OUTLINE_PRIM_IDS_PARAMS,
    HVT_OUTLINE_PRIM_IDS_RESOURCES,
    HVT_OUTLINE_PRIM_IDS_VALIDATE
);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#endif

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        HVT_OUTLINE_PRIM_IDS_PARAMS,
        "outline primIds configuration params"
    );
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        HVT_OUTLINE_PRIM_IDS_RESOURCES,
        "outline primIds resources"
    );
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        HVT_OUTLINE_PRIM_IDS_VALIDATE,
        "outline primIds validate results"
    );
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS::Outline
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

        // Reported here rather than inferred from _aovBindings: a failure can leave a partial
        // set, which is indistinguishable from success by inspection. Without complete bindings
        // Execute() would run the render pass with nothing, or not enough, attached.
        // _CreateAovBindings() raises its own diagnostics, which are not latched, unlike the two
        // below.
        if (!_CreateAovBindings())
        {
            return false;
        }
        _vpChanged = false;
    }

    // Every resource is tested, not just the render pass: a pass that was created before the state
    // failed would otherwise make the next call skip this block and report success with a null
    // _renderPassState, which Prepare() and Execute() dereference unguarded.
    if (!_renderPass || !_renderPassState)
    {
        // Each step is guarded separately so a retry re-attempts only what is missing, and the
        // latch is released as each one succeeds: a pass that comes up on a retry must not silence
        // the diagnostic for a state that then fails.
        if (!_renderPass)
        {
            // The collection created below is just for satisfying the HdRenderPass
            // constructor. The collections for the render passes are set in Query.
            HdRprimCollection col(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));

            _renderPass = _renderIndex->GetRenderDelegate()->CreateRenderPass(&*_renderIndex, col);
            if (!_renderPass)
            {
                if (!_initWarned)
                {
                    TF_CODING_ERROR("Failed to create render pass");
                    _initWarned = true;
                }
                return false;
            }
            _initWarned = false;
        }

        if (!_renderPassState)
        {
            _renderPassState = _InitIdRenderPassState(_renderIndex, _GetShaderFilePath());
            if (!_renderPassState)
            {
                if (!_initWarned)
                {
                    TF_CODING_ERROR("Failed to create render pass state");
                    _initWarned = true;
                }
                return false;
            }
        }
    }

    _initWarned = false;
    return true;
}

bool OutlinePrimIdsTask::_CreateAovBindings()
{
    if (!_renderIndex)
    {
        TF_CODING_ERROR("No render index available for AOV creation");
        return false;
    }

    _CleanupAovBindings();

    if (_params.size[0] <= 0 || _params.size[1] <= 0)
    {
        TF_CODING_ERROR("Invalid buffer dimensions: %dx%d", _params.size[0], _params.size[1]);
        return false;
    }

    HdStResourceRegistrySharedPtr resourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(_renderIndex->GetResourceRegistry());

    if (!resourceRegistry)
    {
        TF_CODING_ERROR("No resource registry available");
        return false;
    }

    try
    {
        TfTokenVector aovOutputs;
        aovOutputs.push_back(HdAovTokens->primId);

        // The outline pipeline samples depth only: the render pass disables stencil and the mask
        // shader discards the stencil channel. A combined depth/stencil AOV is therefore never
        // read, and on WebGPU a two-aspect texture cannot be bound as a sampled texture.
        aovOutputs.push_back(HdAovTokens->depth);

        _aovBindings.clear();

        // Create AOV buffers
        for (size_t i = 0; i < aovOutputs.size(); ++i)
        {
            TfToken const& aovOutput = aovOutputs[i];
            SdfPath const aovId      = _GetAovPath(aovOutput);

            // Create the render buffer for this AOV. make_unique throws rather than returning
            // null, so allocation failure arrives either here as an exception or below as a false
            // Allocate() result.
            auto aovBuffer = std::make_unique<HdStRenderBuffer>(resourceRegistry.get(), aovId);

            HdAovDescriptor aovDesc =
                _renderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(aovOutput);

            bool success = aovBuffer->Allocate(
                GfVec3i(_params.size[0], _params.size[1], 1), aovDesc.format, false);

            if (!success)
            {
                TF_CODING_ERROR("Failed to allocate AOV buffer for %s", aovOutput.GetText());
                aovBuffer.reset();

                // Discard the bindings already pushed for earlier AOVs. A partial set survives
                // otherwise: the caller re-enters only when _vpChanged is set or _aovBuffers is
                // empty, and a partial set is neither.
                _CleanupAovBindings();
                return false;
            }

            _aovBuffers.push_back(std::move(aovBuffer));

            HdRenderPassAovBinding binding;
            binding.aovName        = aovOutput;
            binding.renderBufferId = aovId;
            binding.renderBuffer   = _aovBuffers.back().get();
            binding.aovSettings    = aovDesc.aovSettings;
            binding.clearValue     = aovDesc.clearValue;

            _aovBindings.push_back(binding);

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
        return false;
    }
    catch (...)
    {
        TF_CODING_ERROR("Unknown exception during primId AOV creation");
        _CleanupAovBindings();
        return false;
    }

    return true;
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
        // Report the bits as consumed; DirtyParams is the only one this task reads, since the
        // collection travels inside OutlinePrimIdsTaskParams rather than under
        // HdTokens->collection. This does not latch the task off: a later params update re-dirties
        // it, Hydra may sync it even when clean, and an HdRenderIndex's render delegate is fixed at
        // construction, so a renderer switch destroys this task rather than leaving
        // _isStormRenderer stale.
        *dirtyBits = HdChangeTracker::Clean;
        return;
    }

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        OutlinePrimIdsTaskParams params;
        if (!_GetTaskParams(delegate, &params))
        {
            // Leave the dirty bits set so a later re-sync retries the fetch. The previously
            // fetched parameters stay in effect meanwhile. Warn once per failure streak: this
            // path re-runs every frame while the fetch keeps failing.
            if (!_paramsFetchWarned)
            {
                TF_WARN("OutlinePrimIdsTask: could not fetch task parameters; keeping the previous "
                        "values and retrying on the next sync.");
                _paramsFetchWarned = true;
            }
            return;
        }
        _paramsFetchWarned = false;

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
        // Nothing to sync while disabled; a later enable arrives as a fresh DirtyParams.
        *dirtyBits = HdChangeTracker::Clean;
        return;
    }

    if (!_InitIfNeeded())
    {
        // Initialization failed (e.g. the render pass or ID render-pass-state could not be
        // created). Disable the task so Prepare()/Execute() do not dereference a null
        // _renderPassState. The dirty bits are intentionally left set so a later DirtyParams
        // re-sync retries initialization.
        _params.enabled = false;
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

void OutlinePrimIdsTask::_RefreshTextureTokensIfNeeded()
{
    if (!_primIdsTextureToken.IsEmpty() && _textureTokenPrefix == _params.bufferPrefix)
    {
        return;
    }

    // Not Immortal: these are derived from mutable params, and the members hold them for as long as
    // this task needs them. Immortal would pin one registry entry per prefix ever seen. (The fixed
    // names in outlineTextureNames.h are constants, so Immortal is right for those.)
    _textureTokenPrefix  = _params.bufferPrefix;
    _primIdsTextureToken = TfToken(OutlinePrimIdsTextureName(_textureTokenPrefix));
    _depthTextureToken   = TfToken(OutlineDepthTextureName(_textureTokenPrefix));
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

    // Keep the cached tokens in step with the buffer prefix before either branch below uses them.
    _RefreshTextureTokensIfNeeded();

    // When disabled, clear our textures from the task context so downstream
    // tasks don't use stale data from previous frames
    if (!_Enabled() || !_params.enabled)
    {
        ctx->erase(_primIdsTextureToken);
        ctx->erase(_depthTextureToken);
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

        (*ctx)[_primIdsTextureToken] = resource;

        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
            .Msg("(RESOURCES) OutlinePrimIdsTask: Successfully exported %s\n",
                _primIdsTextureToken.GetText());

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

            (*ctx)[_depthTextureToken] = resource;

            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
                .Msg("(RESOURCES) OutlinePrimIdsTask: Successfully exported %s\n",
                    _depthTextureToken.GetText());
        }
    }
}

TfToken const& OutlinePrimIdsTask::GetToken(const std::string& prefix)
{
    static std::mutex mutex;
    static std::unordered_map<std::string, TfToken> tokens;

    const std::string name = "outline" + prefix + "PrimIdsTask";

    // Not Immortal: the map owns each token for the life of the process, which is what lets this
    // return a reference. Immortal is for genuine constants, and would add nothing here.
    std::lock_guard<std::mutex> lock(mutex);
    return tokens.try_emplace(name, name).first->second;
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
        .Msg("(VALIDATE) OutlinePrimIdsTask: All prims in RenderIndex (%zu prims):\n",
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

    // generic_u8string() is UTF-8 on every platform (lossless for non-ASCII install paths),
    // unlike generic_string() which is the native narrow encoding (lossy ANSI on Windows).
    // The begin/end copy yields a std::string under both C++17 (char) and C++20 (char8_t).
    auto const u8str = shaderFilePath.generic_u8string();
    std::string const shaderStr(u8str.begin(), u8str.end());
    static TfToken const shader { shaderStr, TfToken::Immortal };
    return shader;
}

} // namespace HVT_NS::Outline
