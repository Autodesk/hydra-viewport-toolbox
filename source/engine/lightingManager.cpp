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

#include "lightingManager.h"

#include "shadow/shadowMatrixComputation.h"

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/pxr.h>

#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>
#include <pxr/imaging/hdx/shadowTask.h>
#include <pxr/imaging/hdx/simpleLightTask.h>
#include <pxr/imaging/hio/imageRegistry.h>
#include <pxr/usd/sdf/path.h>

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (materialNetworkMap)
    (PxrDistantLight)
    (PxrDomeLight)
);

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace hvt
{

namespace
{

template <typename T>
T GetParameter(const SyncDelegatePtr& syncDelegate, SdfPath const& id, TfToken const& key)
{
    VtValue vParams = syncDelegate->GetValue(id, key);
    return vParams.Get<T>();
}

// Distant Light values
constexpr const float DISTANT_LIGHT_ANGLE = 0.53;
constexpr float DISTANT_LIGHT_INTENSITY   = 15000.0;

// FIXME: HdxPackageDefaultDomeLightTexture is an USD private method!
// The code below tries to mimic the USD code without using the USD private helpers.

TfToken _GetTexturePath(char const* texture)
{
    static PlugPluginPtr plugin = PlugRegistry::GetInstance().GetPluginWithName("hdx");

    const std::string path = PlugFindPluginResource(plugin, TfStringCatPaths("textures", texture));
    TF_VERIFY(!path.empty(), "Could not find texture: %s\n", texture);

    return TfToken(path);
}

TfToken PackageDefaultDomeLightTexture()
{
    // Use the tex version of the Domelight's environment map if supported
    HioImageRegistry& hioImageReg = HioImageRegistry::GetInstance();
    static bool useTex            = hioImageReg.IsSupportedImageFile("StinsonBeach.tex");

    static TfToken domeLightTexture =
        (useTex) ? _GetTexturePath("StinsonBeach.tex") : _GetTexturePath("StinsonBeach.hdr");
    return domeLightTexture;
}

} // anonymous namespace

class LightingManager::Impl
{

    SdfPathVector _excludedLights;
    bool _enableShadows { true };

    /// The parent identifier for light Sprims that are added to the render index by the
    /// LightingManager.
    const SdfPath _lightRootPath;

    /// The render index used to insert and remove light Sprims.
    HdRenderIndex* _pRenderIndex { nullptr };

    /// The scene delegate used to provide light Sprim data.
    SyncDelegatePtr _lightDelegate;

    /// High quality renderer support material networks for lighting.
    bool _isHighQualityRenderer { false };

    /// Lighting context stores information of the view light attributes params.
    GlfSimpleLightingContextRefPtr _lightingState;

public:
    explicit Impl(SdfPath const& lightRootPath, HdRenderIndex* pRenderIndex,
        SyncDelegatePtr& lightDelegate, bool sHighQualityRenderer) :
        _lightRootPath(lightRootPath),
        _pRenderIndex(pRenderIndex),
        _lightDelegate(lightDelegate),
        _isHighQualityRenderer(sHighQualityRenderer)
    {
        _lightingState = GlfSimpleLightingContext::New();
    }

    /// Set the lighting state for the scene.
    /// \param pFreeCameraSceneDelegate The viewport camera.
    /// \param worldExtent The world extents for the scene. Used by things like shadows, etc.
    void ProcessLightingState(HdxFreeCameraSceneDelegate* pFreeCameraSceneDelegate,
        const GfRange3d& worldExtent);

    void SetEnableShadows(bool enable);
    void SetExcludedLights(SdfPathVector const& excludedLights);

    const GlfSimpleLightingContextPtr GetLightingContext() const;
    const SdfPathVector& GetExcludedLights() const;
    bool GetShadowsEnabled() const;
    void CleanUp();

private:
    // Built-in lights.
    SdfPathVector _lightIds;

    // Helper functions to set up the lighting state for the built-in lights.
    // Note: this helper function could be static or external.
    bool SupportBuiltInLightTypes(const HdRenderIndex* index) const;

    void SetBuiltInLightingState(HdxFreeCameraSceneDelegate* pFreeCameraSceneDelegate,
        const GfRange3d& worldExtent);

    // Helper function to get the built-in Camera light type SimpleLight for
    // Storm, and DistantLight otherwise.
    TfToken GetCameraLightType(const HdRenderIndex* pRenderIndex) const;

    // Helper functions to set the parameters of a light, get a particular light
    // in the scene, replace and remove Sprims from the scene
    VtValue GetDomeLightTexture(GlfSimpleLight const& light) const;
    void SetParameters(const SdfPath& pathName, const GlfSimpleLight& light,
        SyncDelegatePtr& lightDelegate, bool isHighQualityRenderer, const GfRange3d& worldExtent);
    void SetMaterialNetwork(
        SdfPath const& pathName, GlfSimpleLight const& light, SyncDelegatePtr& lightDelegate);
    void GetMaterialNetwork(SdfPath const& pathName, GlfSimpleLight const& light,
        HdMaterialNetworkMap& outNetworkMap) const;
    GlfSimpleLight GetLightAtId(size_t const& pathIdx, const SyncDelegatePtr& lightDelegate);

    void RemoveLightSprim(size_t const& pathIdx);
    void ReplaceLightSprim(size_t const& pathIdx, GlfSimpleLight const& light,
        SdfPath const& pathName, const GfRange3d& worldExtent);

    void SetRenderParams(SdfPath const& shadowTaskId, bool enableSceneMaterials,
        HdRenderIndex* pRenderIndex, SyncDelegatePtr& shadowTaskDelegate);
};

VtValue LightingManager::Impl::GetDomeLightTexture(GlfSimpleLight const& light) const
{
    SdfAssetPath const& domeLightAsset = light.GetDomeLightTextureFile();
    if (domeLightAsset != SdfAssetPath())
    {
        return VtValue(domeLightAsset);
    }
    else
    {
        static VtValue const defaultDomeLightAsset = VtValue(
            SdfAssetPath(PackageDefaultDomeLightTexture(), PackageDefaultDomeLightTexture()));
#if (TARGET_OS_IPHONE == 1)
        // FIXME: iPhone or iPad can only support RGBA16float, but the HDR file
        // is RGBA32float, need to convert after loaded. Disable it temporarily.
        return VtValue(domeLightAsset);
#else
        return defaultDomeLightAsset;
#endif
    }
}

void LightingManager::Impl::SetParameters(SdfPath const& pathName, GlfSimpleLight const& light,
    SyncDelegatePtr& lightDelegate, bool isHighQualityRenderer, const GfRange3d& worldExtent)
{
    lightDelegate->SetValue(pathName, HdLightTokens->intensity, VtValue(1.0f));
    lightDelegate->SetValue(pathName, HdLightTokens->exposure, VtValue(0.0f));
    lightDelegate->SetValue(pathName, HdLightTokens->normalize, VtValue(false));
    lightDelegate->SetValue(pathName, HdLightTokens->color, VtValue(GfVec3f(1, 1, 1)));
    lightDelegate->SetValue(pathName, HdTokens->transform, VtValue(light.GetTransform()));
    lightDelegate->SetValue(pathName, HdLightTokens->shadowParams,
        VtValue(HdxShadowParams())); // By default, we pass empty shadow parameters
    lightDelegate->SetValue(pathName, HdLightTokens->shadowCollection, VtValue());
    lightDelegate->SetValue(pathName, HdLightTokens->params, VtValue(light));

    // If this is a dome light add the domelight texture resource.
    if (light.IsDomeLight())
    {
        lightDelegate->SetValue(pathName, HdLightTokens->textureFile, GetDomeLightTexture(light));
        lightDelegate->SetValue(pathName, HdLightTokens->shadowEnable, VtValue(false));
    }
    // When not using storm, initialize the camera light transform based on
    // the SimpleLight position
    else if (isHighQualityRenderer)
    {
        GfMatrix4d trans(1.0);
        const GfVec4d& pos = light.GetPosition();
        trans.SetTranslateOnly(GfVec3d(pos[0], pos[1], pos[2]));
        lightDelegate->SetValue(pathName, HdTokens->transform, VtValue(trans));

        // Initialize distant light specific parameters
        lightDelegate->SetValue(pathName, HdLightTokens->angle, VtValue(DISTANT_LIGHT_ANGLE));
        lightDelegate->SetValue(
            pathName, HdLightTokens->intensity, VtValue(DISTANT_LIGHT_INTENSITY));
        lightDelegate->SetValue(pathName, HdLightTokens->shadowEnable, VtValue(false));
    }

    // Update for shadows
    if (light.HasShadow())
    {
        HdxShadowParams shadowParams;
        shadowParams.enabled      = light.HasShadow();
        shadowParams.resolution   = light.GetShadowResolution();
        shadowParams.blur         = (double)light.GetShadowBlur();
        shadowParams.shadowMatrix = HdxShadowMatrixComputationSharedPtr(
            new ShadowMatrixComputation(GfRange3f(worldExtent), light));

        lightDelegate->SetValue(pathName, HdLightTokens->shadowParams, VtValue(shadowParams));

        SdfPathVector shadowCollectionExcludePaths;
        HdRprimCollection collection(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));
        collection.SetExcludePaths(shadowCollectionExcludePaths);

        lightDelegate->SetValue(pathName, HdLightTokens->shadowCollection, VtValue(collection));
    }
}

