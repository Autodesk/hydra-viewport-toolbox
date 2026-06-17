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

#include "renderBufferManagerSDImpl.h"

#include <hvt/engine/engine.h>
#include <hvt/engine/hgiInstance.h>
#include <hvt/engine/taskUtils.h>
#include <hvt/tasks/aovInputTask.h>
#include <hvt/tasks/resources.h>

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
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/sdf/path.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
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

namespace HVT_NS
{

namespace
{

Hgi* GetHgi(HdRenderIndex const* renderIndex)
{
    Hgi* hgi = hvt::HgiInstance::instance().hgi();
    if (hgi)
        return hgi;

    // If it wasn't created by the HgiInstance look for it on the render index.
    HdDriverVector const& drivers = renderIndex->GetDrivers();
    for (HdDriver* hdDriver : drivers)
    {
        if ((hdDriver->name == HgiTokens->renderDriver) && hdDriver->driver.IsHolding<Hgi*>())
        {
            hgi = hdDriver->driver.UncheckedGet<Hgi*>();
            if (hgi)
                return hgi;
        }
    }

    return nullptr;
}

} // anonymous namespace

RenderBufferManagerSDImpl::RenderBufferManagerSDImpl(
    HdRenderIndex* pRenderIndex, SyncDelegatePtr& syncDelegate) :
    _renderBufferSize(0, 0), _pRenderIndex(pRenderIndex), _syncDelegate(syncDelegate)
{
    _presentParams.api             = HgiTokens->OpenGL;
    _isProgressiveRenderingEnabled = { TfGetenvBool("AGP_ENABLE_PROGRESSIVE_RENDERING", false) };
}

RenderBufferManagerSDImpl::~RenderBufferManagerSDImpl()
{
    for (auto const& id : _aovBufferIds)
    {
        _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, id);
    }
}

bool RenderBufferManagerSDImpl::IsAovSupported() const
{
    return _pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer);
}

GfVec2i const& RenderBufferManagerSDImpl::GetRenderBufferSize() const
{
    return _renderBufferSize;
}

