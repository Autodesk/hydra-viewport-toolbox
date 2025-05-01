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

#include <hvt/engine/framePass.h>

#include <memory>

namespace hvt
{

/// \name Select helper methods.
/// @{

/// Returns the first picked prim from a specific frame pass.
/// \param pass The selected frame pass.
/// \param pickingMatrix The picking matrix.
/// \param viewport The viewport dimensions.
/// \param viewMatrix The view matrix.
/// \return Returns the first selected prim.
HVT_API extern PXR_NS::SdfPath GetPickedPrim(FramePass* pass,
    PXR_NS::GfMatrix4d const& pickingMatrix, ViewportRect const& viewport,
    PXR_NS::GfMatrix4d const& viewMatrix);

/// @}

/// Highlights selected prims from a specific frame pass.
/// \param framePass The framePass containing the primitives.
/// \param highlightPaths The primitives to highlight.
HVT_API extern void HighlightSelection(
    FramePass* framePass, PXR_NS::SdfPathSet const& highlightPaths);

/// Creates a render buffer proxy mimicking part of PXR_NS::HdStRenderBuffer.
/// \param framePass The framePass containing the primitives.
/// \param aovToken The AOV to encapsulate.
/// \return A HdRenderBuffer instance acting as HdStRenderBuffer instance. 
HVT_API extern std::shared_ptr<PXR_NS::HdRenderBuffer> CreateRenderBufferProxy(
    FramePassPtr& framePass, PXR_NS::TfToken const& aovToken);

} // namespace hvt