void LightingManager::Impl::SetMaterialNetwork(
    SdfPath const& pathName, GlfSimpleLight const& light, SyncDelegatePtr& lightDelegate)
{
    HdMaterialNetworkMap networkMap;
    GetMaterialNetwork(pathName, light, networkMap);

    lightDelegate->SetValue(pathName, _tokens->materialNetworkMap, VtValue(networkMap));
}

void LightingManager::Impl::GetMaterialNetwork(
    SdfPath const& pathName, GlfSimpleLight const& light, HdMaterialNetworkMap& outNetworkMap) const
{
    // Build a HdMaterialNetwork for the Light
    HdMaterialNetwork lightNetwork;
    HdMaterialNode node;
    node.path = pathName;
    // XXX Using these Pxr**Light tokens works for now since HdPrman is
    // currently the only renderer that supports material networks for lights.
    node.identifier = light.IsDomeLight() ? _tokens->PxrDomeLight : _tokens->PxrDistantLight;

    // Initialize parameters - same as above, but without Storm specific
    // parameters (ShadowParams, ShadowCollection, params)
    node.parameters[HdLightTokens->intensity] = 1.0f;
    node.parameters[HdLightTokens->exposure]  = 0.0f;
    node.parameters[HdLightTokens->normalize] = false;
    node.parameters[HdLightTokens->color]     = GfVec3f(1, 1, 1);
    node.parameters[HdTokens->transform]      = light.GetTransform();

    if (light.IsDomeLight())
    {
        // For the domelight, add the domelight texture resource.
        node.parameters[HdLightTokens->textureFile]  = GetDomeLightTexture(light);
        node.parameters[HdLightTokens->shadowEnable] = true;
    }
    else
    {
        // For the camera light, initialize the transform based on the
        // SimpleLight position
        GfMatrix4d trans(1.0);
        const GfVec4d& pos = light.GetPosition();
        trans.SetTranslateOnly(GfVec3d(pos[0], pos[1], pos[2]));
        node.parameters[HdTokens->transform] = trans;

        // Initialize distant light specific parameters
        node.parameters[HdLightTokens->angle]        = DISTANT_LIGHT_ANGLE;
        node.parameters[HdLightTokens->intensity]    = DISTANT_LIGHT_INTENSITY;
        node.parameters[HdLightTokens->shadowEnable] = light.HasShadow();
    }
    lightNetwork.nodes.push_back(node);

    // Material network maps for lights will contain a single network with the
    // terminal name "light'.
    outNetworkMap.map.emplace(HdMaterialTerminalTokens->light, lightNetwork);
    outNetworkMap.terminals.push_back(pathName);
}

