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

#include <hvt/engine/taskBackendFactory.h>

#include "framePassCamera.h"
#include "lightingPrimBackend.h"
#include "renderBufferPrimBackend.h"

#include <hvt/engine/taskBackend.h>
#include <pxr/base/tf/diagnostic.h>

#include "sd/framePassCameraSD.h"
#include "sd/lightingPrimSDBackend.h"
#include "sd/renderBufferPrimSDBackend.h"
#include "sd/taskSDBackend.h"
#if HVT_HAS_LEGACY_TASK_SCHEMA
#include "si/framePassCameraSI.h"
#include "si/lightingPrimSIBackend.h"
#include "si/renderBufferPrimSIBackend.h"
#include "si/taskSIBackend.h"
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

///////////////////////////////////////////////////////////////////////////////
// TaskBackend
///////////////////////////////////////////////////////////////////////////////

std::shared_ptr<TaskBackend> CreateTaskBackend(HdRenderIndex* renderIndex, SdfPath const& uid,
    [[maybe_unused]] bool useLegacySceneDelegate)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (!useLegacySceneDelegate)
    {
        return std::make_unique<TaskSIBackend>(renderIndex, uid);
    }
#endif
    return std::make_unique<TaskSDBackend>(renderIndex, uid);
}

///////////////////////////////////////////////////////////////////////////////
// FramePassCamera
///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<FramePassCamera> CreateFramePassCamera(SdfPath const& uid,
    HdRenderIndex* renderIndex, [[maybe_unused]] std::shared_ptr<TaskBackend> const& taskBackend,
    [[maybe_unused]] bool useLegacySceneDelegate)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (!useLegacySceneDelegate)
    {
        if (auto* si = dynamic_cast<TaskSIBackend*>(taskBackend.get()))
        {
            return std::make_unique<FramePassCameraSI>(uid, si->GetRetainedSceneIndex());
        }
    }
#endif
    return std::make_unique<FramePassCameraSD>(renderIndex, uid);
}

///////////////////////////////////////////////////////////////////////////////
// LightingPrimBackend
///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<LightingPrimBackend> CreateLightingPrimBackend(
    std::shared_ptr<TaskBackend> const& taskBackend, HdRenderIndex* pRenderIndex,
    [[maybe_unused]] bool isHighQualityRenderer, [[maybe_unused]] bool useLegacySceneDelegate)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (!useLegacySceneDelegate)
    {
        if (auto* si = dynamic_cast<TaskSIBackend*>(taskBackend.get()))
        {
            return std::make_unique<LightingPrimSIBackend>(
                pRenderIndex, si->GetRetainedSceneIndex(), isHighQualityRenderer);
        }
    }
#endif
    auto* sd = dynamic_cast<TaskSDBackend*>(taskBackend.get());
    TF_VERIFY(sd, "TaskBackend is neither SI nor SD");
    if (sd)
    {
        return std::make_unique<LightingPrimSDBackend>(
            pRenderIndex, sd->GetSyncDelegate());
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// RenderBufferPrimBackend
///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<RenderBufferPrimBackend> CreateRenderBufferPrimBackend(
    std::shared_ptr<TaskBackend> const& taskBackend, HdRenderIndex* pRenderIndex,
    [[maybe_unused]] bool useLegacySceneDelegate)
{
#if HVT_HAS_LEGACY_TASK_SCHEMA
    if (!useLegacySceneDelegate)
    {
        if (auto* si = dynamic_cast<TaskSIBackend*>(taskBackend.get()))
        {
            return std::make_unique<RenderBufferPrimSIBackend>(
                si->GetRetainedSceneIndex());
        }
    }
#endif
    auto* sd = dynamic_cast<TaskSDBackend*>(taskBackend.get());
    TF_VERIFY(sd, "TaskBackend is neither SI nor SD");
    if (sd)
    {
        return std::make_unique<RenderBufferPrimSDBackend>(
            pRenderIndex, sd->GetSyncDelegate());
    }
    return nullptr;
}

} // namespace HVT_NS
