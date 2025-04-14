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

#include "renderBufferManager.h"

#include <hvt/engine/taskUtils.h>
#include <hvt/tasks/aovInputTask.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wextra-semi"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4201)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4305)
#endif
// clang-format on

#include <pxr/pxr.h>

#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/sdf/path.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (renderBufferDescriptor)
);

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace hvt
{

/// The Impl is derived from HdxTaskController. The Impl consolidates
/// the render-buffer related operations which were originally in the Task Controller.
///
/// The implementation is private and hidden for now. This will evolve. Modify at your own risks.
class RenderBufferManager::Impl : public RenderBufferSettingsProvider
{
public:
    explicit Impl(HdRenderIndex* pRenderIndex, SyncDelegatePtr& syncDelegate);
    ~Impl();

    Impl(Impl const&)            = delete;
    Impl& operator=(Impl const&) = delete;

    /// Sets the size of the render buffer and MSAA settings, update render buffer descriptors.
    void SetBufferSizeAndMsaa(
        const GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled);

    HdRenderBuffer* GetRenderOutput(TfToken const& name, SdfPath const& controllerId);

    /// Updates render output parameters and creates new render buffers if needed.
    /// Note: AOV binding values are stored here and consulted later by RenderTasks.
    bool SetRenderOutputs(TfTokenVector const& names, RenderBufferBindings const& inputs,
        GfVec4d const& viewport, SdfPath const& controllerId);

    /// Updates the render output clear color.
    /// Note: Clear color values are stored here and consulted later by RenderTasks.
    void SetRenderOutputClearColor(
        TfToken const& name, SdfPath const& controllerId, VtValue const& clearValue);

    /// Set the framebuffer to present the render to.
    void SetPresentationOutput(TfToken const& api, VtValue const& framebuffer)
    {
        _aovTaskCache.presentApi         = api;
        _aovTaskCache.presentFramebuffer = framebuffer;
    }

    /// Returns true if AOVs (RenderBuffer Bprims) are supported by the render delegate.
    bool AovsSupported() const override;

    /// Returns the name of the AOV to be used for the viewport.
    TfToken const& GetViewportAov() const override { return _viewportAov; }

    /// Get the size of the render buffers.
    GfVec2i const& GetRenderBufferSize() const override;

    /// Builds the AOV oath from the controller ID and AOV name.
    static SdfPath GetAovPath(const SdfPath& controllerID, const TfToken& aov);

    /// Retuns the AOV parameter cache, containing data required to update RenderTask AOV binding
    /// parameters.
    const AovParams& GetAovParamCache() const override { return _aovTaskCache; }

private:
    /// Sets the viewport render output (color or buffer visualization).
    void SetViewportRenderOutput(const TfToken& name, HdRenderBuffer* aovBuffer,
        HdRenderBuffer* depthBuffer, const SdfPath& controllerId);

    /// The render texture dimensions.
    GfVec2i _renderBufferSize;

    /// Multisampling enabled or not.
    bool _enableMultisampling { true };

    /// Number of samples for multisampling.
    size_t _msaaSampleCount { 4 };

    bool _enableProgressiveRendering;

    /// List of Bprim IDs. These IDs are used to:
    ///  - Insert and remove Bprims from the RenderIndex.
    ///  - Get Bprims from the RenderIndex.
    ///  - Get and Set parameters in the SyncDelegate.
    ///     e.g.
    ///       aovDelegate.param[bufferID, tokens::stormMsaaSampleCount]
    ///       HdRenderBufferDescriptor aovDelegate.param[bufferID, tokens::renderBufferDescriptor]
    SdfPathVector _aovBufferIds;

    /// AOV output cache, for checking if outputs have changed since the last call and only update
    /// render or aovBindings when necessary.
    TfTokenVector _aovOutputs;

    /// AOV input cache, for checking if inputs have changed since the last call.
    RenderBufferBindings  _aovInputs;

    /// Viewport AOV cache to prevent unnecessary execution or dirty states in
    /// SetViewportRenderOutput.
    TfToken _viewportAov;

    /// Intermediate storage for RenderTask AOV parameters. These values are meant to be set
    /// into RenderTaskParams, but the RenderBufferManager is not responsible for doing it, it only
    /// stores the values.
    AovParams _aovTaskCache;

    /// The RenderIndex, used to create Bprims (buffers).
    HdRenderIndex* _pRenderIndex;

    /// The SyncDelegate used to create RenderBufferDescriptor data for use by the render index.
    SyncDelegatePtr _syncDelegate;
};

RenderBufferManager::Impl::Impl(HdRenderIndex* pRenderIndex, SyncDelegatePtr& syncDelegate) :
    _renderBufferSize(0, 0), _pRenderIndex(pRenderIndex), _syncDelegate(syncDelegate)
{
    _aovTaskCache.presentApi    = HgiTokens->OpenGL;
    _enableProgressiveRendering = { TfGetenvBool("AGP_ENABLE_PROGRESSIVE_RENDERING", false) };
}

RenderBufferManager::Impl::~Impl()
{
    for (auto const& id : _aovBufferIds)
    {
        _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, id);
    }
}