TfToken LightingManager::Impl::GetCameraLightType(const HdRenderIndex* pRenderIndex) const
{
    return pRenderIndex->IsSprimTypeSupported(HdPrimTypeTokens->simpleLight)
        ? HdPrimTypeTokens->simpleLight
        : HdPrimTypeTokens->distantLight;
}

GlfSimpleLight LightingManager::Impl::GetLightAtId(
    size_t const& pathIdx, const SyncDelegatePtr& lightDelegate)
{
    GlfSimpleLight light = GlfSimpleLight();
    if (pathIdx < _lightIds.size())
    {
        light =
            GetParameter<GlfSimpleLight>(lightDelegate, _lightIds[pathIdx], HdLightTokens->params);
    }
    return light;
}

void LightingManager::Impl::RemoveLightSprim(size_t const& pathIdx)
{
    if (pathIdx < _lightIds.size())
    {
        _pRenderIndex->RemoveSprim(GetCameraLightType(_pRenderIndex), _lightIds[pathIdx]);
        _pRenderIndex->RemoveSprim(HdPrimTypeTokens->domeLight, _lightIds[pathIdx]);
    }
}

void LightingManager::Impl::ReplaceLightSprim(size_t const& pathIdx, GlfSimpleLight const& light,
    SdfPath const& pathName, const GfRange3d& worldExtent)
{
    RemoveLightSprim(pathIdx);

    if (light.IsDomeLight())
    {
        _pRenderIndex->InsertSprim(HdPrimTypeTokens->domeLight, _lightDelegate.get(), pathName);
    }
    else
    {
        _pRenderIndex->InsertSprim(
            GetCameraLightType(_pRenderIndex), _lightDelegate.get(), pathName);
    }

    // Set the parameters for the light and mark as dirty
    SetParameters(pathName, light, _lightDelegate, _isHighQualityRenderer, worldExtent);

    // Create a HdMaterialNetworkMap for the light if we are not using Storm
    // const DelegateAdaptor delegateAdaptor(lightDelegate);
    // DelegateAdaptor delegateAdaptor(lightDelegate);
    if (_isHighQualityRenderer)
    {
        SetMaterialNetwork(pathName, light, _lightDelegate);
    }

    _pRenderIndex->GetChangeTracker().MarkSprimDirty(pathName, HdLight::AllDirty);
}