void RenderBufferManagerSDImpl::PrepareBuffersFromInputs(RenderBufferBinding const& colorInputAov,
    RenderBufferBinding const& depthInputAov, HdRenderBufferDescriptor const& desc,
    SdfPath const& controllerId)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HgiTextureHandle colorInput = colorInputAov.texture;
    HgiTextureHandle depthInput = depthInputAov.texture;

    if (!colorInput)
    {
        return;
    }

    const SdfPath aovPath = GetAovPath(controllerId, colorInputAov.aovName);
    // Get the buffer that the renderer will draw into from the render index.
    HdRenderBuffer* colorBuffer = static_cast<HdRenderBuffer*>(
        _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovPath));

    // If there is no color buffer in this render index it was determined that the color buffer
    // to write into should come from the input buffer from the previous pass.
    if (!colorBuffer)
    {
        // Use the input color buffer
        colorBuffer = colorInputAov.buffer;
    }
    else
    {
        if (!colorBuffer->IsMapped())
        {
            // This might be a newly created BPrim.  Allocate the GPU texture if needed.
            colorBuffer->Allocate(desc.dimensions, desc.format, desc.multiSampled);
        }
    }

    HgiTextureHandle colorOutput;
    VtValue colorOutputValue = colorBuffer->GetResource(desc.multiSampled);
    if (colorOutputValue.IsHolding<HgiTextureHandle>())
    {
        colorOutput = colorOutputValue.Get<HgiTextureHandle>();
        if (!colorOutput)
        {
            TF_CODING_ERROR("The output render buffer does not have a valid texture %s.",
                colorInputAov.aovName.GetText());
            return;
        }
    }
    else
    {
        // The output render buffer is not holding a writeable buffer.
        // You will need to composite to blend passes results.
        return;
    }

    // If the input and output are the same texture, no need to copy.
    if (colorOutput == colorInput)
        return;

    // Get the depth texture handle from the input depth buffer.
    HgiTextureHandle depthOutput;
    if (depthInput)
    {
        const SdfPath aovDepthPath = GetAovPath(controllerId, pxr::HdAovTokens->depth);

        // Get the buffer that the renderer will draw into from the render index.
        HdRenderBuffer* depthBuffer = static_cast<HdRenderBuffer*>(
            _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovDepthPath));

        // If there is no depth buffer in this render index it was determined that the depth buffer
        // to write into should come from the input buffer from the previous pass.
        if (!depthBuffer)
        {
            // Use the input color buffer
            depthBuffer = depthInputAov.buffer;
        }
        else
        {
            if (!depthBuffer->IsMapped())
            {
                // This might be a newly created BPrim.  Allocate the GPU texture if needed.
                depthBuffer->Allocate(desc.dimensions, HdFormatFloat32, desc.multiSampled);
            }
        }

        if (depthBuffer)
        {
            VtValue depthOutputValue = depthBuffer->GetResource(desc.multiSampled);
            if (depthOutputValue.IsHolding<HgiTextureHandle>())
            {
                if (depthBuffer)
                    depthOutput = depthOutputValue.Get<HgiTextureHandle>();

                if (!depthOutput)
                {
                    TF_CODING_ERROR("The output render buffer does not have a valid texture %s.",
                        aovDepthPath.GetName().c_str());
                    return;
                }
            }
            else
            {
                // The output render buffer is not holding a writeable buffer.
                // You will need to composite to blend passes results.
                return;
            }
        }
    }

    Hgi* hgi = GetHgi(_pRenderIndex);
    if (!hgi)
    {
        TF_CODING_ERROR("There is no valid Hgi driver.");
        return;
    }

    // Initialize the shader that will copy the contents from the input to the output.
    if (!_copyColorShader)
    {
        _copyColorShader = std::make_unique<HdxFullscreenShader>(hgi, "Copy Color Buffer");
    }

    // Initialize the shader that will copy the contents from the input to the output.
    if (!_copyColorShaderNoDepth)
    {
        _copyColorShaderNoDepth =
            std::make_unique<HdxFullscreenShader>(hgi, "Copy Color Buffer No Depth");
    }

    HdxFullscreenShader* shader =
        (!depthInput ? _copyColorShaderNoDepth.get() : _copyColorShader.get());

    // Submit the layout change to read from the textures.
    colorInput->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    if (!depthInput)
    {
        shader->BindTextures({ colorInput });
        shader->Draw(colorOutput, HgiTextureHandle());
    }
    else
    {
        depthInput->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
        shader->BindTextures({ colorInput, depthInput });
        shader->Draw(colorOutput, depthOutput);
        depthInput->SubmitLayoutChange(HgiTextureUsageBitsDepthTarget);
    }

    colorInput->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);
}

