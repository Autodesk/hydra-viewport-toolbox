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

#include "lightingManagerSDImpl.h"
#include "lightingManagerSIImpl.h"
#include "taskContainerSDImpl.h"
#if HVT_HAS_LEGACY_TASK_SCHEMA
#include "taskContainerSIImpl.h"
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

///////////////////////////////////////////////////////////////////////////////
// LightingManager public API
///////////////////////////////////////////////////////////////////////////////

LightingManager::LightingManager(SdfPath const& lightRootPath, HdRenderIndex* pRenderIndex,
    std::shared_ptr<TaskDataContainer> const& container, bool isHighQualityRenderer)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (auto* si = dynamic_cast<TaskContainerSIImpl*>(container.get()))
    {
        _impl = std::make_unique<LightingManagerSIImpl>(
            lightRootPath, pRenderIndex, si->GetRetainedSceneIndex(), isHighQualityRenderer);
        return;
    }
#endif
    auto* sd = dynamic_cast<TaskContainerSDImpl*>(container.get());
    TF_VERIFY(sd, "TaskDataContainer is neither SI nor SD");
    if (sd)
    {
        _impl = std::make_unique<LightingManagerSDImpl>(
            lightRootPath, pRenderIndex, sd->GetSyncDelegate(), isHighQualityRenderer);
    }
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