bool LightingManager::Impl::SupportBuiltInLightTypes(const HdRenderIndex* index) const
{
    // Verify that the renderDelegate supports the light types for the built-in
    // dome and camera lights.
    // Dome Light
    bool dome = index->IsSprimTypeSupported(HdPrimTypeTokens->domeLight);
    // Camera Light
    bool camera = (index->IsSprimTypeSupported(HdPrimTypeTokens->simpleLight) ||
        index->IsSprimTypeSupported(HdPrimTypeTokens->distantLight));
    return dome && camera;
}

void LightingManager::Impl::SetBuiltInLightingState(
    HdxFreeCameraSceneDelegate* pFreeCameraSceneDelegate, const GfRange3d& worldExtent)
{

    GlfSimpleLightVector const& activeLights = _lightingState->GetLights();

    // If we need to add lights to the _lightIds vector.
    if (_lightIds.size() < activeLights.size())
    {

        // Cycle through the active lights, add the new light and make sure
        // the Sprim at _lightIds[i] matches activeLights[i]
        for (size_t i = 0; i < activeLights.size(); ++i)
        {

            // Get or create the light path for activeLights[i]
            bool needToAddLightPath = false;
            SdfPath lightPath       = SdfPath();
            if (i >= _lightIds.size())
            {
                lightPath = _lightRootPath.AppendChild(
                    TfToken(TfStringPrintf("light%d", (int)_lightIds.size())));
                needToAddLightPath = true;
            }
            else
            {
                lightPath = _lightIds[i];
            }
            // Make sure the light at _lightIds[i] matches activeLights[i]
            if (GetLightAtId(i, _lightDelegate) != activeLights[i])
            {
                ReplaceLightSprim(i, activeLights[i], lightPath, worldExtent);
            }
            if (needToAddLightPath)
            {
                _lightIds.push_back(lightPath);
            }
        }
    }

    // If we need to remove lights from the _lightIds vector
    else if (_lightIds.size() > activeLights.size())
    {

        // Cycle through the active lights and make sure the Sprim at
        // _lightIds[i] matchs activeLights[i]
        for (size_t i = 0; i < activeLights.size(); ++i)
        {

            // Get the light path for activeLights[i]
            SdfPath lightPath = _lightIds[i];

            // Make sure the light at _lightIds[i] matches activeLights[i]
            if (GetLightAtId(i, _lightDelegate) != activeLights[i])
            {
                ReplaceLightSprim(i, activeLights[i], lightPath, worldExtent);
            }
        }
        // Now that everything matches, remove the last item in _lightIds
        RemoveLightSprim(_lightIds.size() - 1);
        _lightIds.pop_back();
    }

    // If there has been no change in the number of lights we still may need to
    // update the light parameters eg. if the free camera has moved
    for (size_t i = 0; i < activeLights.size(); ++i)
    {

        // Make sure the light parameters and transform match
        GlfSimpleLight const& activeLight = activeLights[i];
        if (GetLightAtId(i, _lightDelegate) != activeLight)
        {
            // Any light parameter may have changed -- update them
            ReplaceLightSprim(i, activeLight, _lightIds[i], worldExtent);

            _lightDelegate->SetValue(_lightIds[i], HdLightTokens->params, VtValue(activeLight));
            _lightDelegate->SetValue(
                _lightIds[i], HdTokens->transform, VtValue(activeLight.GetTransform()));

            if (activeLight.IsDomeLight())
            {
                _lightDelegate->SetValue(
                    _lightIds[i], HdLightTokens->textureFile, GetDomeLightTexture(activeLight));
            }
            _pRenderIndex->GetChangeTracker().MarkSprimDirty(
                _lightIds[i], HdLight::DirtyParams | HdLight::DirtyTransform);

            // Update shadow computation if applicable
            if (activeLight.HasShadow() || GetLightAtId(i, _lightDelegate).HasShadow())
            {
                auto shadowParams = GetParameter<HdxShadowParams>(
                    _lightDelegate, _lightIds[i], HdLightTokens->shadowParams);
                std::shared_ptr<ShadowMatrixComputation> pShadowMatrixComputation =
                    std::dynamic_pointer_cast<ShadowMatrixComputation>(shadowParams.shadowMatrix);
                if (pShadowMatrixComputation != nullptr)
                    pShadowMatrixComputation->update(GfRange3f(worldExtent), activeLights[i]);
                _pRenderIndex->GetChangeTracker().MarkSprimDirty(
                    _lightIds[i], HdLight::DirtyShadowParams);
            }
        }

        // Update the camera light transform if needed
        // NOTE: previously, an empty _simpleLightTaskId was used as a condition here.
        //       It is assumed that _simpleLightTaskId is empty when NOT using HdStorm.
        if (_isHighQualityRenderer && !activeLight.IsDomeLight())
        {
            GfMatrix4d const& viewInvMatrix =
                pFreeCameraSceneDelegate->GetTransform(pFreeCameraSceneDelegate->GetCameraId());
            VtValue trans     = VtValue(viewInvMatrix * activeLight.GetTransform());
            VtValue prevTrans = _lightDelegate->GetValue(_lightIds[i], HdTokens->transform);
            if (viewInvMatrix != GfMatrix4d(1.0) && trans != prevTrans)
            {
                _lightDelegate->SetValue(_lightIds[i], HdTokens->transform, trans);
                _pRenderIndex->GetChangeTracker().MarkSprimDirty(
                    _lightIds[i], HdLight::DirtyTransform);
            }
        }
    }
}

