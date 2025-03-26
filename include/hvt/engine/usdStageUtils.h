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
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wdtor-name"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/usd/usd/stage.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace hvt
{

/// Set visible a box if found.
/// \param stage The stage containing the box.
/// \param isVisible Set visible or not the box.
HVT_API extern void SetVisibleSelectBox(PXR_NS::UsdStageRefPtr& stage, bool isVisible);

/// Update the box position if found.
/// \param stage The stage containing the box.
/// \param x1 The source x.
/// \param y1 The source y.
/// \param x2 The destination x.
/// \param y2 The destination y.
/// \param viewportWidth The viewport width.
/// \param viewportHeight The viewport height.
HVT_API extern void UpdateSelectBox(PXR_NS::UsdStageRefPtr& stage, int x1, int y1, int x2, int y2,
    double viewportWidth, double viewportHeight);

} // namespace hvt