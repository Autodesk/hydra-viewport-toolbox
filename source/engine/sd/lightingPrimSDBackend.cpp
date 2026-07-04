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

#include "lightingPrimSDBackend.h"

#include "../../shadow/shadowMatrixComputation.h"

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
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (materialNetworkMap)
);

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace HVT_NS
{

namespace
{

static constexpr float kDistantLightAngle     = 0.53f;
static constexpr float kDistantLightIntensity = 15000.0f;

TfToken GetTexturePath(char const* texture)
{
    static PlugPluginPtr plugin = PlugRegistry::GetInstance().GetPluginWithName("hdx");

    const std::string path = PlugFindPluginResource(plugin, TfStringCatPaths("textures", texture));
    TF_VERIFY(!path.empty(), "Could not find texture: %s\n", texture);

    return TfToken(path);
}

TfToken GetPackageDefaultDomeLightTexture()
{
    HioImageRegistry& hioImageReg = HioImageRegistry::GetInstance();
    static bool useTex            = hioImageReg.IsSupportedImageFile("StinsonBeach.tex");

    static TfToken domeLightTexture =
        (useTex) ? GetTexturePath("StinsonBeach.tex") : GetTexturePath("StinsonBeach.hdr");
    return domeLightTexture;
}

VtValue GetDomeLightTextureValue(GlfSimpleLight const& light)
{
    SdfAssetPath const& domeLightAsset = light.GetDomeLightTextureFile();
    if (domeLightAsset != SdfAssetPath())
    {
        return VtValue(domeLightAsset);
    }
    else
    {
        static VtValue const defaultDomeLightAsset = VtValue(SdfAssetPath(
            GetPackageDefaultDomeLightTexture(), GetPackageDefaultDomeLightTexture()));
#if (TARGET_OS_IPHONE == 1)
        return VtValue(domeLightAsset);
#else
        return defaultDomeLightAsset;
#endif
    }
}

void GetMaterialNetwork(
    SdfPath const& pathName, GlfSimpleLight const& light, HdMaterialNetworkMap& outNetworkMap)
{
    static TfToken const PxrDomeLight("PxrDomeLight");
    static TfToken const PxrDistantLight("PxrDistantLight");

    HdMaterialNetwork lightNetwork;
    HdMaterialNode node;
    node.path       = pathName;
    node.identifier = light.IsDomeLight() ? PxrDomeLight : PxrDistantLight;

    node.parameters[HdLightTokens->intensity] = 1.0f;
    node.parameters[HdLightTokens->exposure]  = 0.0f;
    node.parameters[HdLightTokens->normalize] = false;
    node.parameters[HdLightTokens->color]     = GfVec3f(1, 1, 1);
    node.parameters[HdTokens->transform]      = light.GetTransform();

    if (light.IsDomeLight())
    {
        node.parameters[HdLightTokens->textureFile]  = GetDomeLightTextureValue(light);
        node.parameters[HdLightTokens->shadowEnable] = true;
    }
    else
    {
        GfMatrix4d trans(1.0);
        GfVec4d const& pos = light.GetPosition();
        trans.SetTranslateOnly(GfVec3d(pos[0], pos[1], pos[2]));
        node.parameters[HdTokens->transform]         = trans;
        node.parameters[HdLightTokens->angle]        = kDistantLightAngle;
        node.parameters[HdLightTokens->intensity]    = kDistantLightIntensity;
        node.parameters[HdLightTokens->shadowEnable] = light.HasShadow();
    }
    lightNetwork.nodes.push_back(node);

    outNetworkMap.map.emplace(HdMaterialTerminalTokens->light, lightNetwork);
    outNetworkMap.terminals.push_back(pathName);
}

template <typename T>
T GetValue(SyncDelegatePtr const& syncDelegate, SdfPath const& id, TfToken const& key)
{
    VtValue vParams = syncDelegate->GetValue(id, key);
    return vParams.Get<T>();
}

} // anonymous namespace

///////////////////////////////////////////////////////////////////////////////
// LightingPrimSDBackend
///////////////////////////////////////////////////////////////////////////////

LightingPrimSDBackend::LightingPrimSDBackend(HdRenderIndex* pRenderIndex,
    SyncDelegatePtr const& syncDelegate) :
    _pRenderIndex(pRenderIndex), _syncDelegate(syncDelegate)
{
}

LightingPrimSDBackend::~LightingPrimSDBackend()
{
    RemoveAllLights();
}

void LightingPrimSDBackend::RemoveAllLights()
{
    const TfToken cameraLightType = GetCameraLightType();
    for (auto const& id : _trackedLightIds)
    {
        _pRenderIndex->RemoveSprim(cameraLightType, id);
        _pRenderIndex->RemoveSprim(HdPrimTypeTokens->domeLight, id);
    }
    _trackedLightIds.clear();
}

TfToken LightingPrimSDBackend::GetCameraLightType() const
{
    return _pRenderIndex->IsSprimTypeSupported(HdPrimTypeTokens->simpleLight)
        ? HdPrimTypeTokens->simpleLight
        : HdPrimTypeTokens->distantLight;
}

void LightingPrimSDBackend::SetParameters(SdfPath const& pathName, GlfSimpleLight const& light,
    SyncDelegatePtr& lightDelegate, bool isHighQualityRenderer, GfRange3d const& worldExtent)
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
        lightDelegate->SetValue(pathName, HdLightTokens->textureFile, GetDomeLightTextureValue(light));
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
        lightDelegate->SetValue(pathName, HdLightTokens->angle, VtValue(kDistantLightAngle));
        lightDelegate->SetValue(
            pathName, HdLightTokens->intensity, VtValue(kDistantLightIntensity));
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

void LightingPrimSDBackend::SetMaterialNetwork(
    SdfPath const& pathName, GlfSimpleLight const& light, SyncDelegatePtr& lightDelegate)
{
    HdMaterialNetworkMap networkMap;
    GetMaterialNetwork(pathName, light, networkMap);

    lightDelegate->SetValue(pathName, _tokens->materialNetworkMap, VtValue(networkMap));
}

GlfSimpleLight LightingPrimSDBackend::GetLightAtId(
    size_t pathIdx, SdfPathVector const& lightIds) const
{
    GlfSimpleLight light = GlfSimpleLight();
    if (pathIdx < lightIds.size())
    {
        light = GetValue<GlfSimpleLight>(_syncDelegate, lightIds[pathIdx], HdLightTokens->params);
    }
    return light;
}

void LightingPrimSDBackend::RemoveLightSprim(size_t pathIdx, SdfPathVector const& lightIds)
{
    if (pathIdx < lightIds.size())
    {
        _pRenderIndex->RemoveSprim(GetCameraLightType(), lightIds[pathIdx]);
        _pRenderIndex->RemoveSprim(HdPrimTypeTokens->domeLight, lightIds[pathIdx]);
    }
}

void LightingPrimSDBackend::ReplaceLightSprim(size_t pathIdx, GlfSimpleLight const& light,
    SdfPath const& pathName, GfRange3d const& worldExtent,
    SdfPathVector const& lightIds, bool isHighQualityRenderer)
{
    RemoveLightSprim(pathIdx, lightIds);

    if (light.IsDomeLight())
    {
        _pRenderIndex->InsertSprim(HdPrimTypeTokens->domeLight, _syncDelegate.get(), pathName);
    }
    else
    {
        _pRenderIndex->InsertSprim(
            GetCameraLightType(), _syncDelegate.get(), pathName);
    }

    // Set the parameters for the light and mark as dirty
    SetParameters(pathName, light, _syncDelegate, isHighQualityRenderer, worldExtent);

    // Create a HdMaterialNetworkMap for the light if we are not using Storm
    if (isHighQualityRenderer)
    {
        SetMaterialNetwork(pathName, light, _syncDelegate);
    }

    _pRenderIndex->GetChangeTracker().MarkSprimDirty(pathName, HdLight::AllDirty);

    // Track this light ID for cleanup
    if (std::find(_trackedLightIds.begin(), _trackedLightIds.end(), pathName) == _trackedLightIds.end())
    {
        _trackedLightIds.push_back(pathName);
    }
}

void LightingPrimSDBackend::SyncLightStateAfterReplace(
    size_t pathIdx, GlfSimpleLight const& light, SdfPathVector const& lightIds)
{
    _syncDelegate->SetValue(lightIds[pathIdx], HdLightTokens->params, VtValue(light));
    _syncDelegate->SetValue(
        lightIds[pathIdx], HdTokens->transform, VtValue(light.GetTransform()));

    if (light.IsDomeLight())
    {
        _syncDelegate->SetValue(
            lightIds[pathIdx], HdLightTokens->textureFile, GetDomeLightTextureValue(light));
    }
    _pRenderIndex->GetChangeTracker().MarkSprimDirty(
        lightIds[pathIdx], HdLight::DirtyParams | HdLight::DirtyTransform);
}

void LightingPrimSDBackend::UpdateShadowMatrixComputation(
    size_t pathIdx, GlfSimpleLight const& light, GfRange3d const& worldExtent,
    SdfPathVector const& lightIds)
{
    if (light.HasShadow() || GetLightAtId(pathIdx, lightIds).HasShadow())
    {
        auto shadowParams = GetValue<HdxShadowParams>(
            _syncDelegate, lightIds[pathIdx], HdLightTokens->shadowParams);
        std::shared_ptr<ShadowMatrixComputation> pShadowMatrixComputation =
            std::dynamic_pointer_cast<ShadowMatrixComputation>(shadowParams.shadowMatrix);
        if (pShadowMatrixComputation != nullptr)
            pShadowMatrixComputation->update(GfRange3f(worldExtent), light);
        _pRenderIndex->GetChangeTracker().MarkSprimDirty(
            lightIds[pathIdx], HdLight::DirtyShadowParams);
    }
}

void LightingPrimSDBackend::UpdateCameraLightTransform(size_t pathIdx,
    GlfSimpleLight const& light, GfMatrix4d const& cameraTransform,
    GfRange3d const& /*worldExtent*/, SdfPathVector const& lightIds)
{
    GfMatrix4d const& viewInvMatrix = cameraTransform;
    VtValue trans     = VtValue(viewInvMatrix * light.GetTransform());
    VtValue prevTrans = _syncDelegate->GetValue(lightIds[pathIdx], HdTokens->transform);
    if (viewInvMatrix != GfMatrix4d(1.0) && trans != prevTrans)
    {
        _syncDelegate->SetValue(lightIds[pathIdx], HdTokens->transform, trans);
        _pRenderIndex->GetChangeTracker().MarkSprimDirty(
            lightIds[pathIdx], HdLight::DirtyTransform);
    }
}

} // namespace HVT_NS
