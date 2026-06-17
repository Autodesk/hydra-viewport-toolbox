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

#include "lightingManagerImpl.h"
#include "shadow/shadowMatrixComputation.h"

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

/// The scene-index (SI) based lighting management implementation.
///
/// This is the implementation introduced by the migration of the TaskManager to Hydra 2.0 scene
/// indices. It maintains light Sprims in a retained scene index.
///
/// \note This used to be the nested LightingManager::Impl class. It was extracted into a standalone
/// class so a second (scene-delegate based) implementation can coexist with it.
class LightingManagerSIImpl : public LightingManagerImpl
{

    PXR_NS::SdfPathVector _excludedLights;
    bool _enableShadows { true };

    const PXR_NS::SdfPath _lightRootPath;
    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };
    PXR_NS::HdRetainedSceneIndexRefPtr _retainedSceneIndex;
    bool _isHighQualityRenderer { false };
    PXR_NS::GlfSimpleLightingContextRefPtr _lightingState;

    // Store current light data for GetLightAtId (path -> light)
    std::unordered_map<PXR_NS::SdfPath, PXR_NS::GlfSimpleLight, PXR_NS::SdfPath::Hash> _lightData;

    // Store shadow matrix computation for updates (path -> ShadowMatrixComputation)
    std::unordered_map<PXR_NS::SdfPath, PXR_NS::HdxShadowMatrixComputationSharedPtr,
        PXR_NS::SdfPath::Hash>
        _shadowMatrixComputations;

public:
    explicit LightingManagerSIImpl(PXR_NS::SdfPath const& lightRootPath,
        PXR_NS::HdRenderIndex* pRenderIndex,
        PXR_NS::HdRetainedSceneIndexRefPtr const& retainedSceneIndex, bool isHighQualityRenderer) :
        _lightRootPath(lightRootPath),
        _pRenderIndex(pRenderIndex),
        _retainedSceneIndex(retainedSceneIndex),
        _isHighQualityRenderer(isHighQualityRenderer)
    {
        _lightingState = PXR_NS::GlfSimpleLightingContext::New();
    }

    ~LightingManagerSIImpl()
    {
        if (_retainedSceneIndex)
        {
            PXR_NS::HdSceneIndexObserver::RemovedPrimEntries removedEntries;
            for (auto const& [path, _] : _lightData)
            {
                removedEntries.push_back({ path });
            }
            if (!removedEntries.empty())
            {
                _retainedSceneIndex->RemovePrims(removedEntries);
            }
        }
    }

    void ProcessLightingState(PXR_NS::GfMatrix4d const& cameraTransform,
        PXR_NS::GfRange3d const& worldExtent) override;

    void SetEnableShadows(bool enable) override;
    void SetExcludedLights(PXR_NS::SdfPathVector const& excludedLights) override;

    PXR_NS::GlfSimpleLightingContextRefPtr const& GetLightingContext() const override;
    PXR_NS::SdfPathVector const& GetExcludedLights() const override;
    bool GetShadowsEnabled() const override;

private:
    PXR_NS::SdfPathVector _lightIds;

    bool SupportBuiltInLightTypes(const PXR_NS::HdRenderIndex* index) const;
    void SetBuiltInLightingState(
        PXR_NS::GfMatrix4d const& cameraTransform, PXR_NS::GfRange3d const& worldExtent);
    PXR_NS::TfToken GetCameraLightType(const PXR_NS::HdRenderIndex* pRenderIndex) const;

    PXR_NS::GlfSimpleLight const& GetLightAtId(size_t pathIdx) const;
    void RemoveLightSprim(size_t pathIdx);
    void ReplaceLightSprim(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::SdfPath const& pathName, PXR_NS::GfRange3d const& worldExtent,
        std::optional<PXR_NS::GfMatrix4d> const& cameraLightTransformOverride = std::nullopt);
};

} // namespace HVT_NS
