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

#include "lightingManagerSDImpl.h"

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
#include <pxr/usd/sdf/path.h>

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

template <typename T>
T GetValue(SyncDelegatePtr const& syncDelegate, SdfPath const& id, TfToken const& key)
{
    VtValue vParams = syncDelegate->GetValue(id, key);
    return vParams.Get<T>();
}

} // anonymous namespace

void LightingManagerSDImpl::SetParameters(SdfPath const& pathName, GlfSimpleLight const& light,
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

void LightingManagerSDImpl::SetMaterialNetwork(
    SdfPath const& pathName, GlfSimpleLight const& light, SyncDelegatePtr& lightDelegate)
{
    HdMaterialNetworkMap networkMap;
    GetMaterialNetwork(pathName, light, networkMap);

    lightDelegate->SetValue(pathName, _tokens->materialNetworkMap, VtValue(networkMap));
}

GlfSimpleLight LightingManagerSDImpl::GetLightAtId(size_t pathIdx) const
{
    GlfSimpleLight light = GlfSimpleLight();
    if (pathIdx < _lightIds.size())
    {
        light = GetValue<GlfSimpleLight>(_lightDelegate, _lightIds[pathIdx], HdLightTokens->params);
    }
    return light;
}

void LightingManagerSDImpl::RemoveLightSprim(size_t pathIdx)
{
    if (pathIdx < _lightIds.size())
    {
        _pRenderIndex->RemoveSprim(GetCameraLightType(), _lightIds[pathIdx]);
        _pRenderIndex->RemoveSprim(HdPrimTypeTokens->domeLight, _lightIds[pathIdx]);
    }
}

void LightingManagerSDImpl::ReplaceLightSprim(size_t pathIdx, GlfSimpleLight const& light,
    SdfPath const& pathName, GfRange3d const& worldExtent)
{
    RemoveLightSprim(pathIdx);

    if (light.IsDomeLight())
    {
        _pRenderIndex->InsertSprim(HdPrimTypeTokens->domeLight, _lightDelegate.get(), pathName);
    }
    else
    {
        _pRenderIndex->InsertSprim(
            GetCameraLightType(), _lightDelegate.get(), pathName);
    }

    // Set the parameters for the light and mark as dirty
    SetParameters(pathName, light, _lightDelegate, _isHighQualityRenderer, worldExtent);

    // Create a HdMaterialNetworkMap for the light if we are not using Storm
    if (_isHighQualityRenderer)
    {
        SetMaterialNetwork(pathName, light, _lightDelegate);
    }

    _pRenderIndex->GetChangeTracker().MarkSprimDirty(pathName, HdLight::AllDirty);
}


void LightingManagerSDImpl::SyncLightStateAfterReplace(
    size_t pathIdx, GlfSimpleLight const& light)
{
    _lightDelegate->SetValue(_lightIds[pathIdx], HdLightTokens->params, VtValue(light));
    _lightDelegate->SetValue(
        _lightIds[pathIdx], HdTokens->transform, VtValue(light.GetTransform()));

    if (light.IsDomeLight())
    {
        _lightDelegate->SetValue(
            _lightIds[pathIdx], HdLightTokens->textureFile, GetDomeLightTextureValue(light));
    }
    _pRenderIndex->GetChangeTracker().MarkSprimDirty(
        _lightIds[pathIdx], HdLight::DirtyParams | HdLight::DirtyTransform);
}

void LightingManagerSDImpl::UpdateShadowMatrixComputation(
    size_t pathIdx, GlfSimpleLight const& light, GfRange3d const& worldExtent)
{
    if (light.HasShadow() || GetLightAtId(pathIdx).HasShadow())
    {
        auto shadowParams = GetValue<HdxShadowParams>(
            _lightDelegate, _lightIds[pathIdx], HdLightTokens->shadowParams);
        std::shared_ptr<ShadowMatrixComputation> pShadowMatrixComputation =
            std::dynamic_pointer_cast<ShadowMatrixComputation>(shadowParams.shadowMatrix);
        if (pShadowMatrixComputation != nullptr)
            pShadowMatrixComputation->update(GfRange3f(worldExtent), light);
        _pRenderIndex->GetChangeTracker().MarkSprimDirty(
            _lightIds[pathIdx], HdLight::DirtyShadowParams);
    }
}

void LightingManagerSDImpl::UpdateCameraLightTransform(size_t pathIdx,
    GlfSimpleLight const& light, GfMatrix4d const& cameraTransform,
    GfRange3d const& /*worldExtent*/)
{
    GfMatrix4d const& viewInvMatrix = cameraTransform;
    VtValue trans     = VtValue(viewInvMatrix * light.GetTransform());
    VtValue prevTrans = _lightDelegate->GetValue(_lightIds[pathIdx], HdTokens->transform);
    if (viewInvMatrix != GfMatrix4d(1.0) && trans != prevTrans)
    {
        _lightDelegate->SetValue(_lightIds[pathIdx], HdTokens->transform, trans);
        _pRenderIndex->GetChangeTracker().MarkSprimDirty(
            _lightIds[pathIdx], HdLight::DirtyTransform);
    }
}

} // namespace HVT_NS
