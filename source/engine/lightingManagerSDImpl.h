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

#include <hvt/engine/syncDelegate.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/usd/sdf/path.h>

namespace HVT_NS
{

/// The scene-delegate (SD) based lighting management implementation.
///
/// This is the implementation used before the migration of the TaskManager to Hydra 2.0 scene
/// indices (commit 7bfc0f1). It maintains light Sprims directly in the render index, backed by a
/// SyncDelegate.
///
/// \note This used to be the nested LightingManager::Impl class. It was extracted into a standalone
/// class so it can coexist with the scene-index based implementation, selectable at runtime.
class LightingManagerSDImpl : public LightingManagerImpl
{
    PXR_NS::SdfPathVector _excludedLights;
    bool _enableShadows { true };

    /// The parent identifier for light Sprims that are added to the render index.
    const PXR_NS::SdfPath _lightRootPath;

    /// The render index used to insert and remove light Sprims.
    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };

    /// The scene delegate used to provide light Sprim data.
    SyncDelegatePtr _lightDelegate;

    /// High quality renderer support material networks for lighting.
    bool _isHighQualityRenderer { false };

    /// Lighting context stores information of the view light attributes params.
    PXR_NS::GlfSimpleLightingContextRefPtr _lightingState;

public:
    explicit LightingManagerSDImpl(PXR_NS::SdfPath const& lightRootPath,
        PXR_NS::HdRenderIndex* pRenderIndex, SyncDelegatePtr& lightDelegate,
        bool isHighQualityRenderer) :
        _lightRootPath(lightRootPath),
        _pRenderIndex(pRenderIndex),
        _lightDelegate(lightDelegate),
        _isHighQualityRenderer(isHighQualityRenderer)
    {
        _lightingState = PXR_NS::GlfSimpleLightingContext::New();
    }

    ~LightingManagerSDImpl() override
    {
        const PXR_NS::TfToken cameraLightType = GetCameraLightType(_pRenderIndex);
        for (auto const& id : _lightIds)
        {
            _pRenderIndex->RemoveSprim(cameraLightType, id);
            _pRenderIndex->RemoveSprim(PXR_NS::HdPrimTypeTokens->domeLight, id);
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
    // Built-in lights.
    PXR_NS::SdfPathVector _lightIds;

    bool SupportBuiltInLightTypes(const PXR_NS::HdRenderIndex* index) const;

    void SetBuiltInLightingState(
        PXR_NS::GfMatrix4d const& cameraTransform, PXR_NS::GfRange3d const& worldExtent);

    PXR_NS::TfToken GetCameraLightType(const PXR_NS::HdRenderIndex* pRenderIndex) const;

    PXR_NS::VtValue GetDomeLightTexture(PXR_NS::GlfSimpleLight const& light) const;
    void SetParameters(PXR_NS::SdfPath const& pathName, PXR_NS::GlfSimpleLight const& light,
        SyncDelegatePtr& lightDelegate, bool isHighQualityRenderer,
        PXR_NS::GfRange3d const& worldExtent);
    void SetMaterialNetwork(PXR_NS::SdfPath const& pathName, PXR_NS::GlfSimpleLight const& light,
        SyncDelegatePtr& lightDelegate);
    void GetMaterialNetwork(PXR_NS::SdfPath const& pathName, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::HdMaterialNetworkMap& outNetworkMap) const;
    PXR_NS::GlfSimpleLight GetLightAtId(size_t const& pathIdx, SyncDelegatePtr const& lightDelegate);

    void RemoveLightSprim(size_t const& pathIdx);
    void ReplaceLightSprim(size_t const& pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::SdfPath const& pathName, PXR_NS::GfRange3d const& worldExtent);
};

} // namespace HVT_NS
