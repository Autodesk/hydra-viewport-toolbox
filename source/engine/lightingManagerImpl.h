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

#include <hvt/api.h> // For the HVT_NS namespace macro.

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/path.h>

namespace HVT_NS
{

/// Abstract base class for a lighting management backend.
///
/// The public LightingManager is a thin shell owning a std::unique_ptr<LightingManagerImpl>.
/// The scene-index (SI) and scene-delegate (SD) implementations both derive from this class,
/// so the backend can be selected at runtime without changing the public API.
///
/// This class holds the member data and accessor implementations that are common to both backends.
class LightingManagerImpl
{
public:
    virtual ~LightingManagerImpl() = default;

    /// Creates/updates the light prims from the current lighting context.
    void ProcessLightingState(
        PXR_NS::GfMatrix4d const& cameraTransform, PXR_NS::GfRange3d const& worldExtent)
    {
        if (!_lightingState)
        {
            return;
        }

        if (_isHighQualityRenderer && !SupportBuiltInLightTypes())
        {
            return;
        }

        SetBuiltInLightingState(cameraTransform, worldExtent);
    }

    void SetEnableShadows(bool enable) { _enableShadows = enable; }
    void SetExcludedLights(PXR_NS::SdfPathVector const& excludedLights)
    {
        _excludedLights = excludedLights;
    }

    PXR_NS::GlfSimpleLightingContextRefPtr const& GetLightingContext() const
    {
        return _lightingState;
    }

    PXR_NS::SdfPathVector const& GetExcludedLights() const { return _excludedLights; }
    bool GetShadowsEnabled() const { return _enableShadows; }

    static constexpr float kDistantLightAngle     = 0.53f;
    static constexpr float kDistantLightIntensity = 15000.0f;

    static PXR_NS::TfToken GetTexturePath(char const* texture);
    static PXR_NS::TfToken GetPackageDefaultDomeLightTexture();
    static PXR_NS::VtValue GetDomeLightTextureValue(PXR_NS::GlfSimpleLight const& light);
    static void GetMaterialNetwork(PXR_NS::SdfPath const& pathName,
        PXR_NS::GlfSimpleLight const& light, PXR_NS::HdMaterialNetworkMap& outNetworkMap);

protected:
    LightingManagerImpl(PXR_NS::SdfPath const& lightRootPath, PXR_NS::HdRenderIndex* pRenderIndex,
        bool isHighQualityRenderer) :
        _lightRootPath(lightRootPath),
        _pRenderIndex(pRenderIndex),
        _isHighQualityRenderer(isHighQualityRenderer)
    {
        _lightingState = PXR_NS::GlfSimpleLightingContext::New();
    }

    void SetBuiltInLightingState(
        PXR_NS::GfMatrix4d const& cameraTransform, PXR_NS::GfRange3d const& worldExtent);

    virtual PXR_NS::GlfSimpleLight GetLightAtId(size_t pathIdx) const = 0;
    virtual void ReplaceLightSprim(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::SdfPath const& pathName, PXR_NS::GfRange3d const& worldExtent) = 0;
    virtual void RemoveLightSprim(size_t pathIdx) = 0;
    virtual void PostReplaceLightSync(size_t pathIdx, PXR_NS::GlfSimpleLight const& light,
        PXR_NS::GfRange3d const& worldExtent) = 0;
    virtual void UpdateCameraLightTransform(size_t pathIdx,
        PXR_NS::GlfSimpleLight const& light, PXR_NS::GfMatrix4d const& cameraTransform,
        PXR_NS::GfRange3d const& worldExtent) = 0;

    bool SupportBuiltInLightTypes() const
    {
        bool dome   = _pRenderIndex->IsSprimTypeSupported(PXR_NS::HdPrimTypeTokens->domeLight);
        bool camera = (_pRenderIndex->IsSprimTypeSupported(PXR_NS::HdPrimTypeTokens->simpleLight) ||
            _pRenderIndex->IsSprimTypeSupported(PXR_NS::HdPrimTypeTokens->distantLight));
        return dome && camera;
    }

    PXR_NS::TfToken GetCameraLightType() const
    {
        return _pRenderIndex->IsSprimTypeSupported(PXR_NS::HdPrimTypeTokens->simpleLight)
            ? PXR_NS::HdPrimTypeTokens->simpleLight
            : PXR_NS::HdPrimTypeTokens->distantLight;
    }

    PXR_NS::SdfPathVector _excludedLights;
    bool _enableShadows { true };

    const PXR_NS::SdfPath _lightRootPath;
    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };
    bool _isHighQualityRenderer { false };
    PXR_NS::GlfSimpleLightingContextRefPtr _lightingState;
    PXR_NS::SdfPathVector _lightIds;
};

} // namespace HVT_NS