bool RenderBufferManager::Impl::AovsSupported() const
{
    return _pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer);
}

GfVec2i const& RenderBufferManager::Impl::GetRenderBufferSize() const
{
    return _renderBufferSize;
}

SdfPath RenderBufferManager::Impl::GetAovPath(const SdfPath& controllerID, const TfToken& aov)
{
    std::string identifier = std::string("aov_") + TfMakeValidIdentifier(aov.GetString());
    return controllerID.AppendChild(TfToken(identifier));
}

bool RenderBufferManager::Impl::SetRenderOutputs(const TfTokenVector& outputs,
    RenderBufferBindings const& inputs, GfVec4d const& viewport, SdfPath const& controllerId)
{
    if (!AovsSupported())
    {
        return false;
    }

    if (!_enableProgressiveRendering && _aovOutputs == outputs &&
        inputs.size() == _aovInputs.size() &&
        std::equal(inputs.begin(), inputs.end(), _aovInputs.begin(), _aovInputs.end()))
    {
        return false;
    }

    // If progressive rendering is enabled, render buffer clear is only required when `_aovOutputs
    // != outputs`.
    bool needClear = !_enableProgressiveRendering || _aovOutputs != outputs;

    _aovOutputs = outputs;

    if (inputs.size() > 0)
    {
        // Clear the viewport AOV as we may have new inputs.
        _aovInputs.clear();
        std::copy(inputs.begin(), inputs.end(), back_inserter(_aovInputs));
        _viewportAov = TfToken();
    }

    // NOTE: A function could be used to get localOutputs.
    TfTokenVector localOutputs = outputs;

    // When we're asked to render "color", we treat that as final color,
    // complete with depth-compositing and selection, so we in-line add
    // some extra buffers if they weren't already requested.
    if (IsStormRenderDelegate(_pRenderIndex))
    {
        if (std::find(localOutputs.begin(), localOutputs.end(), HdAovTokens->depth) ==
            localOutputs.end())
        {
            localOutputs.push_back(HdAovTokens->depth);
        }
    }
    else
    {
        std::set<TfToken> mainRenderTokens;
        for (auto const& aov : outputs)
        {
            if (aov == HdAovTokens->color || aov == HdAovTokens->depth ||
                aov == HdAovTokens->primId || aov == HdAovTokens->instanceId ||
                aov == HdAovTokens->elementId)
            {
                mainRenderTokens.insert(aov);
            }
        }
        // For a backend like PrMan/Embree we fill not just the color buffer,
        // but also buffers that are used during picking.
        if (mainRenderTokens.count(HdAovTokens->color) > 0)
        {
            if (mainRenderTokens.count(HdAovTokens->depth) == 0)
            {
                localOutputs.push_back(HdAovTokens->depth);
            }
            if (mainRenderTokens.count(HdAovTokens->primId) == 0)
            {
                localOutputs.push_back(HdAovTokens->primId);
            }
            if (mainRenderTokens.count(HdAovTokens->elementId) == 0)
            {
                localOutputs.push_back(HdAovTokens->elementId);
            }
            if (mainRenderTokens.count(HdAovTokens->instanceId) == 0)
            {
                localOutputs.push_back(HdAovTokens->instanceId);
            }
        }
    }

    // This will delete Bprims from the RenderIndex and clear _aovBufferIds SdfPathVector
    if (needClear)
    {
        for (size_t i = 0; i < _aovBufferIds.size(); ++i)
        {
            _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, _aovBufferIds[i]);
        }
        _aovBufferIds.clear();
    }

    // Temporary 2D dimensions to calculate dimensions3 (the 2D version isn't used later).
    const GfVec2i dimensions =
        _renderBufferSize != GfVec2i(0) ? _renderBufferSize : GfVec2i(viewport[2], viewport[3]);

    const GfVec3i dimensions3(dimensions[0], dimensions[1], 1);

    // Get default AOV descriptors from the render delegate for each AOV token.
    // E.g. color:HdFormatFloat16Vec4, depth:HdFormatFloat32.
    HdAovDescriptorList outputDescs;
    for (auto it = localOutputs.begin(); it != localOutputs.end();)
    {
        HdAovDescriptor desc = _pRenderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(*it);
        if (desc.format == HdFormatInvalid)
        {
            // The backend doesn't support this AOV, so skip it.
            it = localOutputs.erase(it);
        }
        else
        {
            // Otherwise, stash the desc and move forward.
            outputDescs.push_back(desc);
            ++it;
        }
    }

    // Add the new RenderBuffers.
    // NOTE: GetAovPath returns ids of the form {controller_id}/aov_{name}.
    for (size_t i = 0; i < localOutputs.size(); ++i)
    {
        HdRenderBuffer* foundInput = nullptr;
        for (auto input : inputs)
        {
            if (input.first == localOutputs[i])
            {
                foundInput = input.second;
                break;
            }
        }
        if (!foundInput)
        {
            const SdfPath aovId = GetAovPath(controllerId, localOutputs[i]);
            _pRenderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, _syncDelegate.get(), aovId);
            HdRenderBufferDescriptor desc;
            desc.dimensions   = dimensions3;
            desc.format       = outputDescs[i].format;
            desc.multiSampled = _enableMultisampling;
            _syncDelegate->SetValue(aovId, _tokens->renderBufferDescriptor, VtValue(desc));
            _syncDelegate->SetValue(aovId, HdStRenderBufferTokens->stormMsaaSampleCount,
                VtValue(desc.multiSampled ? _msaaSampleCount : 1));
            _pRenderIndex->GetChangeTracker().MarkBprimDirty(
                aovId, HdRenderBuffer::DirtyDescription);
            _aovBufferIds.push_back(aovId);
        }
    }

    // Create the list of AOV bindings.
    // This section only fills the 3 vectors below: aovBindingsClear, aovBindingsNoClear,
    // aovInputBindings.
    // Only the first render task clears AOVs so we also have a bindings set that specifies no clear
    // color for the remaining render tasks.
    HdRenderPassAovBindingVector aovBindingsClear;
    HdRenderPassAovBindingVector aovBindingsNoClear;
    HdRenderPassAovBindingVector aovInputBindings;
    aovBindingsClear.resize(localOutputs.size());
    aovBindingsNoClear.resize(aovBindingsClear.size());

    for (size_t i = 0; i < localOutputs.size(); ++i)
    {
        HdRenderBuffer* foundInput = nullptr;
        for (auto input : inputs)
        {
            if (input.first == localOutputs[i])
            {
                foundInput = input.second;
                break;
            }
        }
        aovBindingsClear[i].aovName        = localOutputs[i];
        aovBindingsClear[i].clearValue     = !foundInput ? outputDescs[i].clearValue : VtValue();
        aovBindingsClear[i].renderBufferId = GetAovPath(controllerId, localOutputs[i]);
        aovBindingsClear[i].aovSettings    = outputDescs[i].aovSettings;
        aovBindingsClear[i].renderBuffer   = foundInput;

        aovBindingsNoClear[i]            = aovBindingsClear[i];
        aovBindingsNoClear[i].clearValue = VtValue();

        if (localOutputs[i] == HdAovTokens->depth)
        {
            aovInputBindings.push_back(aovBindingsNoClear[i]);
        }
    }

    _aovTaskCache.aovBindingsClear   = aovBindingsClear;
    _aovTaskCache.aovBindingsNoClear = aovBindingsNoClear;
    _aovTaskCache.aovInputBindings   = aovInputBindings;
    _aovTaskCache.hasNoAovInputs     = (inputs.size() == 0); // For progressive rendering only?

    const SdfPath volumeId = GetRenderTaskPath(controllerId, HdStMaterialTagTokens->volume);

    // For AOV visualization, if only one output was specified, send it to the viewer; otherwise,
    // disable colorization.
    if (outputs.size() == 1)
    {
        HdRenderBuffer* firstInput = nullptr;
        HdRenderBuffer* depthInput = nullptr;
        for (auto input : inputs)
        {
            if (input.first == localOutputs[0])
                firstInput = input.second;
            if (input.first == "depth")
                depthInput = input.second;
        }
        SetViewportRenderOutput(outputs[0], firstInput, depthInput, controllerId);
    }
    else
    {
        // TODO: Here, I guess it is expected the function won't do much. Maybe this should be
        // clarified:
        //       - The previous version have default nullptr arguments
        //       - The new version has more parameters, which might not support being null (risk of
        //       crash?) Try to clarify which parameter is okay to set to null, and which is not.
        //       Reorder parameters and use default values if that makes more sense (probable).
        SetViewportRenderOutput(TfToken(), nullptr, nullptr, controllerId);
    }

    // NOTE: The viewport data plumbed to tasks unfortunately depends on whether aovs are being
    // used.
    return true;
}

