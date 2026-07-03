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
#pragma once

#include "../renderBufferManagerImpl.h"

#include <hvt/engine/syncDelegate.h>

namespace HVT_NS
{

/// The scene-delegate (SD) based render buffer management implementation.
class RenderBufferManagerSDImpl : public RenderBufferManagerImpl
{
public:
    explicit RenderBufferManagerSDImpl(
        PXR_NS::HdRenderIndex* pRenderIndex, SyncDelegatePtr const& syncDelegate);
    ~RenderBufferManagerSDImpl() override;

    RenderBufferManagerSDImpl(RenderBufferManagerSDImpl const&)            = delete;
    RenderBufferManagerSDImpl& operator=(RenderBufferManagerSDImpl const&) = delete;

private:
    void InsertRenderBuffer(PXR_NS::SdfPath const& id,
        PXR_NS::HdRenderBufferDescriptor const& desc, size_t msaaSampleCount) override;
    void RemoveRenderBuffers(PXR_NS::SdfPathVector const& ids) override;
    void UpdateRenderBufferDescriptors(PXR_NS::GfVec3i const& dimensions, bool multiSampled,
        size_t msaaSampleCount, bool descriptorSpecsChanged,
        bool msaaSampleCountChanged) override;

    SyncDelegatePtr _syncDelegate;
};

} // namespace HVT_NS
