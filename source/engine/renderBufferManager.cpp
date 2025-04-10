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
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>
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

namespace
{

template <typename T>
T GetParameter(const SyncDelegatePtr& syncDelegate, SdfPath const& id, TfToken const& key)
{
    VtValue vParams = syncDelegate->GetValue(id, key);
    return vParams.Get<T>();
}

GfVec2i ViewportToAovDimensions(const GfVec4d& viewport)
{
    // Note from HdxTaskController
    // Ignore the viewport offset and use its size as the aov size.
    // XXX: This is fragile and doesn't handle viewport tricks,
    // such as camera zoom. In the future, we expect to improve the
    // API to better communicate AOV sizing, fill region and camera
    // zoom.
    return GfVec2i(viewport[2], viewport[3]);
}

} // namespace

/// The Impl is derived from HdxTaskController. The Impl consolidates
/// the render-buffer related operations which were originally in the Task Controller.
///
/// The implementation is private and hidden for now. This will evolve. Modify at your own risks.
class RenderBufferManager::Impl : public RenderBufferSettingsProvider
{
public:
    explicit Impl(HdRenderIndex* pRenderIndex);
    ~Impl();

    Impl(Impl const&)            = delete;
    Impl& operator=(Impl const&) = delete;

    void CleanUp();

    /// Update the data associated with the resolution:
    ///  AOVManager::_renderBufferSize
    ///  HdRenderBufferDescriptor delegate data for each HdRenderBufferDescriptor bprim
    /// Note: UpdateAovBufferDescriptor replaces UpdateRenderBufferSize()
    void UpdateAovBufferDescriptor(const GfVec2i& newRenderBufferSize,
        HdChangeTracker& changeTracker, SyncDelegatePtr& aovDelegate);

    /// UpdateBufferSizeAndMsaa: equivalent to calling UpdateAovBufferDescriptor and
    /// SetMultisampleState.
    ///
    /// Calling UpdateAovBufferDescriptor and SetMultisampleState separately causes redundant
    /// changes and checks. Changing both at the same time makes more sense and requires less
    /// operations.
    void UpdateBufferSizeAndMsaa(const GfVec2i newRenderBufferSize, size_t msaaSampleCount,
        bool enableMultisampling, HdChangeTracker& changeTracker, SyncDelegatePtr& aovDelegate);

    void SetMultisampleState(const size_t& msaaSampleCount, bool enableMultisampling,
        HdChangeTracker& changeTracker, SyncDelegatePtr& aovDelegate);

    HdRenderBuffer* GetRenderOutput(const TfToken& name, const SdfPath& controllerId);

    /// SetRenderOutputs: changes performed by this function:
    ///  AOVManager::_aovOutputs
    ///  AOVManager::_aovInputs
    ///  AOVManager::_aovBufferIds
    ///  AOVManager::_viewportAov
    ///  pRenderIndex->InsertBprim
    ///  pRenderIndex->GetChangeTracker().MarkBprimDirty
    ///  aovDelegate->SetValue(aovId, _tokens->renderBufferDescriptor,desc);
    ///  aovDelegate->SetValue(aovId, HdStRenderBufferTokens->stormMsaaSampleCount,
    ///  desc.multiSampled ? _msaaSampleCount : 1); aovDelegate->SetValue(aovInputTaskId,
    ///  HdTokens->params, AovInputTaskParams{ buffer paths and HdRenderBuffer*} );
    ///  renderTaskDelegate.SetParameter(renderTaskId, HdTokens->params, HdxRenderTaskParams{
    ///  aovBindings } ); //HdxRenderTaskParams::aovBindings are changed
    bool SetRenderOutputs(const TfTokenVector& names,
        const std::vector<std::pair<const TfToken&, HdRenderBuffer*>>& inputs,
        SyncDelegatePtr& aovDelegate, SyncDelegatePtr& renderTaskDelegate, const GfVec4d& viewport,
        const SdfPathVector& renderTaskIds, const SdfPath& controllerId,
        const SdfPath& aovInputTaskId);

    // This function is the inner part of SetRenderOutputSettings, it applies on
    // RenderBufferDescriptors, but not on RenderTaskParams.
    void SetRenderBufferOutputSettings(
        const SdfPath& renderBufferId, const HdAovDescriptor& desc, SyncDelegatePtr& aovDelegate);

