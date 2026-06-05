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

#include <hvt/engine/framePassUtils.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/base/gf/camera.h>
#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/xformSchema.h>

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

SdfPath GetPickedPrim(FramePass* pass, GfMatrix4d const& pickingMatrix,
    CameraUtilFraming const& framing, GfMatrix4d const& viewMatrix)
{
    if (!pass || !pass->IsInitialized())
    {
        TF_CODING_ERROR("The frame pass is null or not initialized.");
        return {};
    }

    SdfPath hitPrimPath;

    pass->params().viewInfo.framing          = framing;
    pass->params().viewInfo.viewMatrix       = viewMatrix;
    pass->params().viewInfo.projectionMatrix = pickingMatrix;

    pass->UpdateScene();

    const HdSelectionSharedPtr hits = pass->Pick(HdxPickTokens->pickPrimsAndInstances);
    const SdfPathVector allPrims    = hits->GetAllSelectedPrimPaths();
    if (!allPrims.empty())
    {
        // TODO: Need to process all paths?
        hitPrimPath = SdfPath(allPrims[0]);
    }

    return hitPrimPath;
}

void HighlightSelection(
    FramePass* pass, SdfPathSet const& selectionPaths, SdfPathSet const& locatorPaths)
{
    if (!pass)
    {
        TF_CODING_ERROR("The frame pass is null.");
        return;
    }

    const HdSelectionSharedPtr selection = ViewportEngine::PrepareSelection(selectionPaths);
    if (!locatorPaths.empty())
        ViewportEngine::PrepareSelection(
            locatorPaths, HdSelection::HighlightMode::HighlightModeLocate, selection);

    pass->SetSelection(selection);
}

HdContainerDataSourceHandle BuildCameraPrimDataSource(GfCamera const& gfCamera,
    GfMatrix4d const& worldXform, std::vector<GfVec4f> const& clipPlanes, float linearExposureScale)
{
    const TfToken projectionToken = (gfCamera.GetProjection() == GfCamera::Perspective)
        ? HdCameraSchemaTokens->perspective
        : HdCameraSchemaTokens->orthographic;

    const GfRange1f cr = gfCamera.GetClippingRange();
    const GfVec2f clippingRangeVec(cr.GetMin(), cr.GetMax());

    VtArray<GfVec4d> clippingPlanesArray;
    clippingPlanesArray.reserve(clipPlanes.size());
    for (GfVec4f const& p : clipPlanes)
    {
        clippingPlanesArray.push_back(GfVec4d(p[0], p[1], p[2], p[3]));
    }

    HdContainerDataSourceHandle cameraDS =
        HdCameraSchema::Builder()
            .SetProjection(HdCameraSchema::BuildProjectionDataSource(projectionToken))
            .SetHorizontalAperture(HdRetainedTypedSampledDataSource<float>::New(
                static_cast<float>(gfCamera.GetHorizontalAperture() * GfCamera::APERTURE_UNIT)))
            .SetVerticalAperture(HdRetainedTypedSampledDataSource<float>::New(
                static_cast<float>(gfCamera.GetVerticalAperture() * GfCamera::APERTURE_UNIT)))
            .SetHorizontalApertureOffset(HdRetainedTypedSampledDataSource<float>::New(
                static_cast<float>(gfCamera.GetHorizontalApertureOffset() * GfCamera::APERTURE_UNIT)))
            .SetVerticalApertureOffset(HdRetainedTypedSampledDataSource<float>::New(
                static_cast<float>(gfCamera.GetVerticalApertureOffset() * GfCamera::APERTURE_UNIT)))
            .SetFocalLength(HdRetainedTypedSampledDataSource<float>::New(
                static_cast<float>(gfCamera.GetFocalLength() * GfCamera::FOCAL_LENGTH_UNIT)))
            .SetClippingRange(HdRetainedTypedSampledDataSource<GfVec2f>::New(clippingRangeVec))
            .SetClippingPlanes(
                HdRetainedTypedSampledDataSource<VtArray<GfVec4d>>::New(clippingPlanesArray))
            .SetLinearExposureScale(
                HdRetainedTypedSampledDataSource<float>::New(linearExposureScale))
            .Build();

    // The "world" transform of a camera prim is its inverse view matrix.
    HdContainerDataSourceHandle xformDS =
        HdXformSchema::Builder()
            .SetMatrix(HdRetainedTypedSampledDataSource<GfMatrix4d>::New(worldXform))
            .SetResetXformStack(HdRetainedTypedSampledDataSource<bool>::New(true))
            .Build();

    return HdRetainedContainerDataSource::New(
        HdCameraSchemaTokens->camera, cameraDS, HdXformSchemaTokens->xform, xformDS);
}

