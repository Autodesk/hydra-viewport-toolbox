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

#include "lightingManagerSIImpl.h"

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
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/lightSchema.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/imaging/hdx/shadowTask.h>
#include <pxr/imaging/hdx/simpleLightTask.h>
#include <pxr/imaging/hio/imageRegistry.h>
#include <pxr/usd/sdf/path.h>

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

#include <optional>

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
    (PxrDistantLight)
    (PxrDomeLight)
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

// Distant Light values
constexpr float DISTANT_LIGHT_ANGLE = 0.53f;
constexpr float DISTANT_LIGHT_INTENSITY   = 15000.0f;

// NOTE: The following implementation avoids using USD private methods like
// HdxPackageDefaultDomeLightTexture. This approach replicates the behavior of the USD code while
// adhering to public API usage.

TfToken _GetTexturePath(char const* texture)
{
    static PlugPluginPtr plugin = PlugRegistry::GetInstance().GetPluginWithName("hdx");

    const std::string path = PlugFindPluginResource(plugin, TfStringCatPaths("textures", texture));
    TF_VERIFY(!path.empty(), "Could not find texture: %s\n", texture);

    return TfToken(path);
}

TfToken _GetPackageDefaultDomeLightTexture()
{
    // Use the tex version of the Domelight's environment map if supported
    HioImageRegistry& hioImageReg = HioImageRegistry::GetInstance();
    static bool useTex            = hioImageReg.IsSupportedImageFile("StinsonBeach.tex");

    static TfToken domeLightTexture =
        (useTex) ? _GetTexturePath("StinsonBeach.tex") : _GetTexturePath("StinsonBeach.hdr");
    return domeLightTexture;
}

VtValue _GetDomeLightTextureValue(GlfSimpleLight const& light)
{
    SdfAssetPath const& domeLightAsset = light.GetDomeLightTextureFile();
    if (domeLightAsset != SdfAssetPath())
    {
        return VtValue(domeLightAsset);
    }
    else
    {
        static VtValue const defaultDomeLightAsset = VtValue(
            SdfAssetPath(_GetPackageDefaultDomeLightTexture(), _GetPackageDefaultDomeLightTexture()));
#if (TARGET_OS_IPHONE == 1)
        // TODO: iOS devices currently support RGBA16float, whereas the HDR file
        // format is RGBA32float. Conversion is required after loading, so this
        // functionality is temporarily disabled.
        return VtValue(domeLightAsset);
#else
        return defaultDomeLightAsset;
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////
// LightSchemaDataSource - provides light schema data for a light prim
///////////////////////////////////////////////////////////////////////////////

class LightSchemaDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(LightSchemaDataSource)

    std::shared_ptr<GlfSimpleLight const> light;
    std::shared_ptr<HdxShadowParams const> shadowParams;
    bool isDomeLight;
    bool isHighQualityRenderer;
    GfRange3d worldExtent;
    TfToken primType;

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (!light)
            return nullptr;

        GlfSimpleLight const& l = *light;

        if (name == HdLightTokens->intensity)
        {
            float intensity =
                (primType == HdPrimTypeTokens->distantLight) ? DISTANT_LIGHT_INTENSITY : 1.0f;
            return HdRetainedTypedSampledDataSource<float>::New(intensity);
        }
        if (name == HdLightTokens->exposure)
            return HdRetainedTypedSampledDataSource<float>::New(0.0f);
        if (name == HdLightTokens->normalize)
            return HdRetainedTypedSampledDataSource<bool>::New(false);
        if (name == HdLightTokens->color)
            return HdRetainedTypedSampledDataSource<GfVec3f>::New(GfVec3f(1, 1, 1));
        if (name == HdLightTokens->params)
            return HdRetainedTypedSampledDataSource<GlfSimpleLight>::New(*light);
        if (name == HdLightTokens->shadowEnable)
            return HdRetainedTypedSampledDataSource<bool>::New(isDomeLight ? false : l.HasShadow());
        if (name == HdLightTokens->angle && !isDomeLight)
            return HdRetainedTypedSampledDataSource<float>::New(DISTANT_LIGHT_ANGLE);

        if (name == HdLightTokens->textureFile && isDomeLight)
            return HdRetainedTypedSampledDataSource<SdfAssetPath>::New(
                _GetDomeLightTextureValue(l).Get<SdfAssetPath>());

        if (name == HdLightTokens->shadowParams && shadowParams)
            return HdRetainedTypedSampledDataSource<HdxShadowParams>::New(*shadowParams);
        if (name == HdLightTokens->shadowParams)
            return HdRetainedTypedSampledDataSource<HdxShadowParams>::New(HdxShadowParams());

        if (name == HdLightTokens->shadowCollection)
        {
            SdfPathVector shadowCollectionExcludePaths;
            HdRprimCollection collection(
                HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));
            collection.SetExcludePaths(shadowCollectionExcludePaths);
            return HdRetainedTypedSampledDataSource<HdRprimCollection>::New(collection);
        }

        return nullptr;
    }

    TfTokenVector GetNames() override
    {
        TfTokenVector names = { HdLightTokens->intensity, HdLightTokens->exposure,
            HdLightTokens->normalize, HdLightTokens->color, HdLightTokens->params,
            HdLightTokens->shadowParams, HdLightTokens->shadowCollection };
        if (isDomeLight)
        {
            names.push_back(HdLightTokens->textureFile);
            names.push_back(HdLightTokens->shadowEnable);
        }
        else
        {
            names.push_back(HdLightTokens->angle);
            names.push_back(HdLightTokens->shadowEnable);
        }
        return names;
    }

    static HdContainerDataSourceHandle New(std::shared_ptr<GlfSimpleLight const> const& lightPtr,
        std::shared_ptr<HdxShadowParams const> const& shadowParamsPtr, bool isDomeLight,
        bool isHighQualityRenderer, GfRange3d const& worldExtent, TfToken const& primType)
    {
        return HdContainerDataSourceHandle(new LightSchemaDataSource(
            lightPtr, shadowParamsPtr, isDomeLight, isHighQualityRenderer, worldExtent, primType));
    }