const GlfSimpleLightingContextPtr LightingManager::Impl::GetLightingContext() const
{
    return _lightingState;
}

void LightingManager::Impl::SetExcludedLights(SdfPathVector const& excludedLights)
{
    _excludedLights = excludedLights;
}

const SdfPathVector& LightingManager::Impl::GetExcludedLights() const
{
    return _excludedLights;
}

void LightingManager::Impl::SetEnableShadows(bool enable)
{
    _enableShadows = enable;
}

bool LightingManager::Impl::GetShadowsEnabled() const
{
    return _enableShadows;
}

void LightingManager::Impl::ProcessLightingState(
    HdxFreeCameraSceneDelegate* pFreeCameraSceneDelegate, const GfRange3d& worldExtent)
{
    if (!_lightingState)
    {
        TF_CODING_ERROR("Null lighting context");
        return;
    }

    if (_isHighQualityRenderer && !SupportBuiltInLightTypes(_pRenderIndex))
    {
        return;
    }

    // Process the Built-in lights
    SetBuiltInLightingState(pFreeCameraSceneDelegate, worldExtent);
}

void LightingManager::Impl::CleanUp()
{
    const TfToken cameraLightType = GetCameraLightType(_pRenderIndex);
    for (auto const& id : _lightIds)
    {
        _pRenderIndex->RemoveSprim(cameraLightType, id);
        _pRenderIndex->RemoveSprim(HdPrimTypeTokens->domeLight, id);
    }
}

