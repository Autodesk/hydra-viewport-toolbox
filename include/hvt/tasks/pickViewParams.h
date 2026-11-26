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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
// clang-format on

#include <pxr/pxr.h>

#include <pxr/base/gf/rect2i.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/usd/sdf/path.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
// clang-format on

#include <optional>

namespace HVT_NS
{

/// The view a pick is made in: the camera and framing the application draws that view with, and
/// the pick region inside it.
///
/// Pick tasks that render their own ID buffers through a camera (FlashPickTask) render the pick
/// region through this camera and framing, so the pick matches what is drawn in the view whatever
/// the last rendered view was. Applications with several views on one frame pass need it: the
/// frame pass parameters hold the last rendered view.
///
/// Pick tasks that render through the pick matrices of PXR_NS::HdxPickTaskContextParams
/// (PXR_NS::HdxPickTask) do not use it.
///
/// The pick matrices and the pick region must describe the same pixels: FlashPickTask only uses
/// the pick region and PXR_NS::HdxPickTask only the pick matrices, so when they disagree the two
/// tasks return different hits, with no warning.
struct HVT_API PickViewParams
{
    /// The camera the view is drawn through. When empty, the camera of the frame pass's last
    /// render is used.
    PXR_NS::SdfPath cameraId;

    /// The framing the view is drawn with. Only the display window is used: the pick region
    /// replaces the data window.
    PXR_NS::CameraUtilFraming framing;

    /// The window policy the view is drawn with, if it overrides the camera's.
    std::optional<PXR_NS::CameraUtilConformWindowPolicy> overrideWindowPolicy;

    /// The pixels to pick, in the pixel coordinates of \c framing, the ones of its display and
    /// data windows (y axis pointing down).
    /// \note The pick region is not clipped to the view: pixels outside it are rendered and
    /// picked too, as PXR_NS::HdxPickTask does with the pick matrices. Clip the region to the
    /// view's data window first to pick only what the view shows.
    PXR_NS::GfRect2i pickRegion;

    /// The task context key holding the PickViewParams of the current pick.
    static PXR_NS::TfToken const& GetToken();

    bool operator==(PickViewParams const& other) const
    {
        return cameraId == other.cameraId && framing == other.framing &&
            overrideWindowPolicy == other.overrideWindowPolicy && pickRegion == other.pickRegion;
    }
    bool operator!=(PickViewParams const& other) const { return !(*this == other); }
};

/// Returns the framing that renders only the pick region of \p view, into a buffer of the pick
/// region size: the display window is moved so that the pick region starts at (0, 0). The
/// projection of the pick region is unchanged, as CameraUtilFraming::ApplyToProjectionMatrix()
/// only depends on the window sizes and the offset between the window centers.
HVT_API PXR_NS::CameraUtilFraming GetPickRegionFraming(PickViewParams const& view);

} // namespace HVT_NS
