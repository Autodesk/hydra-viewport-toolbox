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

#include "orbitCamera.h"

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4003)
    #pragma warning(disable : 4244)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/math.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec4f.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cmath>

namespace
{

/// The vertical field of view, in degrees.
constexpr double kFieldOfView = 45.0;

/// The degrees of rotation per pixel of mouse motion.
constexpr double kOrbitSpeed = 0.25;

/// The fraction the eye to target distance is scaled by, for each mouse wheel step.
constexpr double kDollyFactor = 0.9;

/// Keeping the elevation away from the poles avoids a degenerate up direction.
constexpr double kMaxElevation = 85.0;

/// The world up direction.
const pxr::GfVec3d kUpDirection { 0.0, 1.0, 0.0 };

} // anonymous namespace

namespace HvtViewer
{

void OrbitCamera::fit(pxr::GfRange3d const& bounds)
{
    if (bounds.IsEmpty())
    {
        return;
    }

    const pxr::GfVec3d dimensions = bounds.GetSize();
    _diameter                     = std::max(dimensions[0], std::max(dimensions[1], dimensions[2]));
    if (_diameter <= 0.0)
    {
        _diameter = 1.0;
    }

    _target    = bounds.GetMidpoint();
    _distance  = 2.0 * _diameter;
    _azimuth   = 0.0;
    _elevation = 0.0;
}

void OrbitCamera::orbit(double dxPixels, double dyPixels)
{
    _azimuth -= dxPixels * kOrbitSpeed;
    _elevation = std::clamp(_elevation - dyPixels * kOrbitSpeed, -kMaxElevation, kMaxElevation);
}

void OrbitCamera::pan(double dxPixels, double dyPixels, int viewportHeight)
{
    const pxr::GfVec3d forward = (_target - eye()).GetNormalized();
    const pxr::GfVec3d right   = pxr::GfCross(forward, kUpDirection).GetNormalized();
    const pxr::GfVec3d up      = pxr::GfCross(right, forward).GetNormalized();

    // The world size of a pixel at the distance of the target point, so that the point under the
    // mouse cursor keeps following it.
    const double viewHeight =
        2.0 * std::tan(pxr::GfDegreesToRadians(kFieldOfView / 2.0)) * _distance;
    const double worldPerPixel = viewHeight / std::max(1, viewportHeight);

    _target += (right * -dxPixels + up * dyPixels) * worldPerPixel;
}

void OrbitCamera::dolly(double steps)
{
    _distance *= std::pow(kDollyFactor, steps);

    // Keep the eye out of the target point, and within a sensible range of the model.
    _distance = std::clamp(_distance, _diameter * 1.0e-3, _diameter * 1.0e3);
}

pxr::GfVec3d OrbitCamera::eye() const
{
    // The camera starts on the -Z side of the target, as the unit tests do.
    pxr::GfVec3d direction { 0.0, 0.0, -1.0 };
    direction = pxr::GfRotation(pxr::GfVec3d::XAxis(), _elevation).TransformDir(direction);
    direction = pxr::GfRotation(kUpDirection, _azimuth).TransformDir(direction);

    return _target + direction * _distance;
}

pxr::GfMatrix4d OrbitCamera::viewMatrix() const
{
    return pxr::GfMatrix4d().SetLookAt(eye(), _target, kUpDirection);
}

pxr::GfMatrix4d OrbitCamera::projectionMatrix(int width, int height) const
{
    // Note: The aspect ratio must be computed in floating point, otherwise the image is distorted
    // for all the window sizes where the integer division loses the fractional part.
    const double aspectRatio =
        height > 0 ? static_cast<double>(width) / static_cast<double>(height) : 1.0;

    // The far plane follows the eye so that dollying out does not clip the model away.
    const double farDistance  = _distance + _diameter * 5.0;
    const double nearDistance = std::max(farDistance / 10000.0, _diameter / 100.0);

    pxr::GfFrustum frustum;
    frustum.SetPerspective(kFieldOfView, aspectRatio, nearDistance, farDistance);

    return frustum.ComputeProjectionMatrix();
}

pxr::GlfSimpleLightVector OrbitCamera::headlight() const
{
    const pxr::GfVec3d eyePoint = eye();

    pxr::GlfSimpleLight light;
    light.SetPosition(pxr::GfVec4f(static_cast<float>(eyePoint[0]), static_cast<float>(eyePoint[1]),
        static_cast<float>(eyePoint[2]), 1.0f));
    light.SetAmbient(pxr::GfVec4f(0.0f));

    return { light };
}

} // namespace HvtViewer