// The code does not use the HdxFullscreenShader helper here because it only needs to copy the depth
// AOVs and HdxFullscreenShader always needs the color AOVs.
void RenderBufferManagerSDImpl::PrepareDepthOnlyFromInput(RenderBufferBinding const& inputDepthAov,
    HdRenderBufferDescriptor const& desc, SdfPath const& controllerId)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HgiTextureHandle input = inputDepthAov.texture;

    if (!input)
    {
        return;
    }

    const SdfPath aovPath = GetAovPath(controllerId, inputDepthAov.aovName);
    // Get the buffer that the renderer will draw into from the render index.
    HdRenderBuffer* buffer = static_cast<HdRenderBuffer*>(
        _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovPath));

    // If there is no buffer in this render index it was determined that the buffer
    // to write into should come from the input buffer from the previous pass.
    if (!buffer)
    {
        // Use the input buffer
        buffer = inputDepthAov.buffer;
    }
    else
    {
        if (!buffer->IsMapped())
        {
            // This might be a newly created BPrim.  Allocate the GPU texture if needed.
            buffer->Allocate(desc.dimensions, desc.format, desc.multiSampled);
        }
    }

    HgiTextureHandle output;
    VtValue outputValue = buffer->GetResource(desc.multiSampled);
    if (outputValue.IsHolding<HgiTextureHandle>())
    {
        output = outputValue.Get<HgiTextureHandle>();
        if (!output)
        {
            TF_CODING_ERROR("The output render buffer does not have a valid texture %s.",
                inputDepthAov.aovName.GetText());
            return;
        }
    }
    else
    {
        // The output render buffer is not holding a writeable buffer.
        // You will need to composite to blend passes results.
        return;
    }

    // If the input and output are the same texture, no need to copy.
    if (output == input)
    {
        return;
    }

    Hgi* hgi = GetHgi(_pRenderIndex);
    if (!hgi)
    {
        TF_CODING_ERROR("There is no valid Hgi driver.");
        return;
    }

    // Note: HdxFullscreenShader must include the color AOV so it cannot be used here
    // because the code only needs to copy the depth AOV.

    if (!_copyDepthShader)
    {
        _copyDepthShader = std::make_unique<CopyDepthShader>(hgi);
    }

    // Copy the input to the output texture.
    _copyDepthShader->Execute(input, output);
}