private:
    LightSchemaDataSource(std::shared_ptr<GlfSimpleLight const> const& lightPtr,
        std::shared_ptr<HdxShadowParams const> const& shadowParamsPtr, bool isDomeLight,
        bool isHighQualityRenderer, GfRange3d const& worldExtent, TfToken const& primType) :
        light(lightPtr),
        shadowParams(shadowParamsPtr),
        isDomeLight(isDomeLight),
        isHighQualityRenderer(isHighQualityRenderer),
        worldExtent(worldExtent),
        primType(primType)
    {
    }
};

HD_DECLARE_DATASOURCE_HANDLES(LightSchemaDataSource);

///////////////////////////////////////////////////////////////////////////////
// LightPrimDataSource - provides prim-level data for a light (schema + xform + material)
///////////////////////////////////////////////////////////////////////////////

class LightPrimDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(LightPrimDataSource)

    std::shared_ptr<GlfSimpleLight const> light;
    std::shared_ptr<HdxShadowParams const> shadowParams;
    SdfPath path;
    bool isDomeLight;
    bool isHighQualityRenderer;
    GfRange3d worldExtent;
    TfToken primType;
    std::optional<GfMatrix4d> cameraLightTransformOverride;

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (!light)
            return nullptr;

        if (name == HdLightSchema::GetSchemaToken())
        {
            return LightSchemaDataSource::New(
                light, shadowParams, isDomeLight, isHighQualityRenderer, worldExtent, primType);
        }

        if (name == HdXformSchema::GetSchemaToken())
        {
            GfMatrix4d xform;
            if (cameraLightTransformOverride.has_value())
            {
                xform = *cameraLightTransformOverride;
            }
            else if (isHighQualityRenderer && !isDomeLight)
            {
                GfMatrix4d trans(1.0);
                const GfVec4d& pos = light->GetPosition();
                trans.SetTranslateOnly(GfVec3d(pos[0], pos[1], pos[2]));
                xform = trans;
            }
            else
            {
                xform = light->GetTransform();
            }
            return HdXformSchema::Builder()
                .SetMatrix(HdRetainedTypedSampledDataSource<GfMatrix4d>::New(xform))
                .Build();
        }

        if (name == _tokens->materialNetworkMap && isHighQualityRenderer)
        {
            // Build material network for HdPrman (legacy materialNetworkMap approach)
            HdMaterialNetworkMap networkMap;
            HdMaterialNetwork lightNetwork;
            HdMaterialNode node;
            node.path       = path;
            node.identifier = isDomeLight ? _tokens->PxrDomeLight : _tokens->PxrDistantLight;
            node.parameters[HdLightTokens->intensity] = 1.0f;
            node.parameters[HdLightTokens->exposure]  = 0.0f;
            node.parameters[HdLightTokens->normalize] = false;
            node.parameters[HdLightTokens->color]     = GfVec3f(1, 1, 1);
            node.parameters[HdTokens->transform]      = light->GetTransform();

            if (isDomeLight)
            {
                node.parameters[HdLightTokens->textureFile]  = _GetDomeLightTextureValue(*light);
                node.parameters[HdLightTokens->shadowEnable] = true;
            }
            else
            {
                GfMatrix4d trans(1.0);
                GfVec4d const& pos = light->GetPosition();
                trans.SetTranslateOnly(GfVec3d(pos[0], pos[1], pos[2]));
                node.parameters[HdTokens->transform]         = trans;
                node.parameters[HdLightTokens->angle]        = DISTANT_LIGHT_ANGLE;
                node.parameters[HdLightTokens->intensity]    = DISTANT_LIGHT_INTENSITY;
                node.parameters[HdLightTokens->shadowEnable] = light->HasShadow();
            }
            lightNetwork.nodes.push_back(node);
            networkMap.map.emplace(HdMaterialTerminalTokens->light, lightNetwork);
            networkMap.terminals.push_back(path);

            return HdRetainedTypedSampledDataSource<HdMaterialNetworkMap>::New(networkMap);
        }

        return nullptr;
    }

    TfTokenVector GetNames() override
    {
        TfTokenVector names = { HdLightSchema::GetSchemaToken(), HdXformSchema::GetSchemaToken() };
        if (isHighQualityRenderer)
            names.push_back(_tokens->materialNetworkMap);
        return names;
    }

    static HdContainerDataSourceHandle New(std::shared_ptr<GlfSimpleLight const> const& lightPtr,
        std::shared_ptr<HdxShadowParams const> const& shadowParamsPtr, SdfPath const& path,
        bool isDomeLight, bool isHighQualityRenderer, GfRange3d const& worldExtent,
        TfToken const& primType, std::optional<GfMatrix4d> const& transformOverride = std::nullopt)
    {
        return HdContainerDataSourceHandle(new LightPrimDataSource(lightPtr, shadowParamsPtr, path,
            isDomeLight, isHighQualityRenderer, worldExtent, primType, transformOverride));
    }

