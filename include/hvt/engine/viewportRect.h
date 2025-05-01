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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4i.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <ostream>

namespace HVT_NS
{

/// Defines the position and size of a viewport on the screen.
struct HVT_API ViewportRect
{
    /// Top left corner of the viewport on the screen.
    PXR_NS::GfVec2i position {};

    /// Width and height of the viewport on the screen.
    PXR_NS::GfVec2i size {};

    /// Returns the viewport rect as a PXR_NS::GfVec4i (x, y, width, height).
    PXR_NS::GfVec4i ConvertToVec4i() const
    {
        return PXR_NS::GfVec4i(position[0], position[1], size[0], size[1]);
    }

    /// Returns the viewport rect as a PXR_NS::GfRect2i.
    PXR_NS::GfRect2i ConvertToRect2i() const
    {
        return PXR_NS::GfRect2i(PXR_NS::GfVec2i(position[0], position[1]), size[0], size[1]);
    }

    bool operator==(ViewportRect const& rhs) const
    {
        return position == rhs.position && size == rhs.size;
    }

    bool operator!=(ViewportRect const& rhs) const { return !(*this == rhs); }
};

/// Operator to stream out the ViewportRect.
HVT_API inline std::ostream& operator<<(std::ostream& out, ViewportRect const& rect)
{
    out << "ViewportRect: Position: " << rect.position << " Size: " << rect.size;
    return out;
}

} // namespace HVT_NS
