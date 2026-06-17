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

#include <hvt/engine/lightingSettingsProvider.h>
#include <hvt/engine/syncDelegate.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

using LightingManagerPtr = std::shared_ptr<class LightingManager>;

/// A class that maintains lighting data and light prims associated with a render index and provides
/// data for tasks that use this data.
class LightingManager : public LightingSettingsProvider
{
public:
    /// Constructor.
    /// \param lightRootPath The light root path (i.e., uid).
    /// \param pRenderIndex The HdRenderIndex used to create render buffer Bprims.
    /// \param retainedSceneIndex The retained scene index used for light Sprims (Hydra 2.0).
    /// \param isHighQualityRenderer Whether the renderer supports complex materialNetworkMaps.
    LightingManager(PXR_NS::SdfPath const& lightRootPath, PXR_NS::HdRenderIndex* pRenderIndex,
        PXR_NS::HdRetainedSceneIndexRefPtr const& retainedSceneIndex, bool isHighQualityRenderer);

    /// Constructor for the scene-delegate (SD) backend.
    /// \param lightRootPath The light root path (i.e., uid).
    /// \param pRenderIndex The HdRenderIndex used to create light Sprims.
    /// \param syncDelegate The scene delegate used to provide light Sprim data.
    /// \param isHighQualityRenderer Whether the renderer supports complex materialNetworkMaps.
    LightingManager(PXR_NS::SdfPath const& lightRootPath, PXR_NS::HdRenderIndex* pRenderIndex,
        SyncDelegatePtr& syncDelegate, bool isHighQualityRenderer);

    /// Destructor.
    ~LightingManager();

    /// Sets the state for the lighting manager, from which light prims are created.
    /// \param lights The list of active lights for the scene.
    /// \param material light material.
    /// \param ambient light ambient color.
    /// \param cameraPath Prim path of the active camera in the render index. This is
    ///     either the free camera prim authored by the frame pass or a user-supplied
    ///     scene camera path.
    /// \param cameraTransform World-space transform of that camera (i.e., view
    ///     matrix inverse). Used to anchor camera-relative lights.
    /// \param worldExtent The world extents for the scene. Used by things like shadows, etc.
    void SetLighting(PXR_NS::GlfSimpleLightVector const& lights,
        PXR_NS::GlfSimpleMaterial const& material, PXR_NS::GfVec4f const& ambient,
        PXR_NS::SdfPath const& cameraPath, PXR_NS::GfMatrix4d const& cameraTransform,
        const PXR_NS::GfRange3d& worldExtent);

    /// Sets the list of lights to exclude.
    void SetExcludedLights(PXR_NS::SdfPathVector const& excludedLights);

    /// Sets whether shadows are enabled or not.
    void SetEnableShadows(bool enable);

    /// Returns the lighting context.
    PXR_NS::GlfSimpleLightingContextRefPtr const& GetLightingContext() const override;

    /// Returns the SdfPaths of excluded lights.
    PXR_NS::SdfPathVector const& GetExcludedLights() const override;

    /// Returns whether shadows are enabled or not.
    bool GetShadowsEnabled() const override;

private:
    /// The lighting management code.
    /// \note The implementation is extracted into a standalone class so a second (scene-delegate
    /// based) implementation can coexist with the scene-index based one.
    std::unique_ptr<class LightingManagerImpl> _impl;
};

} // namespace HVT_NS