void LightingManager::Impl::SetRenderParams(SdfPath const& shadowTaskId, bool enableSceneMaterials,
    HdRenderIndex* pRenderIndex, SyncDelegatePtr& shadowTaskDelegate)
{
    // Update shadow task in case materials have been enabled/disabled
    if (!shadowTaskId.IsEmpty())
    {
        HdxShadowTaskParams oldShParams =
            GetParameter<HdxShadowTaskParams>(shadowTaskDelegate, shadowTaskId, HdTokens->params);

        if (oldShParams.enableSceneMaterials != enableSceneMaterials)
        {
            oldShParams.enableSceneMaterials = enableSceneMaterials;
            shadowTaskDelegate->SetValue(shadowTaskId, HdTokens->params, VtValue(oldShParams));
            pRenderIndex->GetChangeTracker().MarkTaskDirty(
                shadowTaskId, HdChangeTracker::DirtyParams);
        }
    }
}

LightingManager::LightingManager(SdfPath const& lightRootPath, HdRenderIndex* pRenderIndex,
    SyncDelegatePtr& syncDelegate, bool isHighQualityRenderer)
{
    _impl =
        std::make_unique<Impl>(lightRootPath, pRenderIndex, syncDelegate, isHighQualityRenderer);
}

LightingManager::~LightingManager()
{
    _impl->CleanUp();
}

const GlfSimpleLightingContextPtr LightingManager::GetLightingContext() const
{
    return _impl->GetLightingContext();
}

void LightingManager::SetExcludedLights(SdfPathVector const& excludedLights)
{
    _impl->SetExcludedLights(excludedLights);
}

const SdfPathVector& LightingManager::GetExcludedLights() const
{
    return _impl->GetExcludedLights();
}

void LightingManager::SetEnableShadows(bool enable)
{
    _impl->SetEnableShadows(enable);
}

bool LightingManager::GetShadowsEnabled() const
{
    return _impl->GetShadowsEnabled();
}

void LightingManager::SetLighting(GlfSimpleLightVector const& lights,
    GlfSimpleMaterial const& material, GfVec4f const& ambient,
    HdxFreeCameraSceneDelegate* pCamera, const GfRange3d& worldExtent)
{
    if (lights.size() > 0)
    {
        const GlfSimpleLightingContextPtr lightingState = _impl->GetLightingContext();

        lightingState->SetUseLighting(true);
        lightingState->SetLights(lights);
        lightingState->SetSceneAmbient(ambient);
        lightingState->SetMaterial(material);
    }
    else
    {
        _impl->GetLightingContext()->SetUseLighting(false);
    }

    _impl->ProcessLightingState(pCamera, worldExtent);
}

} // namespace hvt