bool RenderBufferManagerSDImpl::SetRenderOutputs(TfToken const& outputToVisualize,
    TfTokenVector const& outputs, RenderBufferBindings const& inputs, GfVec4d const& viewport,
    SdfPath const& controllerId)
{
    bool hasRemovedBuffers = false;

    if (!IsAovSupported())
    {
        return false;
    }

    bool somethingChanged = true;
    // If progressive rendering is enabled, do not return early.
    if (!_isProgressiveRenderingEnabled)
    {
        // Check if cached inputs and outputs have changed.
        if (_aovOutputs == outputs && inputs.size() == _aovInputs.size() &&
            std::equal(inputs.begin(), inputs.end(), _aovInputs.begin(), _aovInputs.end()))
        {
            somethingChanged = false;
        }
    }

    _aovOutputs = outputs;

    // Temporary 2D dimensions to calculate dimensions3 (the 2D version isn't used later).
    const GfVec2i dimensions = _renderBufferSize != GfVec2i(0)
        ? _renderBufferSize
        : GfVec2i(static_cast<int>(viewport[2]), static_cast<int>(viewport[3]));
    const GfVec3i dimensions3(dimensions[0], dimensions[1], 1);

    HdAovDescriptorList outputDescs;

    // NOTE: A function could be used to get localOutputs.
    TfTokenVector localOutputs = outputs;

    if (somethingChanged)
    {
        _aovInputs.clear();
        if (inputs.size() > 0)
        {
            std::copy(inputs.begin(), inputs.end(), back_inserter(_aovInputs));
            _viewportAov = TfToken();
        }

        // If progressive rendering is enabled, render buffer clear is only required when
        // `_aovOutputs != outputs`.
        bool needClear = !_isProgressiveRenderingEnabled || _aovOutputs != outputs;

        // This will delete Bprims from the RenderIndex and clear the _viewportAov and _aovBufferIds
        // SdfPathVector.
        if (needClear)
        {
            for (size_t i = 0; i < _aovBufferIds.size(); ++i)
            {
                _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, _aovBufferIds[i]);
            }

            hasRemovedBuffers = true;

            // Clearing the viewport AOV triggers the recreation of the bindings after removing the
            // BPrims.
            _viewportAov = TfToken();
            _aovBufferIds.clear();
        }
    }

    // Get default AOV descriptors from the render delegate for each AOV token.
    // E.g. color:HdFormatFloat16Vec4, depth:HdFormatFloat32.
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
    const std::string rendererName = _pRenderIndex->GetRenderDelegate()->GetRendererDisplayName();
    RenderBufferBinding colorInput, depthInput;
    HdRenderBufferDescriptor colorDesc, depthDesc;
    for (size_t i = 0; i < localOutputs.size(); ++i)
    {
        HdRenderBufferDescriptor desc;
        desc.dimensions   = dimensions3;
        desc.format       = outputDescs[i].format;
        desc.multiSampled = _enableMultisampling;
        bool inputFound   = false;

        for (auto input : inputs)
        {
            if (input.aovName == localOutputs[i])
            {
                inputFound = (rendererName == input.rendererName);
                if (localOutputs[i] == pxr::HdAovTokens->depth)
                {
                    depthDesc  = desc;
                    depthInput = input;

                    if (inputFound)
                    {
                        // If the renderer remains the same, we don't want to copy the depth buffer.
                        // The existing depth buffer will continue to be used.
                        // We do this in order to not loose sub-pixel depth information.
                        // However, this means that if any Tasks write to the depth after a
                        // sub-pixel resolve then the depth buffer will be inconsistent with the
                        // color buffer and that depth information will be lost.  I don't think this
                        // currently happens in practice, so we are opting in favor of keeping the
                        // sub-pixel resolution.
                        //
                        // FUTURE: We may want to revisit this decision in the future.
                        // The long-term solution may be to do post processing at the sub-pixel
                        // accuracy.
                        depthInput.texture = HgiTextureHandle();
                    }
                }
                else if (!colorInput.texture)
                {
                    colorDesc  = desc;
                    colorInput = input;
                }
                break;
            }
        }

        // If something has changed and the input was not found or the previous renderer is
        // different than the current one, then we need to create a new render buffer. This will be
        // the buffer used and the previous contents potentially copied into.
        if (somethingChanged && !inputFound)
        {
            const SdfPath aovId = GetAovPath(controllerId, localOutputs[i]);
            _pRenderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, _syncDelegate.get(), aovId);

            _syncDelegate->SetValue(aovId, _tokens->renderBufferDescriptor, VtValue(desc));
            _syncDelegate->SetValue(aovId, HdStRenderBufferTokens->stormMsaaSampleCount,
                VtValue(desc.multiSampled ? _msaaSampleCount : 1));
            _pRenderIndex->GetChangeTracker().MarkBprimDirty(
                aovId, HdRenderBuffer::DirtyDescription);
            _aovBufferIds.push_back(aovId);
        }
    }

    // In case, we want to share the AOV buffers between frame passes but they are from different
    // render delegates, we then need to copy the AOV to visualize. But be careful that's not always
    // the color one we visualize.

    // Color AOV always means color & depth AOVs (where depth is optional).
    if (outputToVisualize == pxr::HdAovTokens->color && colorInput.texture)
    {
        PrepareBuffersFromInputs(colorInput, depthInput, colorDesc, controllerId);
    }
    // But depth AOV only means depth AOV only.
    else if (outputToVisualize == pxr::HdAovTokens->depth && depthInput.texture)
    {
        PrepareDepthOnlyFromInput(depthInput, depthDesc, controllerId);
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
        RenderBufferBinding foundInput {};
        for (auto input : inputs)
        {
            if (input.aovName == localOutputs[i])
            {
                foundInput = input;
                break;
            }
        }

        aovBindingsClear[i].aovName    = localOutputs[i];
        aovBindingsClear[i].clearValue = !foundInput.buffer ? outputDescs[i].clearValue : VtValue();
        aovBindingsClear[i].renderBufferId = GetAovPath(controllerId, localOutputs[i]);
        aovBindingsClear[i].aovSettings    = outputDescs[i].aovSettings;

        // Note, it would be better to just assign the output buffer here, but this breaks some
        // unit tests that expect this to be null and do a pointer-as-string comparison if it is not
        // which is not easily fixable.
        HdRenderBuffer* outputBuffer = static_cast<HdRenderBuffer*>(_pRenderIndex->GetBprim(
            HdPrimTypeTokens->renderBuffer, aovBindingsClear[i].renderBufferId));
        aovBindingsClear[i].renderBuffer = !outputBuffer ? foundInput.buffer : nullptr;

        aovBindingsNoClear[i]            = aovBindingsClear[i];
        aovBindingsNoClear[i].clearValue = VtValue();

        if (localOutputs[i] == HdAovTokens->depth)
        {
            aovInputBindings.push_back(aovBindingsNoClear[i]);
        }
    }

    // Used by the render tasks to indicate what targets are rendered into
    _aovTaskCache.aovBindingsClear   = aovBindingsClear;
    _aovTaskCache.aovBindingsNoClear = aovBindingsNoClear;

    // Used for volume rendering and contains only depth.
    _aovTaskCache.aovInputBindings   = aovInputBindings;

    _aovTaskCache.hasNoAovInputs     = (inputs.size() == 0); // For progressive rendering only?

    const SdfPath volumeId = GetRenderTaskPath(controllerId, HdStMaterialTagTokens->volume);

    if (localOutputs.size() > 0)
    {
        SetViewportRenderOutput(outputToVisualize, controllerId);
    }

    // NOTE: The viewport data plumbed to tasks unfortunately depends on whether aovs are being
    // used.
    return hasRemovedBuffers;
}

