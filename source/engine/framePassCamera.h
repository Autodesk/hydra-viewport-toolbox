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
#pragma once

#include <hvt/api.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/usd/sdf/path.h>

#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderIndex;
PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS
{

/// Abstract camera interface for FramePass.
///
/// Hides the SI/SD distinction for camera management: the SI implementation
/// stores a camera prim in the retained scene index, while the SD implementation
/// wraps HdxFreeCameraSceneDelegate.
class FramePassCamera
{
public:
    virtual ~FramePassCamera() = default;

    /// Returns the camera prim path in the render index.
    virtual PXR_NS::SdfPath const& GetCameraId() const = 0;

    /// Updates the free camera state.  Implementations perform their own
    /// dirty-checking and skip the update when nothing changed.
    virtual void Update(PXR_NS::GfMatrix4d const& viewMatrix,
        PXR_NS::GfMatrix4d const& projectionMatrix,
        std::vector<PXR_NS::GfVec4f> const& clipPlanes,
        float linearExposureScale) = 0;
};

/// Creates a scene-index (SI) based camera.
/// Adds an initial camera prim (identity matrices) to the retained scene index.
std::unique_ptr<FramePassCamera> MakeFramePassCameraSI(PXR_NS::SdfPath const& uid,
    PXR_NS::HdRetainedSceneIndexRefPtr const& retainedSceneIndex);

/// Creates a scene-delegate (SD) based camera backed by HdxFreeCameraSceneDelegate.
std::unique_ptr<FramePassCamera> MakeFramePassCameraSD(
    PXR_NS::HdRenderIndex* renderIndex, PXR_NS::SdfPath const& uid);

} // namespace HVT_NS