    // This function updates the render output settings, both in the
    // RenderTaskParams and the RenderBufferDescriptors.
    //
    // Concretely, this function only updates a subset of settings for the RenderTaskParams:
    //  - RenderTaskParams::aovBindings[i].clearValue
    //  - RenderTaskParams::aovBindings[i].aovBindings
    //
    // Also, the RenderBufferDescriptor for Bprims named {controllerID+name}:
    //  - HdRenderBufferDescriptor.format
    //  - HdRenderBufferDescriptor.multiSampled
    void SetRenderOutputSettings(const TfToken& name, const HdAovDescriptor& desc,
        SyncDelegatePtr& aovDelegate, SyncDelegatePtr& renderTaskDelegate,
        const SdfPathVector& renderTaskIds, const SdfPath& controllerId);

    // This function is needed to update the clearValue when using RenderTask CommitTaskFn
    //
    // Behind this function is a deferred mechanism, where new values are stored in a cache
    // (see _aovTaskCache) and updated later, when the RenderTask CommitTaskFn is called,
    // at the start of TaskManager::Execute().
    void SetRenderOutputClearColor(
        const TfToken& name, const SdfPath& controllerId, const VtValue& clearValue);

    // Updates the clearValue and aovSettings of RenderTaskParams::aovBindings.
    //
    // The code of this function was simply extracted from the lower render task param loop
    // in HdxTaskController::SetRenderOutputSettings. This function is called by
    // Impl::SetRenderOutputSettings, and it COULD also be used by the
    // RenderTask CommitTaskFn if we wanted to update the aovSettings. However, it turns out we only
    // seem to update the RenderTaskParams::aovBindings clear value in the FramePass, so it does
    // not need to be called from CommitTaskFn for now.
    static bool UpdateRenderTaskAovOutputSettings(HdRenderPassAovBindingVector& aovBindings,
        const SdfPath& renderBufferId, const HdAovDescriptor& desc, const bool isFirstRenderTask);

    HdAovDescriptor GetRenderOutputSettings(const TfToken& name, const SyncDelegatePtr& aovDelegate,
        const SyncDelegatePtr& renderTaskDelegate, const SdfPath& frontRenderTaskId,
        const SdfPath& controllerId) const;

    void SetViewportRenderOutput(const TfToken& name, HdRenderBuffer* aovBuffer,
        HdRenderBuffer* depthBuffer, SyncDelegatePtr& aovDelegate, const SdfPath& controllerId,
        const SdfPath& aovInputTaskId);

    /// Set the framebuffer to present the render to.
    void SetPresentationOutput(TfToken const& api, VtValue const& framebuffer)
    {
        _aovTaskCache.presentApi         = api;
        _aovTaskCache.presentFramebuffer = framebuffer;
    }

    TfToken const& GetViewportAov() const override { return _viewportAov; }

    // Simple state queries
    bool AovsSupported() const override;
    bool UsingAovs() const override;
    GfVec2i const& GetRenderBufferSize() const override;

    static SdfPath GetAovPath(const SdfPath& controllerID, const TfToken& aov);

    const AovParams& GetAovParamCache() const override { return _aovTaskCache; }

private:
    void UpdateAovMSAASampleCount(
        HdChangeTracker& changeTracker, SyncDelegatePtr& aovDelegate) const;

    GfVec2i _renderBufferSize;

    /// Multisampling enabled or not.
    bool _enableMultisampling { true };

    /// Number of samples for multisampling.
    size_t _msaaSampleCount { 4 };

    /// _aovBufferIds: list of Bprim IDs. These IDs are used to:
    ///    1) Insert and remove Bprims from the RenderIndex, e.g.:
    ///        pRenderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, _aovBufferIds[0]
    ///        pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, id)
    ///       ANSME: Is pRenderIndex->GetBprim always returning
    ///    2) Get Bprim from the renderIndex, e.g.
    ///        pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer,params.aovBufferPath));
    ///    3) Get and Set parametes in the ParameterDelegate, e.g.
    ///        aovDelegate.param[bufferID, tokens::stormMsaaSampleCount]
    ///        HdRenderBufferDescriptor aovDelegate.param[bufferID, tokens::renderBufferDescriptor]
    /// Question: are these IDs the same as in AovInputTaskParams::aovBufferPath ?
    SdfPathVector _aovBufferIds;