void RenderBufferManagerSDImpl::SetViewportRenderOutput(
    TfToken const& name, const SdfPath& controllerId)
{
    if (!IsAovSupported())
    {
        return;
    }

    if (_viewportAov == name)
    {
        return;
    }
    _viewportAov = name;

    _aovTaskCache.aovBufferPath   = SdfPath::EmptyPath();
    _aovTaskCache.depthBufferPath = SdfPath::EmptyPath();
    _aovTaskCache.neyeBufferPath  = SdfPath::EmptyPath();
    _aovTaskCache.aovBuffer       = nullptr;
    _aovTaskCache.depthBuffer     = nullptr;
    _aovTaskCache.neyeBuffer      = nullptr;

    if (!name.IsEmpty())
    {
        _aovTaskCache.aovBufferPath = GetAovPath(controllerId, name);
        _aovTaskCache.aovBuffer     = GetRenderOutput(name, controllerId);
        if (name == HdAovTokens->color)
        {
            // if we are visualizing the color AOV then we want to set the depth (and Neye?) as
            // well.
            _aovTaskCache.depthBufferPath = GetAovPath(controllerId, HdAovTokens->depth);
            _aovTaskCache.neyeBufferPath  = GetAovPath(controllerId, HdAovTokens->Neye);
            _aovTaskCache.depthBuffer     = GetRenderOutput(HdAovTokens->depth, controllerId);
            _aovTaskCache.neyeBuffer      = GetRenderOutput(HdAovTokens->Neye, controllerId);
        }
    }
}

HdRenderBuffer* RenderBufferManagerSDImpl::GetRenderOutput(
    const TfToken& name, const SdfPath& controllerId)
{
    if (!IsAovSupported())
    {
        return nullptr;
    }

    for (auto& binding : _aovTaskCache.aovBindingsClear)
    {
        if (name == binding.aovName)
        {
            if (binding.renderBuffer)
                return binding.renderBuffer;

            const SdfPath aovId = GetAovPath(controllerId, name);
            return static_cast<HdRenderBuffer*>(
                _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovId));
        }
    }

    return nullptr;
}

void RenderBufferManagerSDImpl::SetRenderOutputClearColor(
    const TfToken& name, const SdfPath& controllerId, const VtValue& clearValue)
{
    if (!IsAovSupported())
    {
        return;
    }

    // Check if we're setting a value for a nonexistent AOV.
    const SdfPath renderBufferId = GetAovPath(controllerId, name);

    if (clearValue.IsEmpty())
    {
        _aovTaskCache.outputClearValues.erase(renderBufferId);
    }
    else
    {
        _aovTaskCache.outputClearValues[renderBufferId] = clearValue;
    }
}

void RenderBufferManagerSDImpl::SetBufferSizeAndMsaa(
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

} // namespace HVT_NS