void RenderBufferManager::Impl::SetViewportRenderOutput(TfToken const& name,
    HdRenderBuffer* aovBuffer, HdRenderBuffer* depthBuffer, const SdfPath& controllerId)
{
    if (!AovsSupported())
    {
        return;
    }

    if (_viewportAov == name)
    {
        return;
    }
    _viewportAov = name;

    if (name.IsEmpty())
    {
        _aovTaskCache.aovBufferPath   = SdfPath::EmptyPath();
        _aovTaskCache.depthBufferPath = SdfPath::EmptyPath();
        _aovTaskCache.aovBuffer       = nullptr;
        _aovTaskCache.depthBuffer     = nullptr;
    }
    else if (name == HdAovTokens->color)
    {
        _aovTaskCache.aovBufferPath   = GetAovPath(controllerId, HdAovTokens->color);
        _aovTaskCache.depthBufferPath = GetAovPath(controllerId, HdAovTokens->depth);
        _aovTaskCache.aovBuffer       = aovBuffer
                  ? aovBuffer
                  : static_cast<HdRenderBuffer*>(_pRenderIndex->GetBprim(
                  HdPrimTypeTokens->renderBuffer, _aovTaskCache.aovBufferPath));

        _aovTaskCache.depthBuffer = depthBuffer
            ? depthBuffer
            : static_cast<HdRenderBuffer*>(_pRenderIndex->GetBprim(
                  HdPrimTypeTokens->renderBuffer, _aovTaskCache.depthBufferPath));
    }
    else
    {
        // When visualizing a buffer other than color, this condition is executed.
        _aovTaskCache.aovBufferPath   = GetAovPath(controllerId, name);
        _aovTaskCache.depthBufferPath = SdfPath::EmptyPath();
        _aovTaskCache.aovBuffer       = aovBuffer ? aovBuffer : GetRenderOutput(name, controllerId);
        _aovTaskCache.depthBuffer     = nullptr;
    }
}

