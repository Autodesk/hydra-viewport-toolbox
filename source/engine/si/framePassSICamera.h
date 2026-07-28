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

#include "../framePassCamera.h"

#include <hvt/engine/taskBackend.h> // For HVT_SI_TASK_BACKEND_SUPPORTED.

// FramePassSICamera relies on HdRetainedSceneIndex which only exists in USD >= 25.05. Guard the
// entire class so that on pre-2505 builds this header contributes nothing.
#if HVT_SI_TASK_BACKEND_SUPPORTED

#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/usd/sdf/path.h>

namespace HVT_NS
{

/// Scene-index (SI) based free camera.
///
/// Stores a camera prim (conforming to HdCameraSchema) in a retained scene index. This is the
/// backend used after the migration of the TaskManager to Hydra 2.0 scene indices.
class FramePassSICamera : public FramePassCamera
{
public:
    FramePassSICamera(PXR_NS::SdfPath const& uid,
        PXR_NS::HdRetainedSceneIndexRefPtr const& retainedSceneIndex);

    PXR_NS::SdfPath const& GetCameraId() const override;
    void Update(ViewParams const& viewInfo) override;

private:
    PXR_NS::SdfPath _cameraId;
    PXR_NS::HdRetainedSceneIndexRefPtr _retainedSceneIndex;
};

} // namespace HVT_NS

#endif // HVT_SI_TASK_BACKEND_SUPPORTED
