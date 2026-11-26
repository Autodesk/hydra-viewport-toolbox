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
#pragma once

#include <hvt/api.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>

namespace HVT_NS
{

/// An interface for post-processing tasks whose screen-space kernel reads pixels beyond the
/// borders of the region it renders into, so the region must be padded with a margin for the
/// effect to be correct at its borders.
class HVT_API ScreenSpaceKernelEffect
{
public:
    /// The view a render will render with i.e., the values a screen-space kernel cannot know
    /// before the first render.
    struct MarginContext
    {
        /// The full image resolution, in pixels.
        PXR_NS::GfVec2i imageResolution { 0, 0 };

        /// The camera projection matrix, as exported i.e., before window conformation.
        PXR_NS::GfMatrix4d projectionMatrix { 1.0 };

        /// The policy conforming the projection matrix to the image aspect ratio, as the render
        /// applies it i.e., the camera window policy, or its override.
        PXR_NS::CameraUtilConformWindowPolicy windowPolicy { PXR_NS::CameraUtilFit };

        /// The camera view matrix.
        PXR_NS::GfMatrix4d viewMatrix { 1.0 };

        /// The world-space bounds of the rendered scene. When not empty, an effect whose reach
        /// depends on the depth (e.g. SSAO) computes it at the nearest point of the bounds
        /// instead of at the near plane, which gives a much smaller margin.
        PXR_NS::GfRange3d worldExtent;
    };

    /// Returns the size of the margin, in pixels, needed on every side of a rendered region for
    /// the effect to render correctly at the region borders, or 0 when the effect is disabled.
    /// \param context The view the region will be rendered with.
    /// \param params The task parameters to compute the margin for, e.g. from
    /// TaskManager::GetTaskValue. When it does not hold the parameter type of the task, the
    /// parameters synced into the task so far are used.
    virtual int ComputeRequiredMargin(
        MarginContext const& context, PXR_NS::VtValue const& params) const = 0;

    virtual ~ScreenSpaceKernelEffect();
};

} // namespace HVT_NS
