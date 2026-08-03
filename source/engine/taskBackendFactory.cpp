// Copyright 2026 Autodesk, Inc.
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

#include "taskBackendFactory.h"

#include "framePassCamera.h"
#include "lightingPrimBackend.h"
#include "renderBufferPrimBackend.h"

#include <hvt/engine/taskBackend.h>
#include <pxr/base/tf/diagnostic.h>

#include "sd/framePassSDCamera.h"
#include "sd/lightingPrimSDBackend.h"
#include "sd/renderBufferPrimSDBackend.h"
#include "sd/taskSDBackend.h"
#if HVT_SI_TASK_BACKEND_SUPPORTED
#include "si/framePassSICamera.h"
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

TaskBackendSharedPtr CreateTaskBackend(HdRenderIndex* renderIndex, SdfPath const& uid,
    [[maybe_unused]] bool useLegacySceneDelegate)
{
#if HVT_SI_TASK_BACKEND_SUPPORTED
    if (!useLegacySceneDelegate)
    {
        return std::make_shared<TaskSIBackend>(renderIndex, uid);
    }
#endif
    return std::make_shared<TaskSDBackend>(renderIndex, uid);
}

///////////////////////////////////////////////////////////////////////////////
// FramePassCamera
///////////////////////////////////////////////////////////////////////////////

FramePassCameraPtr CreateFramePassCamera(SdfPath const& uid,
    HdRenderIndex* renderIndex, TaskBackendSharedPtr const& taskBackend,
    bool useLegacySceneDelegate)
{
#if HVT_SI_TASK_BACKEND_SUPPORTED
    if (!useLegacySceneDelegate)
    {
        if (auto* si = dynamic_cast<TaskSIBackend*>(taskBackend.get()))
        {
            return std::make_unique<FramePassSICamera>(uid, si->GetRetainedSceneIndex());
        }
    }
#endif

    TF_VERIFY(useLegacySceneDelegate,
        "useLegacySceneDelegate must be true when falling back to the SD backend.");
    TF_VERIFY(dynamic_cast<TaskSDBackend*>(taskBackend.get()),
        "Invalid TaskBackend type: expected TaskSDBackend.");

    return std::make_unique<FramePassSDCamera>(renderIndex, uid);
}

///////////////////////////////////////////////////////////////////////////////
// LightingPrimBackend
///////////////////////////////////////////////////////////////////////////////

LightingPrimBackendPtr CreateLightingPrimBackend(
    TaskBackendSharedPtr const& taskBackend, HdRenderIndex* pRenderIndex,
    [[maybe_unused]] bool isHighQualityRenderer, bool useLegacySceneDelegate)
{
#if HVT_SI_TASK_BACKEND_SUPPORTED
    if (!useLegacySceneDelegate)
    {
        if (auto* si = dynamic_cast<TaskSIBackend*>(taskBackend.get()))
        {
            return std::make_unique<LightingPrimSIBackend>(
                pRenderIndex, si->GetRetainedSceneIndex(), isHighQualityRenderer);
        }
    }
#endif
    TF_VERIFY(useLegacySceneDelegate,
        "useLegacySceneDelegate must be true when falling back to the SD backend.");
    auto* sd = dynamic_cast<TaskSDBackend*>(taskBackend.get());
    if (!sd)
    {
        TF_FATAL_ERROR("Invalid TaskBackend type: neither SI nor SD");
    }
    return std::make_unique<LightingPrimSDBackend>(
        pRenderIndex, sd->GetSyncDelegate());
}

///////////////////////////////////////////////////////////////////////////////
// RenderBufferPrimBackend
///////////////////////////////////////////////////////////////////////////////

RenderBufferPrimBackendPtr CreateRenderBufferPrimBackend(
    TaskBackendSharedPtr const& taskBackend, HdRenderIndex* pRenderIndex,
    bool useLegacySceneDelegate)
{
#if HVT_SI_TASK_BACKEND_SUPPORTED
    if (!useLegacySceneDelegate)
    {
        if (auto* si = dynamic_cast<TaskSIBackend*>(taskBackend.get()))
        {
            return std::make_unique<RenderBufferPrimSIBackend>(
                si->GetRetainedSceneIndex());
        }
    }
#endif
    TF_VERIFY(useLegacySceneDelegate,
        "useLegacySceneDelegate must be true when falling back to the SD backend.");
    auto* sd = dynamic_cast<TaskSDBackend*>(taskBackend.get());
    if (!sd)
    {
        TF_FATAL_ERROR("Invalid TaskBackend type: neither SI nor SD");
    }
    return std::make_unique<RenderBufferPrimSDBackend>(
        pRenderIndex, sd->GetSyncDelegate());
}

} // namespace HVT_NS
