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

#include "taskDataContainer.h"

// TaskContainerSIImpl relies on HdLegacyTaskSchema / HdRetainedSceneIndex which only exist in
// USD >= 25.05. Guard the entire class so that on pre-2505 builds this header contributes nothing
// and avoids -Wunused-private-field warnings from fields that are only read in the guarded .cpp.
#if HVT_HAS_LEGACY_TASK_SCHEMA

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>

namespace HVT_NS
{

/// Scene-index (SI) based task storage.
///
/// Tasks are stored as prims (conforming to HdLegacyTaskSchema) in a retained scene index. The
/// render index discovers each task through the scene index and uses the legacy task factory to
/// instantiate the HdTask.
class TaskContainerSIImpl : public TaskDataContainer
{
public:
    TaskContainerSIImpl(PXR_NS::HdRenderIndex* renderIndex,
        PXR_NS::HdRetainedSceneIndexRefPtr const& retainedSceneIndex);
    ~TaskContainerSIImpl() override = default;

    void Insert(PXR_NS::SdfPath const& taskId, TaskInsertSpec const& spec) override;
    void RemoveTask(PXR_NS::SdfPath const& taskId) override;
    PXR_NS::VtValue GetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key) override;
    bool SetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key,
        PXR_NS::VtValue const& value) override;

private:
    /// The render index (not owned).
    PXR_NS::HdRenderIndex* _renderIndex { nullptr };

    /// The retained scene index used to store task prim data (Hydra 2.0).
    PXR_NS::HdRetainedSceneIndexRefPtr _retainedSceneIndex;
};

} // namespace HVT_NS

#endif // HVT_HAS_LEGACY_TASK_SCHEMA
