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

#include <hvt/engine/usdStageUtils.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/xformable.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

namespace
{

// Path for select box object
const SdfPath selectBoxGizmoPath { "/frozen/selectBoxGizmo" };

} // anonymous namespace

void SetVisibleSelectBox(UsdStageRefPtr& stage, bool isVisible)
{
    auto prim = stage->GetPrimAtPath(selectBoxGizmoPath);
    if (prim.IsValid())
    {
        // Set the visibility.

        if (isVisible)
        {
            UsdGeomImageable(prim).MakeVisible();
        }
        else
        {
            UsdGeomImageable(prim).MakeInvisible();
        }
    }
}

void UpdateSelectBox(UsdStageRefPtr& stage, int x1, int y1, int x2, int y2, double viewportWidth,
    double viewportHeight)
{
    auto prim = stage->GetPrimAtPath(selectBoxGizmoPath);
    if (prim.IsValid())
    {
        // Update the position & size.

        const auto xMin = std::min(x1, x2) / viewportWidth;
        const auto xMax = std::max(x1, x2) / viewportWidth;
        const auto yMin = std::min(y1, y2) / viewportHeight;
        const auto yMax = std::max(y1, y2) / viewportHeight;

        bool resetStack = true;
        auto tm         = UsdGeomXformable(prim);
        if (tm)
        {
            // Translate the primitive.

            auto xFormOps = tm.GetOrderedXformOps(&resetStack);
            const auto translateVec =
                GfVec3d(xMin * 2.0 - 1.0, 1.0 - (yMin * 2.0 - 1.0) - 1.0, 0.0f);
            xFormOps[0].Set(translateVec);

            // Scale the primitive.

            const auto scaleVec = GfVec3f((xMax - xMin) * 2.0, (yMax - yMin) * 2.0, 1.0f);
            xFormOps[1].Set(scaleVec);
        }
    }
}

} // namespace hvt