HdRenderBuffer* RenderBufferManager::Impl::GetRenderOutput(
    const TfToken& name, const SdfPath& controllerId)
{
    if (!AovsSupported())
    {
        return nullptr;
    }

    SdfPath renderBufferId = GetAovPath(controllerId, name);
    return static_cast<HdRenderBuffer*>(
        _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, renderBufferId));
}

void RenderBufferManager::Impl::SetRenderOutputClearColor(
    const TfToken& name, const SdfPath& controllerId, const VtValue& clearValue)
{
    if (!AovsSupported())
    {
        return;
    }

    // Check if we're setting a value for a nonexistent AOV.
    SdfPath renderBufferId                          = GetAovPath(controllerId, name);
    _aovTaskCache.outputClearValues[renderBufferId] = clearValue;
}

void RenderBufferManager::Impl::SetBufferSizeAndMsaa(
    const GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled)
{
    HdChangeTracker& changeTracker = _pRenderIndex->GetChangeTracker();

    bool descriptorSpecsChanged = false;
    bool msaaSampleCountChanged = false;

    if (_enableMultisampling != msaaEnabled || _renderBufferSize != newRenderBufferSize)
    {
        _renderBufferSize      = newRenderBufferSize;
        _enableMultisampling   = msaaEnabled;
        descriptorSpecsChanged = true;
    }

    if (_msaaSampleCount != msaaSampleCount)
    {
        _msaaSampleCount       = msaaSampleCount;
        msaaSampleCountChanged = true;
    }

    if (!msaaSampleCountChanged && !descriptorSpecsChanged)
    {
        // No changes to the render buffer size or MSAA sample count.
        return;
    }

    const GfVec3i dimensions3(_renderBufferSize[0], _renderBufferSize[1], 1);

    for (auto const& id : _aovBufferIds)
    {
        bool bprimDirty = false;
        if (descriptorSpecsChanged)
        {
            VtValue vParams = _syncDelegate->GetValue(id, _tokens->renderBufferDescriptor);
            HdRenderBufferDescriptor desc = vParams.Get<HdRenderBufferDescriptor>();

            if (desc.dimensions != dimensions3 || desc.multiSampled != _enableMultisampling)
            {
                desc.dimensions   = dimensions3;
                desc.multiSampled = _enableMultisampling;
                _syncDelegate->SetValue(id, _tokens->renderBufferDescriptor, VtValue(desc));
                bprimDirty = true;
            }
        }

        if (msaaSampleCountChanged)
        {
            _syncDelegate->SetValue(
                id, HdStRenderBufferTokens->stormMsaaSampleCount, VtValue(_msaaSampleCount));
            bprimDirty = true;
        }

        if (bprimDirty)
        {
            changeTracker.MarkBprimDirty(id, HdRenderBuffer::DirtyDescription);
        }
    }
}