bool CameraPrimMatches(HdRetainedSceneIndexRefPtr const& sceneIndex, SdfPath const& cameraId,
    GfCamera const& newCamera, GfMatrix4d const& newWorldXform,
    std::vector<GfVec4f> const& newClipPlanes, float newLinearExposureScale)
{
    HdSceneIndexPrim const prim = sceneIndex->GetPrim(cameraId);
    if (!prim.dataSource)
    {
        return false;
    }

    HdCameraSchema const cameraSchema = HdCameraSchema::GetFromParent(prim.dataSource);
    if (!cameraSchema)
    {
        return false;
    }

    auto matchesFloat = [](HdFloatDataSourceHandle const& ds, float expected)
    { return ds && ds->GetTypedValue(0.0f) == expected; };

    const TfToken expectedProjection     = (newCamera.GetProjection() == GfCamera::Perspective)
            ? HdCameraSchemaTokens->perspective
            : HdCameraSchemaTokens->orthographic;
    HdTokenDataSourceHandle const projDs = cameraSchema.GetProjection();
    if (!projDs || projDs->GetTypedValue(0.0f) != expectedProjection)
    {
        return false;
    }

    if (!matchesFloat(cameraSchema.GetHorizontalAperture(),
            static_cast<float>(newCamera.GetHorizontalAperture() * GfCamera::APERTURE_UNIT)) ||
        !matchesFloat(cameraSchema.GetVerticalAperture(),
            static_cast<float>(newCamera.GetVerticalAperture() * GfCamera::APERTURE_UNIT)) ||
        !matchesFloat(cameraSchema.GetHorizontalApertureOffset(),
            static_cast<float>(newCamera.GetHorizontalApertureOffset() * GfCamera::APERTURE_UNIT)) ||
        !matchesFloat(cameraSchema.GetVerticalApertureOffset(),
            static_cast<float>(newCamera.GetVerticalApertureOffset() * GfCamera::APERTURE_UNIT)) ||
        !matchesFloat(cameraSchema.GetFocalLength(),
            static_cast<float>(newCamera.GetFocalLength() * GfCamera::FOCAL_LENGTH_UNIT)) ||
        !matchesFloat(cameraSchema.GetLinearExposureScale(), newLinearExposureScale))
    {
        return false;
    }

    const GfRange1f cr = newCamera.GetClippingRange();
    const GfVec2f expectedClippingRange(cr.GetMin(), cr.GetMax());
    HdVec2fDataSourceHandle const crDs = cameraSchema.GetClippingRange();
    if (!crDs || crDs->GetTypedValue(0.0f) != expectedClippingRange)
    {
        return false;
    }

    HdVec4dArrayDataSourceHandle const cpDs = cameraSchema.GetClippingPlanes();
    if (!cpDs)
    {
        return newClipPlanes.empty();
    }
    VtArray<GfVec4d> const existingPlanes = cpDs->GetTypedValue(0.0f);
    if (existingPlanes.size() != newClipPlanes.size())
    {
        return false;
    }
    for (size_t i = 0; i < newClipPlanes.size(); ++i)
    {
        const GfVec4d expectedPlane(
            newClipPlanes[i][0], newClipPlanes[i][1], newClipPlanes[i][2], newClipPlanes[i][3]);
        if (existingPlanes[i] != expectedPlane)
        {
            return false;
        }
    }

    HdXformSchema const xformSchema = HdXformSchema::GetFromParent(prim.dataSource);
    if (!xformSchema)
    {
        return false;
    }
    HdMatrixDataSourceHandle const matDs = xformSchema.GetMatrix();
    if (!matDs || matDs->GetTypedValue(0.0f) != newWorldXform)
    {
        return false;
    }

    return true;
}

} // namespace HVT_NS