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

#include "renderBufferManagerImpl.h"

#include "sd/renderBufferDescriptorSDStorage.h"
#include "sd/taskContainerSDImpl.h"
#if HVT_HAS_LEGACY_TASK_SCHEMA
#include "si/renderBufferDescriptorSIStorage.h"
#include "si/taskContainerSIImpl.h"
#endif

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
#include <pxr/imaging/hdSt/tokens.h>
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

namespace HVT_NS
{

namespace
{

Hgi* GetHgi(HdRenderIndex const* renderIndex)
{
    Hgi* hgi = hvt::HgiInstance::instance().hgi();
    if (hgi)
        return hgi;

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

RenderBufferManagerImpl::RenderBufferManagerImpl(
    HdRenderIndex* pRenderIndex, std::shared_ptr<TaskDataContainer> const& container) :
    _renderBufferSize(0, 0), _pRenderIndex(pRenderIndex)
{
    _presentParams.api             = HgiTokens->OpenGL;
    _isProgressiveRenderingEnabled = { TfGetenvBool("AGP_ENABLE_PROGRESSIVE_RENDERING", false) };

#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (auto* si = dynamic_cast<TaskContainerSIImpl*>(container.get()))
    {
        _storage = std::make_unique<RenderBufferDescriptorSIStorage>(
            si->GetRetainedSceneIndex());
        return;
    }
#endif
    auto* sd = dynamic_cast<TaskContainerSDImpl*>(container.get());
    TF_VERIFY(sd, "TaskDataContainer is neither SI nor SD");
    if (sd)
    {
        _storage = std::make_unique<RenderBufferDescriptorSDStorage>(
            _pRenderIndex, sd->GetSyncDelegate());
    }
}

RenderBufferManagerImpl::~RenderBufferManagerImpl()
{
    if (_storage && !_aovBufferIds.empty())
    {
        _storage->RemoveRenderBuffers(_aovBufferIds);
    }
}

bool RenderBufferManagerImpl::IsAovSupported() const
{
    return _pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer);
}

GfVec2i const& RenderBufferManagerImpl::GetRenderBufferSize() const
{
    return _renderBufferSize;
}

void RenderBufferManagerImpl::PrepareBuffersFromInputs(RenderBufferBinding const& colorInputAov,
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
    HdRenderBuffer* colorBuffer = static_cast<HdRenderBuffer*>(
        _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovPath));

