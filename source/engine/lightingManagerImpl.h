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
#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/renderIndex.h>
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
    virtual void ProcessLightingState(
        PXR_NS::GfMatrix4d const& cameraTransform, PXR_NS::GfRange3d const& worldExtent) = 0;

    /// Sets whether shadows are enabled or not.
    virtual void SetEnableShadows(bool enable) = 0;

    /// Sets the list of lights to exclude.
    virtual void SetExcludedLights(PXR_NS::SdfPathVector const& excludedLights) = 0;

    /// Returns the lighting context.
    virtual PXR_NS::GlfSimpleLightingContextRefPtr const& GetLightingContext() const = 0;

    /// Returns the SdfPaths of excluded lights.
    virtual PXR_NS::SdfPathVector const& GetExcludedLights() const = 0;

    /// Returns whether shadows are enabled or not.
    virtual bool GetShadowsEnabled() const = 0;

protected:
    LightingManagerImpl(PXR_NS::SdfPath const& lightRootPath, PXR_NS::HdRenderIndex* pRenderIndex,
        bool isHighQualityRenderer) :
        _lightRootPath(lightRootPath),
        _pRenderIndex(pRenderIndex),
        _isHighQualityRenderer(isHighQualityRenderer)
    {
        _lightingState = PXR_NS::GlfSimpleLightingContext::New();
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