private:
    LightPrimDataSource(std::shared_ptr<GlfSimpleLight const> const& lightPtr,
        std::shared_ptr<HdxShadowParams const> const& shadowParamsPtr, SdfPath const& path,
        bool isDomeLight, bool isHighQualityRenderer, GfRange3d const& worldExtent,
        TfToken const& primType, std::optional<GfMatrix4d> const& transformOverride) :
        light(lightPtr),
        shadowParams(shadowParamsPtr),
        path(path),
        isDomeLight(isDomeLight),
        isHighQualityRenderer(isHighQualityRenderer),
        worldExtent(worldExtent),
        primType(primType),
        cameraLightTransformOverride(transformOverride)
    {
    }
};

HD_DECLARE_DATASOURCE_HANDLES(LightPrimDataSource);

} // anonymous namespace

///////////////////////////////////////////////////////////////////////////////
// LightingManagerSIImpl
///////////////////////////////////////////////////////////////////////////////

TfToken LightingManagerSIImpl::GetCameraLightType(const HdRenderIndex* pRenderIndex) const
{
    return pRenderIndex->IsSprimTypeSupported(HdPrimTypeTokens->simpleLight)
        ? HdPrimTypeTokens->simpleLight
        : HdPrimTypeTokens->distantLight;
}

GlfSimpleLight const& LightingManagerSIImpl::GetLightAtId(size_t pathIdx) const
{
    if (pathIdx < _lightIds.size())
    {
        auto it = _lightData.find(_lightIds[pathIdx]);
        if (it != _lightData.end())
            return it->second;
    }
    static const GlfSimpleLight light;
    return light;
}

void LightingManagerSIImpl::RemoveLightSprim(size_t pathIdx)
{
    if (pathIdx < _lightIds.size() && _retainedSceneIndex)
    {
        SdfPath const& path = _lightIds[pathIdx];
        _retainedSceneIndex->RemovePrims({ { path } });
        _lightData.erase(path);
        _shadowMatrixComputations.erase(path);
    }
}

