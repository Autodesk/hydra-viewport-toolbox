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

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4003)
    #pragma warning(disable : 4244)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/imaging/glf/simpleLight.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace HvtViewer
{

/// A turntable camera orbiting a target point.
///
/// The default view matches the one used by the unit tests (see TestView::updateCameraAndLights):
/// the eye is placed on the -Z side of the model, at twice the model size, looking at the center of
/// the model, with +Y up.
class OrbitCamera
{
public:
    /// Frames the given world extent, resetting the orientation to the default view.
    /// \param bounds The world extent of the model to frame.
    void fit(pxr::GfRange3d const& bounds);

    /// Orbits around the target point.
    /// \param dxPixels The horizontal mouse motion, in pixels.
    /// \param dyPixels The vertical mouse motion, in pixels (positive is downwards).
    void orbit(double dxPixels, double dyPixels);

    /// Moves the target point (and the eye with it) in the plane of the screen.
    /// \param dxPixels The horizontal mouse motion, in pixels.
    /// \param dyPixels The vertical mouse motion, in pixels (positive is downwards).
    /// \param viewportHeight The viewport height in pixels, so that the target point follows the
    /// mouse cursor.
    void pan(double dxPixels, double dyPixels, int viewportHeight);

    /// Moves the eye closer to or further from the target point.
    /// \param steps The number of mouse wheel steps, positive when moving closer.
    void dolly(double steps);

    /// Gets the view matrix.
    pxr::GfMatrix4d viewMatrix() const;

    /// Gets the projection matrix.
    /// \param width The viewport width, in pixels.
    /// \param height The viewport height, in pixels.
    pxr::GfMatrix4d projectionMatrix(int width, int height) const;

    /// Gets a single light located at the eye, i.e., a headlight following the camera.
    pxr::GlfSimpleLightVector headlight() const;

    /// Gets the eye position.
    pxr::GfVec3d eye() const;

private:
    /// The point the camera looks at and orbits around.
    pxr::GfVec3d _target { 0.0, 0.0, 0.0 };

    /// The distance between the eye and the target.
    double _distance { 1.0 };

    /// The rotation around the up axis, in degrees.
    double _azimuth { 0.0 };

    /// The rotation above (positive) or below (negative) the horizon, in degrees.
    double _elevation { 0.0 };

    /// The size of the framed model, used to scale the motion speeds and the clipping planes.
    double _diameter { 1.0 };
};

} // namespace HvtViewer