    if (!colorBuffer)
    {
        colorBuffer = colorInputAov.buffer;
    }
    else
    {
        if (!colorBuffer->IsMapped())
        {
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
        return;
    }

    if (colorOutput == colorInput)
        return;

    HgiTextureHandle depthOutput;
    if (depthInput)
    {
        const SdfPath aovDepthPath = GetAovPath(controllerId, HdAovTokens->depth);

        HdRenderBuffer* depthBuffer = static_cast<HdRenderBuffer*>(
            _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovDepthPath));

        if (!depthBuffer)
        {
            depthBuffer = depthInputAov.buffer;
        }
        else
        {
            if (!depthBuffer->IsMapped())
            {
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

    if (!_copyColorShader)
    {
        _copyColorShader = std::make_unique<HdxFullscreenShader>(hgi, "Copy Color Buffer");
    }

    if (!_copyColorShaderNoDepth)
    {
        _copyColorShaderNoDepth =
            std::make_unique<HdxFullscreenShader>(hgi, "Copy Color Buffer No Depth");
    }

    HdxFullscreenShader* shader =
        (!depthInput ? _copyColorShaderNoDepth.get() : _copyColorShader.get());

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

void RenderBufferManagerImpl::PrepareDepthOnlyFromInput(RenderBufferBinding const& inputDepthAov,
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
    HdRenderBuffer* buffer = static_cast<HdRenderBuffer*>(
        _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovPath));

    if (!buffer)
    {
        buffer = inputDepthAov.buffer;
    }
    else
    {
        if (!buffer->IsMapped())
        {
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
        return;
    }

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

    if (!_copyDepthShader)
    {
        _copyDepthShader = std::make_unique<CopyDepthShader>(hgi);
    }

    _copyDepthShader->Execute(input, output);
}

bool RenderBufferManagerImpl::SetRenderOutputs(TfToken const& outputToVisualize,
    TfTokenVector const& outputs, RenderBufferBindings const& inputs, GfVec4d const& viewport,
    SdfPath const& controllerId)
{
    bool hasRemovedBuffers = false;

    if (!IsAovSupported())
    {
        return false;
    }

    bool somethingChanged = true;
    if (!_isProgressiveRenderingEnabled)
    {
        if (_aovOutputs == outputs && inputs.size() == _aovInputs.size() &&
            std::equal(inputs.begin(), inputs.end(), _aovInputs.begin(), _aovInputs.end()))
        {
            somethingChanged = false;
        }
    }

    _aovOutputs = outputs;

    const GfVec2i dimensions = _renderBufferSize != GfVec2i(0)
        ? _renderBufferSize
        : GfVec2i(static_cast<int>(viewport[2]), static_cast<int>(viewport[3]));
    const GfVec3i dimensions3(dimensions[0], dimensions[1], 1);

    HdAovDescriptorList outputDescs;

    TfTokenVector localOutputs = outputs;

    if (somethingChanged)
    {
        _aovInputs.clear();
        if (inputs.size() > 0)
        {
            std::copy(inputs.begin(), inputs.end(), back_inserter(_aovInputs));
            _viewportAov = TfToken();
        }

        bool needClear = !_isProgressiveRenderingEnabled || _aovOutputs != outputs;

        if (needClear)
        {
            if (!_aovBufferIds.empty())
            {
                _storage->RemoveRenderBuffers(_aovBufferIds);
            }

            hasRemovedBuffers = true;

            _viewportAov = TfToken();
            _aovBufferIds.clear();
        }
    }

    for (auto it = localOutputs.begin(); it != localOutputs.end();)
    {
        HdAovDescriptor desc = _pRenderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(*it);
        if (desc.format == HdFormatInvalid)
        {
            it = localOutputs.erase(it);
        }
        else
        {
            outputDescs.push_back(desc);
            ++it;
        }
    }

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
                if (localOutputs[i] == HdAovTokens->depth)
                {
                    depthDesc  = desc;
                    depthInput = input;

                    if (inputFound)
                    {
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

        if (somethingChanged && !inputFound)
        {
            const SdfPath aovId = GetAovPath(controllerId, localOutputs[i]);
            _storage->InsertRenderBuffer(aovId, desc, _msaaSampleCount);
            _aovBufferIds.push_back(aovId);
        }
    }

    if (outputToVisualize == HdAovTokens->color && colorInput.texture)
    {
        PrepareBuffersFromInputs(colorInput, depthInput, colorDesc, controllerId);
    }
    else if (outputToVisualize == HdAovTokens->depth && depthInput.texture)
    {
        PrepareDepthOnlyFromInput(depthInput, depthDesc, controllerId);
    }

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

    _aovTaskCache.aovBindingsClear   = aovBindingsClear;
    _aovTaskCache.aovBindingsNoClear = aovBindingsNoClear;
    _aovTaskCache.aovInputBindings   = aovInputBindings;
    _aovTaskCache.hasNoAovInputs     = (inputs.size() == 0);

    const SdfPath volumeId = GetRenderTaskPath(controllerId, HdStMaterialTagTokens->volume);

    if (localOutputs.size() > 0)
    {
        SetViewportRenderOutput(outputToVisualize, controllerId);
    }

    return hasRemovedBuffers;
}

void RenderBufferManagerImpl::SetViewportRenderOutput(
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
            _aovTaskCache.depthBufferPath = GetAovPath(controllerId, HdAovTokens->depth);
            _aovTaskCache.neyeBufferPath  = GetAovPath(controllerId, HdAovTokens->Neye);
            _aovTaskCache.depthBuffer     = GetRenderOutput(HdAovTokens->depth, controllerId);
            _aovTaskCache.neyeBuffer      = GetRenderOutput(HdAovTokens->Neye, controllerId);
        }
    }
}

HdRenderBuffer* RenderBufferManagerImpl::GetRenderOutput(
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

void RenderBufferManagerImpl::SetRenderOutputClearColor(
    const TfToken& name, const SdfPath& controllerId, const VtValue& clearValue)
{
    if (!IsAovSupported())
    {
        return;
    }

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

void RenderBufferManagerImpl::SetBufferSizeAndMsaa(
    const GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled)
{
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
        return;
    }

    const GfVec3i dimensions3(_renderBufferSize[0], _renderBufferSize[1], 1);

    _storage->UpdateRenderBufferDescriptors(
        _aovBufferIds, dimensions3, _enableMultisampling, _msaaSampleCount,
        descriptorSpecsChanged, msaaSampleCountChanged);
}

} // namespace HVT_NS
