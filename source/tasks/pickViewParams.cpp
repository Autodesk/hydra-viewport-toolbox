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

#include <hvt/tasks/pickViewParams.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
// clang-format on

#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

TfToken const& PickViewParams::GetToken()
{
    static const TfToken token { "hvtPickViewParams", TfToken::Immortal };
    return token;
}

CameraUtilFraming GetPickRegionFraming(PickViewParams const& view)
{
    GfRect2i const& region = view.pickRegion;
    const GfVec2f offset(
        static_cast<float>(region.GetMinX()), static_cast<float>(region.GetMinY()));

    GfRange2f const& displayWindow = view.framing.displayWindow;
    return CameraUtilFraming(
        GfRange2f(displayWindow.GetMin() - offset, displayWindow.GetMax() - offset),
        GfRect2i(GfVec2i(0, 0), region.GetWidth(), region.GetHeight()),
        view.framing.pixelAspectRatio);
}

} // namespace HVT_NS
