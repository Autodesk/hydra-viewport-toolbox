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
#include "lightingPrimBackend.h"

#include <hvt/engine/taskBackendFactory.h>

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

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

///////////////////////////////////////////////////////////////////////////////
// LightingManager::Impl
///////////////////////////////////////////////////////////////////////////////

class LightingManager::Impl
{
public:
    Impl(SdfPath const& lightRootPath, HdRenderIndex* pRenderIndex,
        std::shared_ptr<TaskBackend> const& taskBackend, bool isHighQualityRenderer,
        bool useLegacySceneDelegate) :
        _lightRootPath(lightRootPath),
        _pRenderIndex(pRenderIndex),
        _isHighQualityRenderer(isHighQualityRenderer)
    {
        _lightingState = GlfSimpleLightingContext::New();

        _primBackend = CreateLightingPrimBackend(
            taskBackend, pRenderIndex, isHighQualityRenderer, useLegacySceneDelegate);
    }

    ~Impl()
    {
        // Storage destructor handles light cleanup.
    }

    void SetEnableShadows(bool enable) { _enableShadows = enable; }
    void SetExcludedLights(SdfPathVector const& excludedLights)
    {
        _excludedLights = excludedLights;
    }

    GlfSimpleLightingContextRefPtr const& GetLightingContext() const { return _lightingState; }
    SdfPathVector const& GetExcludedLights() const { return _excludedLights; }
    bool GetShadowsEnabled() const { return _enableShadows; }

    /// Creates/updates the light prims from the current lighting context.
    void ProcessLightingState(
        GfMatrix4d const& cameraTransform, GfRange3d const& worldExtent)
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

private:
    void SetBuiltInLightingState(
        GfMatrix4d const& cameraTransform, GfRange3d const& worldExtent);

    bool SupportBuiltInLightTypes() const
    {
        bool dome   = _pRenderIndex->IsSprimTypeSupported(HdPrimTypeTokens->domeLight);
        bool camera = (_pRenderIndex->IsSprimTypeSupported(HdPrimTypeTokens->simpleLight) ||
            _pRenderIndex->IsSprimTypeSupported(HdPrimTypeTokens->distantLight));
        return dome && camera;
    }

    SdfPathVector _excludedLights;
    bool _enableShadows { true };

    const SdfPath _lightRootPath;
    HdRenderIndex* _pRenderIndex { nullptr };
    bool _isHighQualityRenderer { false };
    GlfSimpleLightingContextRefPtr _lightingState;
    SdfPathVector _lightIds;

    std::unique_ptr<LightingPrimBackend> _primBackend;
};

void LightingManager::Impl::SetBuiltInLightingState(
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
            if (_primBackend->GetLightAtId(i, _lightIds) != activeLights[i])
            {
                _primBackend->ReplaceLightSprim(
                    i, activeLights[i], lightPath, worldExtent, _lightIds, _isHighQualityRenderer);
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
            if (_primBackend->GetLightAtId(i, _lightIds) != activeLights[i])
            {
                _primBackend->ReplaceLightSprim(
                    i, activeLights[i], lightPath, worldExtent, _lightIds, _isHighQualityRenderer);
            }
        }
        _primBackend->RemoveLightSprim(_lightIds.size() - 1, _lightIds);
        _lightIds.pop_back();
    }

    // If there has been no change in the number of lights we still may need to
    // update the light parameters eg. if the free camera has moved.
    for (size_t i = 0; i < activeLights.size(); ++i)
    {
        GlfSimpleLight const& activeLight = activeLights[i];
        if (_primBackend->GetLightAtId(i, _lightIds) != activeLight)
        {
            _primBackend->ReplaceLightSprim(
                i, activeLight, _lightIds[i], worldExtent, _lightIds, _isHighQualityRenderer);
            _primBackend->SyncLightStateAfterReplace(i, activeLight, _lightIds);
            _primBackend->UpdateShadowMatrixComputation(i, activeLight, worldExtent, _lightIds);
        }

        if (_isHighQualityRenderer && !activeLight.IsDomeLight())
        {
            _primBackend->UpdateCameraLightTransform(
                i, activeLight, cameraTransform, worldExtent, _lightIds);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// LightingManager public API
///////////////////////////////////////////////////////////////////////////////

LightingManager::LightingManager(SdfPath const& lightRootPath, HdRenderIndex* pRenderIndex,
    std::shared_ptr<TaskBackend> const& taskBackend, bool isHighQualityRenderer,
    bool useLegacySceneDelegate)
{
    _impl = std::make_unique<Impl>(
        lightRootPath, pRenderIndex, taskBackend, isHighQualityRenderer, useLegacySceneDelegate);
}

LightingManager::~LightingManager() {}

GlfSimpleLightingContextRefPtr const& LightingManager::GetLightingContext() const
{
    return _impl->GetLightingContext();
}

void LightingManager::SetExcludedLights(SdfPathVector const& excludedLights)
{
    _impl->SetExcludedLights(excludedLights);
}

SdfPathVector const& LightingManager::GetExcludedLights() const
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
    SdfPath const& /*cameraPath*/, GfMatrix4d const& cameraTransform,
    GfRange3d const& worldExtent)
{
    // cameraPath is reserved for future use (e.g. attaching shadow tasks to a
    // specific camera prim path); the current implementation only needs the
    // camera's world transform.
    const GlfSimpleLightingContextPtr lightingState = _impl->GetLightingContext();

    if (lights.size() > 0)
    {
        lightingState->SetUseLighting(true);
        lightingState->SetLights(lights);
        lightingState->SetSceneAmbient(ambient);
        lightingState->SetMaterial(material);
    }
    else
    {
        lightingState->SetUseLighting(false);
        lightingState->SetLights({});
    }

    _impl->ProcessLightingState(cameraTransform, worldExtent);
}

} // namespace HVT_NS
