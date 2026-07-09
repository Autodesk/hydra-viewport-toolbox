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

#include <hvt/api.h>

#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

/// Backend for render buffer Bprims.
///
/// The SI implementation stores Bprims in a retained scene index; the SD implementation stores
/// them in the render index via a SyncDelegate. RenderBufferManager::Impl owns one of these and
/// delegates backend-specific operations to it.
class RenderBufferPrimBackend
{
public:
    virtual ~RenderBufferPrimBackend() = default;

    virtual void InsertRenderBuffer(PXR_NS::SdfPath const& id,
        PXR_NS::HdRenderBufferDescriptor const& desc, size_t msaaSampleCount) = 0;

    virtual void RemoveRenderBuffers(PXR_NS::SdfPathVector const& ids) = 0;

    virtual void UpdateRenderBufferDescriptors(PXR_NS::SdfPathVector const& bufferIds,
        PXR_NS::GfVec3i const& dimensions, bool multiSampled, size_t msaaSampleCount,
        bool descriptorSpecsChanged, bool msaaSampleCountChanged) = 0;
};

using RenderBufferPrimBackendPtr = std::unique_ptr<RenderBufferPrimBackend>;

} // namespace HVT_NS