void LightingManagerSIImpl::ReplaceLightSprim(size_t pathIdx, GlfSimpleLight const& light,
    SdfPath const& pathName, GfRange3d const& worldExtent,
    std::optional<GfMatrix4d> const& cameraLightTransformOverride)
{
    if (!_retainedSceneIndex)
        return;

    bool isDomeLight = light.IsDomeLight();
    TfToken primType =
        isDomeLight ? HdPrimTypeTokens->domeLight : GetCameraLightType(_pRenderIndex);

    // Build shadow params if needed
    std::shared_ptr<HdxShadowParams> shadowParams;
    if (light.HasShadow())
    {
        shadowParams             = std::make_shared<HdxShadowParams>();
        shadowParams->enabled    = true;
        shadowParams->resolution = light.GetShadowResolution();
        shadowParams->blur       = (double)light.GetShadowBlur();
        auto shadowMatrixComp =
            std::make_shared<ShadowMatrixComputation>(GfRange3f(worldExtent), light);
        shadowParams->shadowMatrix          = shadowMatrixComp;
        _shadowMatrixComputations[pathName] = shadowMatrixComp;
    }
    else
    {
        _shadowMatrixComputations.erase(pathName);
    }

    auto lightPtr = std::make_shared<GlfSimpleLight>(light);
    std::shared_ptr<HdxShadowParams const> shadowParamsConst = shadowParams;

    // Update the existing prim in place when possible.
    auto it = _lightData.find(pathName);
    if (it != _lightData.end())
    {
        HdSceneIndexPrim prim = _retainedSceneIndex->GetPrim(pathName);
        LightPrimDataSourceHandle ds = LightPrimDataSource::Cast(prim.dataSource);
        if (ds && prim.primType == primType)
        {
            // Check if the light has changed.
            const bool lightChanged = (it->second != light);
            // Check if the xform has changed.
            const bool xformChanged = (ds->worldExtent != worldExtent ||
                ds->cameraLightTransformOverride != cameraLightTransformOverride);

            // Update the light data.
            ds->light                        = lightPtr;
            ds->shadowParams                 = shadowParamsConst;
            ds->worldExtent                  = worldExtent;
            ds->cameraLightTransformOverride = cameraLightTransformOverride;

            HdDataSourceLocatorSet dirtyLocators;
            if (lightChanged)
            {
                dirtyLocators.insert(HdLightSchema::GetDefaultLocator());
                if (_isHighQualityRenderer)
                {
                    dirtyLocators.insert(HdDataSourceLocator(_tokens->materialNetworkMap));
                }
            }
            if (lightChanged || xformChanged)
            {
                dirtyLocators.insert(HdXformSchema::GetDefaultLocator());
            }

            // Dirty the prim only if needed.
            if (!dirtyLocators.IsEmpty())
            {
                _retainedSceneIndex->DirtyPrims({ { pathName, dirtyLocators } });
            }

            _lightData[pathName] = light;
            return;
        }

        // Prim type changed (e.g. dome <-> distant): must remove and re-add.
        RemoveLightSprim(pathIdx);
    }

    HdContainerDataSourceHandle ds = LightPrimDataSource::New(lightPtr, shadowParamsConst, pathName,
        isDomeLight, _isHighQualityRenderer, worldExtent, primType, cameraLightTransformOverride);

    _retainedSceneIndex->AddPrims({ { pathName, primType, ds } });
    _lightData[pathName] = light;
}

bool LightingManagerSIImpl::SupportBuiltInLightTypes(const HdRenderIndex* index) const
{
    // Verify that the renderDelegate supports the light types for the built-in
    // dome and camera lights.
    bool dome   = index->IsSprimTypeSupported(HdPrimTypeTokens->domeLight);
    bool camera = (index->IsSprimTypeSupported(HdPrimTypeTokens->simpleLight) ||
        index->IsSprimTypeSupported(HdPrimTypeTokens->distantLight));
    return dome && camera;
}

void LightingManagerSIImpl::SetBuiltInLightingState(
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

            // Update shadow computation if applicable
            auto it = _shadowMatrixComputations.find(_lightIds[i]);
            if (it != _shadowMatrixComputations.end())
            {
                std::shared_ptr<ShadowMatrixComputation> pShadowMatrixComputation =
                    std::dynamic_pointer_cast<ShadowMatrixComputation>(it->second);
                if (pShadowMatrixComputation != nullptr)
                {
                    pShadowMatrixComputation->update(GfRange3f(worldExtent), activeLight);
                }
            }
        }

        // Update the camera light transform if needed.  cameraTransform is
        // the camera's world-space transform (i.e., view matrix inverse).
        if (_isHighQualityRenderer && !activeLight.IsDomeLight())
        {
            if (cameraTransform != GfMatrix4d(1.0))
            {
                GfMatrix4d trans = cameraTransform * activeLight.GetTransform();
                ReplaceLightSprim(i, activeLight, _lightIds[i], worldExtent, trans);
            }
        }
    }
}

const GlfSimpleLightingContextRefPtr& LightingManagerSIImpl::GetLightingContext() const
{
    return _lightingState;
}

void LightingManagerSIImpl::SetExcludedLights(SdfPathVector const& excludedLights)
{
    _excludedLights = excludedLights;
}

SdfPathVector const& LightingManagerSIImpl::GetExcludedLights() const
{
    return _excludedLights;
}

void LightingManagerSIImpl::SetEnableShadows(bool enable)
{
    _enableShadows = enable;
}

bool LightingManagerSIImpl::GetShadowsEnabled() const
{
    return _enableShadows;
}

void LightingManagerSIImpl::ProcessLightingState(
    GfMatrix4d const& cameraTransform, GfRange3d const& worldExtent)
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

    SetBuiltInLightingState(cameraTransform, worldExtent);
}

} // namespace HVT_NS
