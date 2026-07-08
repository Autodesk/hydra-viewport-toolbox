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

#include <pxr/base/gf/vec4f.h>
#include <pxr/usd/sdf/path.h>

#include <vector>

namespace HVT_NS
{

struct ViewParams;

/// Abstract camera interface for FramePass.
///
/// Hides the SI/SD distinction for camera management: the scene-index (SI) implementation stores a
/// camera prim in the retained scene index, while the scene-delegate (SD) implementation wraps
/// HdxFreeCameraSceneDelegate. The concrete implementations live in si/framePassSICamera.{h,cpp}
/// and sd/framePassSDCamera.{h,cpp}.
class FramePassCamera
{
public:
    virtual ~FramePassCamera() = default;

    /// Returns the camera prim path in the render index.
    virtual PXR_NS::SdfPath const& GetCameraId() const = 0;

    /// Updates the free camera state from the view parameters.  Implementations
    /// derive the view-space clip planes from the section planes themselves and
    /// perform their own dirty-checking, skipping the update when nothing changed.
    virtual void Update(ViewParams const& viewInfo) = 0;

protected:
    /// Converts the world-space section planes carried by the view parameters into the view-space
    /// clip-plane equations expected by HdCameraSchema::clippingPlanes.  Shared by the SI and SD
    /// implementations.
    static std::vector<PXR_NS::GfVec4f> ComputeViewSpaceClipPlanes(ViewParams const& viewInfo);
};

} // namespace HVT_NS
