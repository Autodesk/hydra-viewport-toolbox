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
#include <hvt/engine/taskDataContainer.h>

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

class FramePassCamera;
class LightingPrimStorage;
class RenderBufferDescriptorStorage;

std::shared_ptr<TaskDataContainer> CreateTaskDataContainer(
    PXR_NS::HdRenderIndex* renderIndex, PXR_NS::SdfPath const& uid,
    bool useLegacySceneDelegate);

std::unique_ptr<FramePassCamera> CreateFramePassCamera(
    PXR_NS::SdfPath const& uid, PXR_NS::HdRenderIndex* renderIndex,
    std::shared_ptr<TaskDataContainer> const& container,
    bool useLegacySceneDelegate);

std::unique_ptr<LightingPrimStorage> CreateLightingPrimStorage(
    std::shared_ptr<TaskDataContainer> const& container,
    PXR_NS::HdRenderIndex* pRenderIndex, bool isHighQualityRenderer,
    bool useLegacySceneDelegate);

std::unique_ptr<RenderBufferDescriptorStorage> CreateRenderBufferDescriptorStorage(
    std::shared_ptr<TaskDataContainer> const& container,
    PXR_NS::HdRenderIndex* pRenderIndex, bool useLegacySceneDelegate);

} // namespace HVT_NS
