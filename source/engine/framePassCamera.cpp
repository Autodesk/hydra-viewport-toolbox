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

#include <hvt/engine/framePass.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/plane.h>
#include <pxr/base/gf/vec4d.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

std::vector<GfVec4f> FramePassCamera::ComputeViewSpaceClipPlanes(ViewParams const& viewInfo)
{
    std::vector<GfVec4f> clipPlanes;
    clipPlanes.reserve(viewInfo.sectionPlanes.size());

    GfMatrix4d const& viewMatrix = viewInfo.viewMatrix;
    for (const auto& worldSpacePlane : viewInfo.sectionPlanes)
    {
        // Transform section plane from world space to view space.
        GfPlane viewSpacePlane = worldSpacePlane;
        viewSpacePlane.Transform(viewMatrix);

        // Get the equation for the camera clip planes.
        GfVec4d planeEquation = viewSpacePlane.GetEquation();
        clipPlanes.push_back(GfVec4f(planeEquation));
    }

    return clipPlanes;
}

} // namespace HVT_NS
