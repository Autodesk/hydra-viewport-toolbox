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

#include "taskStorageFactory.h"

#include "framePassCamera.h"
#include "lightingPrimStorage.h"
#include "renderBufferDescriptorStorage.h"

#include <hvt/engine/taskDataContainer.h>
#include <pxr/base/tf/diagnostic.h>

#include "sd/lightingPrimSDStorage.h"
#include "sd/renderBufferDescriptorSDStorage.h"
#include "sd/taskContainerSDImpl.h"
#if HVT_HAS_LEGACY_TASK_SCHEMA
#include "si/lightingPrimSIStorage.h"
#include "si/renderBufferDescriptorSIStorage.h"
#include "si/taskContainerSIImpl.h"
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

///////////////////////////////////////////////////////////////////////////////
// TaskDataContainer
///////////////////////////////////////////////////////////////////////////////

std::shared_ptr<TaskDataContainer> CreateTaskDataContainer(
    HdRenderIndex* renderIndex, SdfPath const& uid, bool useLegacySceneDelegate)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (!useLegacySceneDelegate)
    {
        return MakeTaskContainerSI(renderIndex, uid);
    }
#endif
    return MakeTaskContainerSD(renderIndex, uid);
}

///////////////////////////////////////////////////////////////////////////////
// FramePassCamera
///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<FramePassCamera> CreateFramePassCamera(SdfPath const& uid,
    HdRenderIndex* renderIndex, std::shared_ptr<TaskDataContainer> const& container,
    bool useLegacySceneDelegate)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (!useLegacySceneDelegate)
    {
        return MakeFramePassCameraSI(uid, container);
    }
#endif
    return MakeFramePassCameraSD(renderIndex, uid);
}

///////////////////////////////////////////////////////////////////////////////
// LightingPrimStorage
///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<LightingPrimStorage> CreateLightingPrimStorage(
    std::shared_ptr<TaskDataContainer> const& container, HdRenderIndex* pRenderIndex,
    bool isHighQualityRenderer, bool useLegacySceneDelegate)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (!useLegacySceneDelegate)
    {
        if (auto* si = dynamic_cast<TaskContainerSIImpl*>(container.get()))
        {
            return std::make_unique<LightingPrimSIStorage>(
                pRenderIndex, si->GetRetainedSceneIndex(), isHighQualityRenderer);
        }
    }
#endif
    auto* sd = dynamic_cast<TaskContainerSDImpl*>(container.get());
    TF_VERIFY(sd, "TaskDataContainer is neither SI nor SD");
    if (sd)
    {
        return std::make_unique<LightingPrimSDStorage>(
            pRenderIndex, sd->GetSyncDelegate(), isHighQualityRenderer);
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// RenderBufferDescriptorStorage
///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<RenderBufferDescriptorStorage> CreateRenderBufferDescriptorStorage(
    std::shared_ptr<TaskDataContainer> const& container, HdRenderIndex* pRenderIndex,
    bool useLegacySceneDelegate)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (!useLegacySceneDelegate)
    {
        if (auto* si = dynamic_cast<TaskContainerSIImpl*>(container.get()))
        {
            return std::make_unique<RenderBufferDescriptorSIStorage>(
                si->GetRetainedSceneIndex());
        }
    }
#endif
    auto* sd = dynamic_cast<TaskContainerSDImpl*>(container.get());
    TF_VERIFY(sd, "TaskDataContainer is neither SI nor SD");
    if (sd)
    {
        return std::make_unique<RenderBufferDescriptorSDStorage>(
            pRenderIndex, sd->GetSyncDelegate());
    }
    return nullptr;
}

} // namespace HVT_NS
