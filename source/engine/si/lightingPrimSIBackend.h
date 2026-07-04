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

#include "../lightingPrimBackend.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hdx/simpleLightTask.h> // For HdxShadowMatrixComputationSharedPtr.
#include <pxr/usd/sdf/path.h>

#include <optional>
#include <unordered_map>

namespace HVT_NS
{

/// Scene-index (SI) based light prim backend.
///
/// Light Sprims are maintained in a retained scene index. This is the backend used after the
/// migration of the TaskManager to Hydra 2.0 scene indices.
class LightingPrimSIBackend : public LightingPrimBackend
{
public:
    explicit LightingPrimSIBackend(PXR_NS::HdRenderIndex* pRenderIndex,
        PXR_NS::HdRetainedSceneIndexRefPtr const& retainedSceneIndex,
        bool isHighQualityRenderer);

    ~LightingPrimSIBackend() override;

    PXR_NS::GlfSimpleLight GetLightAtId(
        size_t pathIdx, PXR_NS::SdfPathVector const& lightIds) const override;
    void ReplaceLightSprim(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::SdfPath const& pathName, PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds, bool isHighQualityRenderer) override;
    void RemoveLightSprim(size_t pathIdx, PXR_NS::SdfPathVector const& lightIds) override;
    void UpdateShadowMatrixComputation(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds) override;
    void UpdateCameraLightTransform(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::GfMatrix4d const& cameraTransform, PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds) override;
    void RemoveAllLights() override;

private:
    void ReplaceLightSprimInternal(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::SdfPath const& pathName, PXR_NS::GfRange3d const& worldExtent,
        PXR_NS::SdfPathVector const& lightIds, bool isHighQualityRenderer,
        std::optional<PXR_NS::GfMatrix4d> const& cameraLightTransformOverride = std::nullopt);

    PXR_NS::TfToken GetCameraLightType() const;

    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };
    PXR_NS::HdRetainedSceneIndexRefPtr _retainedSceneIndex;
    bool _isHighQualityRenderer { false };

    // Store current light data for GetLightAtId (path -> light)
    std::unordered_map<PXR_NS::SdfPath, PXR_NS::GlfSimpleLight, PXR_NS::SdfPath::Hash> _lightData;

    // Store shadow matrix computation for updates (path -> ShadowMatrixComputation)
    std::unordered_map<PXR_NS::SdfPath, PXR_NS::HdxShadowMatrixComputationSharedPtr,
        PXR_NS::SdfPath::Hash>
        _shadowMatrixComputations;
};

} // namespace HVT_NS