    /// _aovOutputs is only used by SetRenderOutputs, to check if outputs have changed before
    /// recreating Bprims render buffers, updating HdxRenderTaskParams aovBindings, etc.
    TfTokenVector _aovOutputs;

    /// Only used by SetRenderOutputs, for checking if inputs have changed since the last call.
    std::vector<std::pair<const TfToken&, HdRenderBuffer*>> _aovInputs;

    /// Used as a cache to prevent unnecessary execution or dirty states in
    /// SetViewportRenderOutput.
    TfToken _viewportAov;

    AovParams _aovTaskCache;

    HdRenderIndex* _pRenderIndex;
};

RenderBufferManager::Impl::Impl(HdRenderIndex* pRenderIndex) :
    _renderBufferSize(0, 0), _pRenderIndex(pRenderIndex)
{
    _aovTaskCache.presentApi = HgiTokens->OpenGL;
}

RenderBufferManager::Impl::~Impl()
{
    CleanUp();
}

bool RenderBufferManager::Impl::AovsSupported() const
{
    return _pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer);
}

bool RenderBufferManager::Impl::UsingAovs() const
{
    return !_aovBufferIds.empty();
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
    const std::vector<std::pair<const TfToken&, HdRenderBuffer*>>& inputs,
    SyncDelegatePtr& aovDelegate, SyncDelegatePtr& renderTaskDelegate, const GfVec4d& viewport,
    const SdfPathVector& renderTaskIds, const SdfPath& controllerId, const SdfPath& aovInputTaskId)
{
    if (!AovsSupported())
    {
        return false;
    }

    const bool enableProgressive { TfGetenvBool("AGP_ENABLE_PROGRESSIVE_RENDERING", false) };

    if (!enableProgressive && _aovOutputs == outputs && inputs.size() == _aovInputs.size() &&
        std::equal(inputs.begin(), inputs.end(), _aovInputs.begin(), _aovInputs.end()))
    {
        return false;
    }

    // If progressive rendering is enabled, render buffer clear is only required when `_aovOutputs
    // != outputs`.
    bool needClear = !enableProgressive || _aovOutputs != outputs;

    // At this point, needClear = true and outputs={color}
    _aovOutputs = outputs;

    if (inputs.size() > 0)
    {
        _aovInputs.clear();
        std::copy(inputs.begin(), inputs.end(), back_inserter(_aovInputs));
        _viewportAov = TfToken(); // clear the viewport AOV as we may have new inputs
    }

    // A function could be used to get localOutputs, this is the only output for this function
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

    // This will delete the BPrim from the RenderIndex and clear _aovBufferIds SdfPathVector
    // Delete the old renderbuffers.
    if (needClear)
    {
        for (size_t i = 0; i < _aovBufferIds.size(); ++i)
        {
            _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, _aovBufferIds[i]);
        }
        _aovBufferIds.clear();
    }

    // This only calcs the dim3. (the 2D version isn't used later)
    // Get the render buffer dimensions.
    const GfVec2i dimensions =
        _renderBufferSize != GfVec2i(0) ? _renderBufferSize : ViewportToAovDimensions(viewport);

    const GfVec3i dimensions3(dimensions[0], dimensions[1], 1);

    // This gets the default descriptors for each AOV token. E.g. color:HdFormatFloat16Vec4,
    // depth:HdFormatFloat32
    // Get default AOV descriptors from the render delegate.
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

    // Add the new renderbuffers. _GetAovPath returns ids of the form
    // {controller_id}/aov_{name}.
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
            _pRenderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, aovDelegate.get(), aovId);
            HdRenderBufferDescriptor desc;
            desc.dimensions   = dimensions3;
            desc.format       = outputDescs[i].format;
            desc.multiSampled = _enableMultisampling;
            aovDelegate->SetValue(aovId, _tokens->renderBufferDescriptor, VtValue(desc));
            aovDelegate->SetValue(aovId, HdStRenderBufferTokens->stormMsaaSampleCount,
                VtValue(desc.multiSampled ? _msaaSampleCount : 1));
            _pRenderIndex->GetChangeTracker().MarkBprimDirty(
                aovId, HdRenderBuffer::DirtyDescription);
            _aovBufferIds.push_back(aovId);
        }
    }

    // This section only fills the 3 vectors below: aovBindingsClear, aovBindingsNoClear,
    // aovInputBindings
    // Create the list of AOV bindings.
    // Only the first render task clears AOVs so we also have a bindings set
    // that specifies no clear color for the remaining render tasks.
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

    const SdfPath volumeId =
        GetRenderTaskPath(controllerId, HdStMaterialTagTokens->volume);

    // Set AOV bindings on render tasks
    for (const SdfPath& renderTaskId : renderTaskIds)
    {
        const bool isFirstRenderTask = enableProgressive
            ? (inputs.size() == 0) && (renderTaskId == renderTaskIds.front())
            : renderTaskId == renderTaskIds.front();

        const HdRenderPassAovBindingVector& aovBindings =
            isFirstRenderTask ? aovBindingsClear : aovBindingsNoClear;

        // Note: Get Param / set Param sandwich below. We can see the input bindings parameters are
        // changed. It is still not very clear to me what this is used for, however. I'd need to
        // check with more details what the render task aovBindings params are used for.

        HdxRenderTaskParams rParams =
            GetParameter<HdxRenderTaskParams>(renderTaskDelegate, renderTaskId, HdTokens->params);

        rParams.aovBindings = aovBindings;
        if (renderTaskId == volumeId)
        {
            rParams.aovInputBindings = aovInputBindings;
        }

        renderTaskDelegate->SetValue(renderTaskId, HdTokens->params, VtValue(rParams));
        _pRenderIndex->GetChangeTracker().MarkTaskDirty(renderTaskId, HdChangeTracker::DirtyParams);
    }

    // For AOV visualization, if only one output was specified, send it
    // to the viewer; otherwise, disable colorization.
    // DelegateAdaptor delegateAdaptor(aovDelegate);
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
        SetViewportRenderOutput(
            outputs[0], firstInput, depthInput, aovDelegate, controllerId, aovInputTaskId);
    }
    else
    {
        // TODO: Here, I guess it is expected the function won't do much. Maybe this should be
        // clarified:
        //       - The previous version have default nullptr arguments
        //       - The new version has more parameters, which might not support being null (risk of
        //       crash?) Try to clarify which parameter is okay to set to null, and which is not.
        //       Reorder parameters and use default values if that makes more sense (probable).
        SetViewportRenderOutput(
            TfToken(), nullptr, nullptr, aovDelegate, controllerId, aovInputTaskId);
    }

    // XXX: The viewport data plumbed to tasks unfortunately depends on whether
    // aovs are being used.
    return true;
}

