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
#include <hvt/engine/taskBackend.h>

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

// These types are opaque/internal to HVT: they are forward-declared here only so the factory
// functions below can be declared. Their definitions are private (source/engine/) and not
// installed, so external callers can only pass the resulting handles back into HVT (e.g. through
// FramePass), not dereference or destroy them directly.
class FramePassCamera;
class LightingPrimBackend;
class RenderBufferPrimBackend;

/// Creates the backend-specific (SI or SD) task backend.
HVT_API
std::shared_ptr<TaskBackend> CreateTaskBackend(
    PXR_NS::HdRenderIndex* renderIndex, PXR_NS::SdfPath const& uid,
    bool useLegacySceneDelegate);

/// Creates the backend-specific (SI or SD) frame pass camera.
HVT_API
std::unique_ptr<FramePassCamera> CreateFramePassCamera(
    PXR_NS::SdfPath const& uid, PXR_NS::HdRenderIndex* renderIndex,
    std::shared_ptr<TaskBackend> const& container,
    bool useLegacySceneDelegate);

/// Creates the backend-specific (SI or SD) lighting prim backend.
HVT_API
std::unique_ptr<LightingPrimBackend> CreateLightingPrimBackend(
    std::shared_ptr<TaskBackend> const& container,
    PXR_NS::HdRenderIndex* pRenderIndex, bool isHighQualityRenderer,
    bool useLegacySceneDelegate);

/// Creates the backend-specific (SI or SD) render buffer prim backend.
HVT_API
std::unique_ptr<RenderBufferPrimBackend> CreateRenderBufferPrimBackend(
    std::shared_ptr<TaskBackend> const& container,
    PXR_NS::HdRenderIndex* pRenderIndex, bool useLegacySceneDelegate);

} // namespace HVT_NS
