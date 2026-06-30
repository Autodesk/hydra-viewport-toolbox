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

#include "framePassCamera.h"

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
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>

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

///////////////////////////////////////////////////////////////////////////////
// Scene-index (SI) implementation

class FramePassCameraSI : public FramePassCamera
{
public:
    FramePassCameraSI(
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

    SdfPath const& GetCameraId() const override { return _cameraId; }

    void Update(GfMatrix4d const& viewMatrix, GfMatrix4d const& projectionMatrix,
        std::vector<GfVec4f> const& clipPlanes, float linearExposureScale) override
    {
        GfCamera newCamera;
        newCamera.SetFromViewAndProjectionMatrix(viewMatrix, projectionMatrix);
        if (!clipPlanes.empty())
        {
            newCamera.SetClippingPlanes(clipPlanes);
        }

        const GfMatrix4d newWorldXform = viewMatrix.GetInverse();

        if (!CameraPrimMatches(
                _retainedSceneIndex, _cameraId, newCamera, newWorldXform, clipPlanes,
                linearExposureScale))
        {
            _retainedSceneIndex->AddPrims({ { _cameraId, HdPrimTypeTokens->camera,
                BuildCameraPrimDataSource(
                    newCamera, newWorldXform, clipPlanes, linearExposureScale) } });
        }
    }

private:
    SdfPath _cameraId;
    HdRetainedSceneIndexRefPtr _retainedSceneIndex;
};

///////////////////////////////////////////////////////////////////////////////
// Scene-delegate (SD) implementation

class FramePassCameraSD : public FramePassCamera
{
public:
    FramePassCameraSD(HdRenderIndex* renderIndex, SdfPath const& uid) :
        _delegate(std::make_unique<HdxFreeCameraSceneDelegate>(renderIndex, uid))
    {
    }

    SdfPath const& GetCameraId() const override { return _delegate->GetCameraId(); }

    void Update(GfMatrix4d const& viewMatrix, GfMatrix4d const& projectionMatrix,
        std::vector<GfVec4f> const& clipPlanes, float /*linearExposureScale*/) override
    {
        _delegate->SetMatrices(viewMatrix, projectionMatrix);
        _delegate->SetClipPlanes(clipPlanes);
    }

private:
    std::unique_ptr<HdxFreeCameraSceneDelegate> _delegate;
};

///////////////////////////////////////////////////////////////////////////////
// Factory functions

std::unique_ptr<FramePassCamera> MakeFramePassCameraSI(
    SdfPath const& uid, HdRetainedSceneIndexRefPtr const& retainedSceneIndex)
{
    return std::make_unique<FramePassCameraSI>(uid, retainedSceneIndex);
}

std::unique_ptr<FramePassCamera> MakeFramePassCameraSD(
    HdRenderIndex* renderIndex, SdfPath const& uid)
{
    return std::make_unique<FramePassCameraSD>(renderIndex, uid);
}

} // namespace HVT_NS