void RenderBufferManager::Impl::SetViewportRenderOutput(TfToken const& name,
    HdRenderBuffer* aovBuffer, HdRenderBuffer* depthBuffer, SyncDelegatePtr& aovDelegate,
    const SdfPath& controllerId, const SdfPath& aovInputTaskId)
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

    // Legacy SyncDelegate use case : set the AovInputTaskParams directly
    if (!aovInputTaskId.IsEmpty())
    {
        AovInputTaskParams params;
        if (name.IsEmpty())
        {
            params.aovBufferPath   = SdfPath::EmptyPath();
            params.depthBufferPath = SdfPath::EmptyPath();
            params.aovBuffer       = nullptr;
            params.depthBuffer     = nullptr;
        }
        else if (name == HdAovTokens->color)
        {
            // Typically, this condition is true
            params.aovBufferPath   = GetAovPath(controllerId, HdAovTokens->color);
            params.depthBufferPath = GetAovPath(controllerId, HdAovTokens->depth);
            params.aovBuffer       = aovBuffer
                      ? aovBuffer
                      : static_cast<HdRenderBuffer*>(_pRenderIndex->GetBprim(
                      HdPrimTypeTokens->renderBuffer, params.aovBufferPath));

            params.depthBuffer = depthBuffer
                ? depthBuffer
                : static_cast<HdRenderBuffer*>(_pRenderIndex->GetBprim(
                      HdPrimTypeTokens->renderBuffer, params.depthBufferPath));
        }
        else
        {
            // When visualizing a buffer other than color, this condition is executed
            params.aovBufferPath   = GetAovPath(controllerId, name);
            params.depthBufferPath = SdfPath::EmptyPath();
            params.aovBuffer       = aovBuffer ? aovBuffer : GetRenderOutput(name, controllerId);
            params.depthBuffer     = nullptr;
        }

        aovDelegate->SetValue(aovInputTaskId, HdTokens->params, VtValue(params));
        _pRenderIndex->GetChangeTracker().MarkTaskDirty(
            aovInputTaskId, HdChangeTracker::DirtyParams);
    }
    // CommitTaskFn use case: store the values in the manager, retrieve them later.
    else
    {
        if (name.IsEmpty())
        {
            _aovTaskCache.aovBufferPath   = SdfPath::EmptyPath();
            _aovTaskCache.depthBufferPath = SdfPath::EmptyPath();
            _aovTaskCache.aovBuffer       = nullptr;
            _aovTaskCache.depthBuffer     = nullptr;
        }
        else if (name == HdAovTokens->color)
        {
            // Typically, this condition is true
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
            // When visualizing a buffer other than color, this condition is executed
            _aovTaskCache.aovBufferPath   = GetAovPath(controllerId, name);
            _aovTaskCache.depthBufferPath = SdfPath::EmptyPath();
            _aovTaskCache.aovBuffer   = aovBuffer ? aovBuffer : GetRenderOutput(name, controllerId);
            _aovTaskCache.depthBuffer = nullptr;
        }
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

void RenderBufferManager::Impl::SetRenderOutputSettings(const TfToken& name,
    const HdAovDescriptor& desc, SyncDelegatePtr& aovDelegate, SyncDelegatePtr& renderTaskDelegate,
    const SdfPathVector& renderTaskIds, const SdfPath& controllerId)
{
    if (!AovsSupported() || renderTaskIds.empty())
    {
        return;
    }

    // Check if we're setting a value for a nonexistent AOV.
    SdfPath renderBufferId = GetAovPath(controllerId, name);
    SetRenderBufferOutputSettings(renderBufferId, desc, aovDelegate);

    for (const SdfPath& renderTaskId : renderTaskIds)
    {
        HdxRenderTaskParams renderParams =
            GetParameter<HdxRenderTaskParams>(renderTaskDelegate, renderTaskId, HdTokens->params);

        const bool isFirstRenderTask = renderTaskId == renderTaskIds.front();
        if (UpdateRenderTaskAovOutputSettings(
                renderParams.aovBindings, renderBufferId, desc, isFirstRenderTask))
        {
            renderTaskDelegate->SetValue(renderTaskId, HdTokens->params, VtValue(renderParams));
            _pRenderIndex->GetChangeTracker().MarkTaskDirty(
                renderTaskId, HdChangeTracker::DirtyParams);
        }
    }
}

void RenderBufferManager::Impl::SetRenderBufferOutputSettings(
    const SdfPath& renderBufferId, const HdAovDescriptor& desc, SyncDelegatePtr& aovDelegate)
{
    if (!AovsSupported())
    {
        return;
    }

    // Check if we're setting a value for a nonexistent AOV.
    if (!aovDelegate->HasValue(renderBufferId, _tokens->renderBufferDescriptor))
    {
        TF_WARN("Render output %s doesn't exist", renderBufferId.GetText());
        return;
    }

    // HdAovDescriptor contains data for both the renderbuffer descriptor,
    // and the renderpass aov binding.  Update them both.
    HdRenderBufferDescriptor rbDesc = GetParameter<HdRenderBufferDescriptor>(
        aovDelegate, renderBufferId, _tokens->renderBufferDescriptor);

    if (rbDesc.format != desc.format || rbDesc.multiSampled != desc.multiSampled)
    {
        rbDesc.format       = desc.format;
        rbDesc.multiSampled = desc.multiSampled;
        aovDelegate->SetValue(renderBufferId, HdStRenderBufferTokens->stormMsaaSampleCount,
            VtValue(rbDesc.multiSampled ? _msaaSampleCount : 1));
        aovDelegate->SetValue(renderBufferId, _tokens->renderBufferDescriptor, VtValue(rbDesc));
        _pRenderIndex->GetChangeTracker().MarkBprimDirty(
            renderBufferId, HdRenderBuffer::DirtyDescription);
    }
}

// This stores the clear color, and assigns it later in the RenderTask CommitTaskFn
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

HdAovDescriptor RenderBufferManager::Impl::GetRenderOutputSettings(const TfToken& name,
    const SyncDelegatePtr& aovDelegate, const SyncDelegatePtr& renderTaskDelegate,
    const SdfPath& frontRenderTaskId, const SdfPath& controllerId) const
{
    if (!AovsSupported())
    {
        return HdAovDescriptor();
    }

    // Check if we're getting a value for a nonexistent AOV.
    SdfPath renderBufferId = GetAovPath(controllerId, name);
    if (!aovDelegate->HasValue(renderBufferId, _tokens->renderBufferDescriptor))
    {
        return HdAovDescriptor();
    }

    HdRenderBufferDescriptor rbDesc = GetParameter<HdRenderBufferDescriptor>(
        aovDelegate, renderBufferId, _tokens->renderBufferDescriptor);

    HdAovDescriptor desc;
    desc.format       = rbDesc.format;
    desc.multiSampled = rbDesc.multiSampled;

    const SdfPath& renderTaskId = frontRenderTaskId;

    HdxRenderTaskParams renderParams =
        GetParameter<HdxRenderTaskParams>(renderTaskDelegate, renderTaskId, HdTokens->params);

    for (size_t i = 0; i < renderParams.aovBindings.size(); ++i)
    {
        if (renderParams.aovBindings[i].renderBufferId == renderBufferId)
        {
            desc.clearValue  = renderParams.aovBindings[i].clearValue;
            desc.aovSettings = renderParams.aovBindings[i].aovSettings;
            break;
        }
    }

    return desc;
}

void RenderBufferManager::RenderBufferManager::Impl::UpdateAovBufferDescriptor(
    const GfVec2i& newRenderBufferSize, HdChangeTracker& changeTracker,
    SyncDelegatePtr& aovDelegate)
{
    _renderBufferSize = newRenderBufferSize;
    const GfVec3i dimensions3(_renderBufferSize[0], _renderBufferSize[1], 1);

    for (auto const& id : _aovBufferIds)
    {
        HdRenderBufferDescriptor desc = GetParameter<HdRenderBufferDescriptor>(
            aovDelegate, id, _tokens->renderBufferDescriptor);
        if (desc.dimensions != dimensions3 || desc.multiSampled != _enableMultisampling)
        {
            desc.dimensions   = dimensions3;
            desc.multiSampled = _enableMultisampling;
            aovDelegate->SetValue(id, _tokens->renderBufferDescriptor, VtValue(desc));
            changeTracker.MarkBprimDirty(id, HdRenderBuffer::DirtyDescription);
        }
    }
}

void RenderBufferManager::Impl::UpdateAovMSAASampleCount(
    HdChangeTracker& changeTracker, SyncDelegatePtr& aovDelegate) const
{
    for (auto const& id : _aovBufferIds)
    {
        aovDelegate->SetValue(
            id, HdStRenderBufferTokens->stormMsaaSampleCount, VtValue(_msaaSampleCount));
        changeTracker.MarkBprimDirty(id, HdRenderBuffer::DirtyDescription);
    }
}

void RenderBufferManager::Impl::UpdateBufferSizeAndMsaa(const GfVec2i newRenderBufferSize,
    size_t msaaSampleCount, bool enableMultisampling, HdChangeTracker& changeTracker,
    SyncDelegatePtr& aovDelegate)
{
    _renderBufferSize    = newRenderBufferSize;
    _enableMultisampling = enableMultisampling;

    // Note: There is a condition inside UpdateAovBufferDescriptor to verify if the
    //       RenderBufferSize or multi sampling state has changed in the buffer descriptor.
    //       We could perhaps add an external check here too, to avoid having to always fetch each
    //       buffer descriptor when nothing changed, but for now the logic will be kept this way,
    //       since this is how it works in the HdxTaskController.
    UpdateAovBufferDescriptor(_renderBufferSize, changeTracker, aovDelegate);

    if (_msaaSampleCount != msaaSampleCount)
    {
        _msaaSampleCount = msaaSampleCount;
        UpdateAovMSAASampleCount(changeTracker, aovDelegate);
    }
}

void RenderBufferManager::Impl::SetMultisampleState(const size_t& msaaSampleCount,
    bool enableMultisampling, HdChangeTracker& changeTracker, SyncDelegatePtr& aovDelegate)
{
    if (_enableMultisampling != enableMultisampling)
    {
        _enableMultisampling = enableMultisampling;
        UpdateAovBufferDescriptor(_renderBufferSize, changeTracker, aovDelegate);
    }

    if (_msaaSampleCount != msaaSampleCount)
    {
        _msaaSampleCount = msaaSampleCount;
        UpdateAovMSAASampleCount(changeTracker, aovDelegate);
    }
}

void RenderBufferManager::Impl::CleanUp()
{
    for (auto const& id : _aovBufferIds)
    {
        _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, id);
    }
}

