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
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/gf/vec2i.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hgi/hgi.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
// clang-format on

namespace HVT_NS
{

/// Computes the position of a render buffer in the full image, and the full image size, in the
/// row order of the backend's textures i.e., the convention the shaders read and write.
///
/// The framing gives the full image as its display window and the render buffer as its data
/// window, so the buffer origin is the negated display window minimum, and the image size is
/// the display window size. The y axis is flipped for a backend whose textures store the bottom
/// row first, so that the offset addresses texels in the order they are stored.
///
/// \param framing The framing of the render i.e., display window: full image, data window:
/// render buffer.
/// \param bufferSize The render buffer size, in pixels.
/// \param isTopDown True when the backend's textures store the top row first (Metal), false when
/// they store the bottom row first (OpenGL, Vulkan).
/// \param outOffset The full-image pixel at the render buffer origin, in texture row order.
/// \param outImageSize The full image size, in pixels.
/// \note An invalid framing yields a zero offset and the buffer size as the image size, which
/// addresses a full-image (untiled) render.
HVT_API void ComputeImagePixelOffset(PXR_NS::CameraUtilFraming const& framing,
    PXR_NS::GfVec2i const& bufferSize, bool isTopDown, PXR_NS::GfVec2i* outOffset,
    PXR_NS::GfVec2i* outImageSize);

/// Returns true when the textures of the given backend store the first row at the top of the
/// image (Metal), false when they store the bottom row first (OpenGL, Vulkan).
HVT_API bool IsTopDownTextureRowOrder(PXR_NS::Hgi const* hgi);

} // namespace HVT_NS
