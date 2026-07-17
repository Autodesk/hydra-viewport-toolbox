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
#pragma once

#include "../lightingPrimBackend.h"

#include "syncDelegate.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/usd/sdf/path.h>

namespace HVT_NS
{

/// Scene-delegate (SD) based light prim backend.
///
/// Light Sprims are inserted directly into the render index and their values are stored in a
/// SyncDelegate. This is the backend used before the migration to Hydra 2.0 scene indices.
class LightingPrimSDBackend : public LightingPrimBackend
{
public:
    explicit LightingPrimSDBackend(
        PXR_NS::HdRenderIndex* pRenderIndex, SyncDelegatePtr const& lightDelegate);

    ~LightingPrimSDBackend() override;

    PXR_NS::GlfSimpleLight GetLightAtId(
        size_t pathIdx, PXR_NS::SdfPathVector const& lightIds) const override;
    void ReplaceLightSprim(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::SdfPath const& pathName, PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds, bool isHighQualityRenderer) override;
    void RemoveLightSprim(size_t pathIdx, PXR_NS::SdfPathVector const& lightIds) override;
    void SyncLightStateAfterReplace(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::SdfPathVector const& lightIds) override;
    void UpdateShadowMatrixComputation(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds) override;
    void UpdateCameraLightTransform(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::GfMatrix4d const& cameraTransform, PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds) override;
    void RemoveAllLights() override;

private:
    PXR_NS::TfToken GetCameraLightType() const;

    static void SetParameters(PXR_NS::SdfPath const& pathName, PXR_NS::GlfSimpleLight const& light,
        SyncDelegatePtr& lightDelegate, bool isHighQualityRenderer,
        PXR_NS::GfRange3d const& worldExtent);
    static void SetMaterialNetwork(PXR_NS::SdfPath const& pathName,
        PXR_NS::GlfSimpleLight const& light, SyncDelegatePtr& lightDelegate);

    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };
    SyncDelegatePtr _syncDelegate;

    /// Light IDs tracked for cleanup in the destructor.
    PXR_NS::SdfPathVector _trackedLightIds;
};

} // namespace HVT_NS
