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

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderIndex;
PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS
{

/// Backend for SI/SD-specific light prim operations.
///
/// The LightingManager::Impl delegates backend-specific prim creation, update, and removal to a
/// concrete backend implementation. The scene-index (SI) backend stores prims in a retained scene
/// index; the scene-delegate (SD) backend stores them directly in the render index via a
/// SyncDelegate.
class LightingPrimBackend
{
public:
    virtual ~LightingPrimBackend() = default;

    virtual PXR_NS::GlfSimpleLight GetLightAtId(
        size_t pathIdx, PXR_NS::SdfPathVector const& lightIds) const = 0;
    virtual void ReplaceLightSprim(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::SdfPath const& pathName, PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds, bool isHighQualityRenderer) = 0;
    virtual void RemoveLightSprim(size_t pathIdx, PXR_NS::SdfPathVector const& lightIds) = 0;
    virtual void SyncLightStateAfterReplace(
        size_t /*pathIdx*/, PXR_NS::GlfSimpleLight const& /*light*/,
        PXR_NS::SdfPathVector const& /*lightIds*/) {}
    virtual void UpdateShadowMatrixComputation(size_t pathIdx,
        PXR_NS::GlfSimpleLight const& light, PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds) = 0;
    virtual void UpdateCameraLightTransform(size_t pathIdx,
        PXR_NS::GlfSimpleLight const& light, PXR_NS::GfMatrix4d const& cameraTransform,
        PXR_NS::GfRange3d const& worldExtent, PXR_NS::SdfPathVector const& lightIds) = 0;
    virtual void RemoveAllLights() = 0;
};

using LightingPrimBackendPtr = std::unique_ptr<LightingPrimBackend>;

} // namespace HVT_NS
