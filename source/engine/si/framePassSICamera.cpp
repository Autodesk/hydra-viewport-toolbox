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

#include "framePassSICamera.h"

#if HVT_HAS_LEGACY_TASK_SCHEMA

#include <hvt/engine/framePass.h>

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
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>
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

namespace
{

/// Builds a "camera" prim data source from a view+projection matrix pair (captured in a pre-built
/// GfCamera), its world transform and a linear exposure scale.
/// \param gfCamera A pre-built GfCamera so callers that also need to compare against the existing
/// scene-index prim (see CameraPrimMatches) can share the SetFromViewAndProjectionMatrix
/// conversion across compare + build.
/// \param worldXform The world transform of the camera (inverse view matrix).
/// \param clipPlanes The clip planes to set on the camera.
/// \param linearExposureScale Linear exposure scale applied to scene radiance.  1.0 disables
/// exposure (no visual change).  This flows through the camera prim's
/// HdCameraSchema::linearExposureScale.
HdContainerDataSourceHandle BuildCameraPrimDataSource(GfCamera const& gfCamera,
    GfMatrix4d const& worldXform, std::vector<GfVec4f> const& clipPlanes,
    [[maybe_unused]] float linearExposureScale)
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
#if PXR_VERSION >= 2505
            .SetLinearExposureScale(
                HdRetainedTypedSampledDataSource<float>::New(linearExposureScale))
#endif
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

/// Compares the camera prim already in the retained scene index against the new state we would
/// otherwise stamp via BuildCameraPrimDataSource.
/// Comparison is field-wise exact against HdCameraSchema / HdXformSchema: any value that
/// BuildCameraPrimDataSource writes is checked here. If a field is added to the builder, it must be
/// added here too -- treating "schema field missing" as a mismatch is what guarantees that omitting
/// a check can only lose the early-out, never silently keep stale state.
bool CameraPrimMatches(HdRetainedSceneIndexRefPtr const& sceneIndex, SdfPath const& cameraId,
    GfCamera const& newCamera, GfMatrix4d const& newWorldXform,
    std::vector<GfVec4f> const& newClipPlanes, [[maybe_unused]] float newLinearExposureScale)
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
            static_cast<float>(newCamera.GetFocalLength() * GfCamera::FOCAL_LENGTH_UNIT))
#if PXR_VERSION >= 2505
        || !matchesFloat(cameraSchema.GetLinearExposureScale(), newLinearExposureScale)
#endif
    )
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

} // anonymous namespace

FramePassSICamera::FramePassSICamera(
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

SdfPath const& FramePassSICamera::GetCameraId() const
{
    return _cameraId;
}

void FramePassSICamera::Update(ViewParams const& viewInfo)
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
