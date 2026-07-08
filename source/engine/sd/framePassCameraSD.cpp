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

#include "framePassCameraSD.h"

#include <hvt/engine/framePass.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

FramePassCameraSD::FramePassCameraSD(HdRenderIndex* renderIndex, SdfPath const& uid) :
    _delegate(std::make_unique<HdxFreeCameraSceneDelegate>(renderIndex, uid))
{
}

FramePassCameraSD::~FramePassCameraSD() = default;

SdfPath const& FramePassCameraSD::GetCameraId() const
{
    return _delegate->GetCameraId();
}

void FramePassCameraSD::Update(ViewParams const& viewInfo)
{
    _delegate->SetMatrices(viewInfo.viewMatrix, viewInfo.projectionMatrix);
    _delegate->SetClipPlanes(ComputeViewSpaceClipPlanes(viewInfo));
}

} // namespace HVT_NS