bool RenderBufferManager::Impl::UpdateRenderTaskAovOutputSettings(
    HdRenderPassAovBindingVector& aovBindings, const SdfPath& renderBufferId,
    const HdAovDescriptor& desc, const bool isFirstRenderTask)
{
    for (size_t i = 0; i < aovBindings.size(); ++i)
    {
        if (aovBindings[i].renderBufferId == renderBufferId)
        {
            if (aovBindings[i].clearValue != desc.clearValue ||
                aovBindings[i].aovSettings != desc.aovSettings)
            {
                // Only the first RenderTask should clear the AOV
                aovBindings[i].clearValue  = isFirstRenderTask ? desc.clearValue : VtValue();
                aovBindings[i].aovSettings = desc.aovSettings;
                return true;
            }
            break;
        }
    }
    return false;
}

RenderBufferManager::RenderBufferManager(
    SdfPath const& taskManagerUid, HdRenderIndex* pRenderIndex, SyncDelegatePtr& syncDelegate) :
    _taskManagerUid(taskManagerUid), _pRenderIndex(pRenderIndex), _syncDelegate(syncDelegate)
{
    _impl = std::make_unique<Impl>(pRenderIndex);
}

RenderBufferManager::~RenderBufferManager()
{
    _impl->CleanUp();
}

