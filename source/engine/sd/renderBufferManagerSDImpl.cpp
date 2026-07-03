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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/pxr.h>

#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hdSt/tokens.h>

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

RenderBufferManagerSDImpl::RenderBufferManagerSDImpl(
    HdRenderIndex* pRenderIndex, SyncDelegatePtr const& syncDelegate) :
    RenderBufferManagerImpl(pRenderIndex), _syncDelegate(syncDelegate)
{
}

RenderBufferManagerSDImpl::~RenderBufferManagerSDImpl()
{
    for (auto const& id : _aovBufferIds)
    {
        _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, id);
    }
}

void RenderBufferManagerSDImpl::InsertRenderBuffer(
    SdfPath const& id, HdRenderBufferDescriptor const& desc, size_t msaaSampleCount)
{
    _pRenderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, _syncDelegate.get(), id);
    _syncDelegate->SetValue(id, _tokens->renderBufferDescriptor, VtValue(desc));
    _syncDelegate->SetValue(
        id, HdStRenderBufferTokens->stormMsaaSampleCount,
        VtValue(desc.multiSampled ? msaaSampleCount : static_cast<size_t>(1)));
    _pRenderIndex->GetChangeTracker().MarkBprimDirty(id, HdRenderBuffer::DirtyDescription);
}

void RenderBufferManagerSDImpl::RemoveRenderBuffers(SdfPathVector const& ids)
{
    for (auto const& id : ids)
    {
        _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, id);
    }
}

void RenderBufferManagerSDImpl::UpdateRenderBufferDescriptors(GfVec3i const& dimensions,
    bool multiSampled, size_t msaaSampleCount, bool descriptorSpecsChanged,
    bool msaaSampleCountChanged)
{
    HdChangeTracker& changeTracker = _pRenderIndex->GetChangeTracker();

    for (auto const& id : _aovBufferIds)
    {
        bool bprimDirty = false;
        if (descriptorSpecsChanged)
        {
            VtValue vParams = _syncDelegate->GetValue(id, _tokens->renderBufferDescriptor);
            HdRenderBufferDescriptor desc = vParams.Get<HdRenderBufferDescriptor>();

            if (desc.dimensions != dimensions || desc.multiSampled != multiSampled)
            {
                desc.dimensions   = dimensions;
                desc.multiSampled = multiSampled;
                _syncDelegate->SetValue(id, _tokens->renderBufferDescriptor, VtValue(desc));
                bprimDirty = true;
            }
        }

        if (msaaSampleCountChanged)
        {
            _syncDelegate->SetValue(
                id, HdStRenderBufferTokens->stormMsaaSampleCount, VtValue(msaaSampleCount));
            bprimDirty = true;
        }

        if (bprimDirty)
        {
            changeTracker.MarkBprimDirty(id, HdRenderBuffer::DirtyDescription);
        }
    }
}

} // namespace HVT_NS