RenderBufferManager::RenderBufferManager(
    SdfPath const& taskManagerUid, HdRenderIndex* pRenderIndex, SyncDelegatePtr& syncDelegate) :
    _taskManagerUid(taskManagerUid), _pRenderIndex(pRenderIndex)
{
    _impl = std::make_unique<Impl>(pRenderIndex, syncDelegate);
}

RenderBufferManager::~RenderBufferManager() {}

HgiTextureHandle RenderBufferManager::GetAovTexture(TfToken const& token, HdEngine* engine) const
{
    VtValue aov;
    HgiTextureHandle aovTexture;

    // NOTE: The Metal only implementation needs an access to "id<MTLTexture>" that
    // only the HgiTextureHandle provides (by casting to HgiMetalTexture).

    if (engine->GetTaskContextData(token, &aov))
    {
        if (aov.IsHolding<HgiTextureHandle>())
        {
            aovTexture = aov.Get<HgiTextureHandle>();
        }
    }

    return aovTexture;
}

bool RenderBufferManager::AovsSupported() const
{
    return _pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer);
}

const AovParams& RenderBufferManager::GetAovParamCache() const
{
    return _impl->GetAovParamCache();
}

TfToken const& RenderBufferManager::GetViewportAov() const
{
    return _impl->GetViewportAov();
}

GfVec2i const& RenderBufferManager::GetRenderBufferSize() const
{
    return _impl->GetRenderBufferSize();
}

HdRenderBuffer* RenderBufferManager::GetRenderOutput(const TfToken& name)
{
    return _impl->GetRenderOutput(name, _taskManagerUid);
}

void RenderBufferManager::SetBufferSizeAndMsaa(
    const GfVec2i& size, size_t msaaSampleCount, bool msaaEnabled)
{
    _impl->SetBufferSizeAndMsaa(size, msaaSampleCount, msaaEnabled);
}

void RenderBufferManager::SetRenderOutputClearColor(const TfToken& name, const VtValue& clearValue)
{
    _impl->SetRenderOutputClearColor(name, _taskManagerUid, clearValue);
}

bool RenderBufferManager::SetRenderOutputs(TfTokenVector const& names,
    RenderBufferBindings const& inputs, GfVec4d const& viewport)
{
    return _impl->SetRenderOutputs(names, inputs, viewport, _taskManagerUid);
}

void RenderBufferManager::SetPresentationOutput(TfToken const& api, VtValue const& framebuffer)
{
    _impl->SetPresentationOutput(api, framebuffer);
}

} // namespace hvt
