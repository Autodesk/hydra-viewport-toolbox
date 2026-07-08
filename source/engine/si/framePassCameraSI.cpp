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

#include "framePassCameraSI.h"

#if HVT_HAS_LEGACY_TASK_SCHEMA

#include <hvt/engine/framePass.h>
#include <hvt/engine/framePassUtils.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/gf/camera.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

FramePassCameraSI::FramePassCameraSI(
    SdfPath const& uid, HdRetainedSceneIndexRefPtr const& retainedSceneIndex) :
    _cameraId(uid.AppendChild(TfToken("camera"))),
    _retainedSceneIndex(retainedSceneIndex)
{
    GfCamera initialCamera;
    initialCamera.SetFromViewAndProjectionMatrix(GfMatrix4d(1.0), GfMatrix4d(1.0));
    _retainedSceneIndex->AddPrims({ { _cameraId, HdPrimTypeTokens->camera,
        BuildCameraPrimDataSource(
            initialCamera, /*worldXform=*/GfMatrix4d(1.0), /*clipPlanes=*/{},
            /*linearExposureScale=*/1.0f) } });
}

SdfPath const& FramePassCameraSI::GetCameraId() const
{
    return _cameraId;
}

void FramePassCameraSI::Update(ViewParams const& viewInfo)
{
    const std::vector<GfVec4f> clipPlanes = ComputeViewSpaceClipPlanes(viewInfo);

    GfCamera newCamera;
    newCamera.SetFromViewAndProjectionMatrix(viewInfo.viewMatrix, viewInfo.projectionMatrix);
    if (!clipPlanes.empty())
    {
        newCamera.SetClippingPlanes(clipPlanes);
    }

    const GfMatrix4d newWorldXform = viewInfo.viewMatrix.GetInverse();

    if (!CameraPrimMatches(_retainedSceneIndex, _cameraId, newCamera, newWorldXform, clipPlanes,
            viewInfo.linearExposureScale))
    {
        _retainedSceneIndex->AddPrims({ { _cameraId, HdPrimTypeTokens->camera,
            BuildCameraPrimDataSource(
                newCamera, newWorldXform, clipPlanes, viewInfo.linearExposureScale) } });
    }
}

} // namespace HVT_NS

#endif // HVT_HAS_LEGACY_TASK_SCHEMA
