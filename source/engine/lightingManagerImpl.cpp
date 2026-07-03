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

#include "lightingManagerImpl.h"

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hio/imageRegistry.h>

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

TfToken LightingManagerImpl::GetTexturePath(char const* texture)
{
    static PlugPluginPtr plugin = PlugRegistry::GetInstance().GetPluginWithName("hdx");

    const std::string path = PlugFindPluginResource(plugin, TfStringCatPaths("textures", texture));
    TF_VERIFY(!path.empty(), "Could not find texture: %s\n", texture);

    return TfToken(path);
}

TfToken LightingManagerImpl::GetPackageDefaultDomeLightTexture()
{
    HioImageRegistry& hioImageReg = HioImageRegistry::GetInstance();
    static bool useTex            = hioImageReg.IsSupportedImageFile("StinsonBeach.tex");

    static TfToken domeLightTexture =
        (useTex) ? GetTexturePath("StinsonBeach.tex") : GetTexturePath("StinsonBeach.hdr");
    return domeLightTexture;
}

VtValue LightingManagerImpl::GetDomeLightTextureValue(GlfSimpleLight const& light)
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

void LightingManagerImpl::GetMaterialNetwork(
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

void LightingManagerImpl::SetBuiltInLightingState(
    GfMatrix4d const& cameraTransform, GfRange3d const& worldExtent)
{
    GlfSimpleLightVector const& activeLights = _lightingState->GetLights();

    // If we need to add lights to the _lightIds vector.
    if (_lightIds.size() < activeLights.size())
    {
        for (size_t i = 0; i < activeLights.size(); ++i)
        {
            bool needToAddLightPath = false;
            SdfPath lightPath;
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
            if (GetLightAtId(i) != activeLights[i])
            {
                ReplaceLightSprim(i, activeLights[i], lightPath, worldExtent);
            }
            if (needToAddLightPath)
            {
                _lightIds.push_back(lightPath);
            }
        }
    }
    // If we need to remove lights from the _lightIds vector.
    else if (_lightIds.size() > activeLights.size())
    {
        for (size_t i = 0; i < activeLights.size(); ++i)
        {
            SdfPath lightPath = _lightIds[i];
            if (GetLightAtId(i) != activeLights[i])
            {
                ReplaceLightSprim(i, activeLights[i], lightPath, worldExtent);
            }
        }
        RemoveLightSprim(_lightIds.size() - 1);
        _lightIds.pop_back();
    }

    // If there has been no change in the number of lights we still may need to
    // update the light parameters eg. if the free camera has moved.
    for (size_t i = 0; i < activeLights.size(); ++i)
    {
        GlfSimpleLight const& activeLight = activeLights[i];
        if (GetLightAtId(i) != activeLight)
        {
            ReplaceLightSprim(i, activeLight, _lightIds[i], worldExtent);
            SyncLightStateAfterReplace(i, activeLight);
            UpdateShadowMatrixComputation(i, activeLight, worldExtent);
        }

        if (_isHighQualityRenderer && !activeLight.IsDomeLight())
        {
            UpdateCameraLightTransform(i, activeLight, cameraTransform, worldExtent);
        }
    }
}

} // namespace HVT_NS
