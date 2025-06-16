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
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wextra-semi"
#elif defined(_MSC_VER)
#pragma warning(push)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/gf/vec4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/usd/sdf/path.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

HVT_API extern PXR_NS::GfVec4i ToVec4i(PXR_NS::GfVec4d const& v);

/// Returns true if the render index uses the HdStorm render delegate.
HVT_API extern bool IsStormRenderDelegate(PXR_NS::HdRenderIndex const* renderIndex);

/// Returns the rendering backend name e.g., HgiTokens->OpenGL.
HVT_API PXR_NS::TfToken GetRenderingBackendName(PXR_NS::HdRenderIndex const* renderIndex);

/// Sets the blend state for the given material tag in the render task parameters.
HVT_API extern void SetBlendStateForMaterialTag(
    PXR_NS::TfToken const& materialTag, PXR_NS::HdxRenderTaskParams* renderParams);

/// Gets the render task path for the given controller and material tag.
HVT_API extern PXR_NS::SdfPath GetRenderTaskPath(
    PXR_NS::SdfPath const& controllerId, PXR_NS::TfToken const& materialTag);

/// Gets the render task name, without the path prefix, for the given material tag.
HVT_API extern PXR_NS::TfToken GetRenderTaskPathLeaf(PXR_NS::TfToken const& materialTag);

/// Gets the AOV path for the given aov name and parent id.
HVT_API extern PXR_NS::SdfPath GetAovPath(
    PXR_NS::SdfPath const& parentId, PXR_NS::TfToken const& aov);

} // namespace HVT_NS