void RenderBufferManager::SetRenderBufferDimensions(GfVec2i const& size)
{
    _size = size;

    UpdateAovBufferDescriptor(_size);
}

HgiTextureHandle RenderBufferManager::GetAovTexture(TfToken const& token, HdEngine* engine) const
{
    VtValue aov;
    HgiTextureHandle aovTexture;

    // Note: The Metal only implementation needs an access to "id<MTLTexture>" that
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

bool RenderBufferManager::UsingAovs() const
{
    return _impl->UsingAovs();
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

void RenderBufferManager::SetRenderOutputClearColor(const TfToken& name, const VtValue& clearValue)
{
    _impl->SetRenderOutputClearColor(name, _taskManagerUid, clearValue);
}

void RenderBufferManager::UpdateAovBufferDescriptor(GfVec2i const& size)
{
    _size = size;

    _impl->UpdateAovBufferDescriptor(size, _pRenderIndex->GetChangeTracker(), _syncDelegate);
}

bool RenderBufferManager::SetRenderOutputs(const TfTokenVector& names,
    const std::vector<std::pair<const TfToken&, HdRenderBuffer*>>& inputs, const GfVec4d& viewport)
{
    return _impl->SetRenderOutputs(names, inputs, _syncDelegate, _syncDelegate, viewport, {},
        _taskManagerUid, SdfPath::EmptyPath());
}

void RenderBufferManager::SetViewportRenderOutput(
    const TfToken& name, HdRenderBuffer* aovBuffer, HdRenderBuffer* depthBuffer)
{
    _impl->SetViewportRenderOutput(
        name, aovBuffer, depthBuffer, _syncDelegate, _taskManagerUid, SdfPath::EmptyPath());
}

void RenderBufferManager::SetPresentationOutput(TfToken const& api, VtValue const& framebuffer)
{
    _impl->SetPresentationOutput(api, framebuffer);
}

void RenderBufferManager::SetMultisampleState(size_t msaaSampleCount, bool enableMultisampling)
{
    _impl->SetMultisampleState(
        msaaSampleCount, enableMultisampling, _pRenderIndex->GetChangeTracker(), _syncDelegate);
}

} // namespace hvt
