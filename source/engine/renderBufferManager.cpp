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

#include <hvt/engine/renderBufferManager.h>

#include "renderBufferManagerSDImpl.h"
#include "renderBufferManagerSIImpl.h"
#include "taskContainerSDImpl.h"
#if HVT_HAS_LEGACY_TASK_SCHEMA
#include "taskContainerSIImpl.h"
#endif

#include <hvt/engine/engine.h>

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

#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>

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

RenderBufferManager::RenderBufferManager(SdfPath const& taskManagerUid, HdRenderIndex* pRenderIndex,
    std::shared_ptr<TaskDataContainer> const& container) :
    _taskManagerUid(taskManagerUid), _pRenderIndex(pRenderIndex)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (auto* si = dynamic_cast<TaskContainerSIImpl*>(container.get()))
    {
        _impl = std::make_unique<RenderBufferManagerSIImpl>(
            _pRenderIndex, si->GetRetainedSceneIndex());
        return;
    }
#endif
    auto* sd = dynamic_cast<TaskContainerSDImpl*>(container.get());
    TF_VERIFY(sd, "TaskDataContainer is neither SI nor SD");
    if (sd)
    {
        _impl = std::make_unique<RenderBufferManagerSDImpl>(
            _pRenderIndex, sd->GetSyncDelegate());
    }
}

RenderBufferManager::~RenderBufferManager() {}

TfTokenVector RenderBufferManager::GetAllRendererAovs()
{
    return { HdAovTokens->color, HdAovTokens->depth, HdAovTokens->primId, HdAovTokens->elementId,
        HdAovTokens->instanceId };
}

TfTokenVector RenderBufferManager::GetSupportedRendererAovs() const
{
    if (_pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer))
    {
        auto const& candidates = GetAllRendererAovs();

        TfTokenVector aovs;
        for (auto const& aov : candidates)
        {
            if (_pRenderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(aov).format !=
                HdFormatInvalid)
            {
                aovs.push_back(aov);
            }
        }
        return aovs;
    }
    return {};
}

HgiTextureHandle RenderBufferManager::GetAovTexture(TfToken const& token, Engine* engine) const
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

bool RenderBufferManager::IsAovSupported() const
{
    return _pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer);
}

bool RenderBufferManager::IsProgressiveRenderingEnabled() const
{
    return _impl->IsProgressiveRenderingEnabled();
}

AovParams const& RenderBufferManager::GetAovParamCache() const
{
    return _impl->GetAovParamCache();
}

PresentationParams const& RenderBufferManager::GetPresentationParams() const
{
    return _impl->GetPresentationParams();
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

bool RenderBufferManager::SetRenderOutputs(TfToken const& visualizeAOV,
    TfTokenVector const& outputs, RenderBufferBindings const& inputs, GfVec4d const& viewport)
{
    return _impl->SetRenderOutputs(visualizeAOV, outputs, inputs, viewport, _taskManagerUid);
}

TfTokenVector const& RenderBufferManager::GetRenderOutputs() const
{
    return _impl->GetRenderOutputs();
}

void RenderBufferManager::SetPresentationOutput(TfToken const& api, VtValue const& framebuffer)
{
    _impl->SetPresentationOutput(api, framebuffer);
}

void RenderBufferManager::SetInteropPresentation(
    VtValue const& destination, VtValue const& composition)
{
    _impl->SetInteropPresentation(destination, composition);
}

void RenderBufferManager::SetWindowPresentation(VtValue const& window, bool vsync)
{
    _impl->SetWindowPresentation(window, vsync);
}

} // namespace HVT_NS
