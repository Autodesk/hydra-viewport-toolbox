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

#include <hvt/engine/framePass.h>

#include <memory>

namespace HVT_NS
{

/// \name Select helper methods.
/// @{

/// Returns the first picked prim from a specific frame pass.
/// \param pass The selected frame pass.
/// \param pickingMatrix The picking matrix.
/// \param framing The framing (i.e., data & display windows).
/// \param viewMatrix The view matrix.
/// \return Returns the first selected prim.
HVT_API extern PXR_NS::SdfPath GetPickedPrim(FramePass* pass,
    PXR_NS::GfMatrix4d const& pickingMatrix, PXR_NS::CameraUtilFraming const& framing,
    PXR_NS::GfMatrix4d const& viewMatrix);

/// @}

/// Highlights selected prims from a specific frame pass.
/// \param framePass The framePass containing the primitives.
/// \param selectionPaths The primitives to highlight.
/// \param locatorPaths The locators to highlight.
HVT_API extern void HighlightSelection(FramePass* framePass,
    PXR_NS::SdfPathSet const& selectionPaths,
    PXR_NS::SdfPathSet const& locatorPaths = PXR_NS::SdfPathSet());

/// Build a "camera" prim data source from a view+projection matrix pair plus
/// a linear exposure scale.
/// \param gfCamera A pre-built GfCamera so callers that also need to compare against
/// the existing scene-index prim (see CameraPrimMatches) can share the
/// SetFromViewAndProjectionMatrix conversion across compare + build.
/// \param worldXform The world transform of the camera (inverse view matrix).
/// \param clipPlanes The clip planes to set on the camera.
/// \param linearExposureScale Linear exposure scale applied to scene radiance.  1.0 disables
/// exposure (no visual change).  This flows through the camera prim's
/// HdCameraSchema::linearExposureScale.
PXR_NS::HdContainerDataSourceHandle BuildCameraPrimDataSource(PXR_NS::GfCamera const& gfCamera,
    PXR_NS::GfMatrix4d const& worldXform, std::vector<PXR_NS::GfVec4f> const& clipPlanes,
    float linearExposureScale = 1.0f);

/// Compares the camera prim already in the retained scene index against the
/// new state we would otherwise stamp via BuildCameraPrimDataSource.
/// Comparison is field-wise exact against HdCameraSchema / HdXformSchema:
/// any value that BuildCameraPrimDataSource writes is checked here. If a
/// field is added to the builder, it must be added here too -- treating
/// "schema field missing" as a mismatch is what guarantees that omitting
/// a check can only lose the early-out, never silently keep stale state.
/// \param sceneIndex The retained scene index to check for the existing camera prim.
/// \param cameraId The path of the camera prim to check.
/// \param newCamera The GfCamera representing the new camera state to compare against the existing
/// camera prim.
/// \param newWorldXform The world transform of the camera (inverse view matrix) to compare against
/// the existing camera prim.
/// \param newClipPlanes The clip planes to compare against the existing camera prim.
/// \param newLinearExposureScale The linear exposure scale to compare against the existing camera
/// prim.
bool CameraPrimMatches(PXR_NS::HdRetainedSceneIndexRefPtr const& sceneIndex,
    PXR_NS::SdfPath const& cameraId, PXR_NS::GfCamera const& newCamera,
    PXR_NS::GfMatrix4d const& newWorldXform, std::vector<PXR_NS::GfVec4f> const& newClipPlanes,
    float newLinearExposureScale);

} // namespace HVT_NS
