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

#include <hvt/engine/taskBackend.h>

// TaskSIBackend relies on HdLegacyTaskSchema / HdRetainedSceneIndex which only exist in
// USD >= 25.05. Guard the entire class so that on pre-2505 builds this header contributes nothing
// and avoids -Wunused-private-field warnings from fields that are only read in the guarded .cpp.
#if HVT_HAS_LEGACY_TASK_SCHEMA

#include <pxr/imaging/hd/retainedSceneIndex.h>

namespace HVT_NS
{

/// Scene-index (SI) based task backend.
///
/// Tasks are stored as prims (conforming to HdLegacyTaskSchema) in a retained scene index. The
/// render index discovers each task through the scene index and uses the legacy task factory to
/// instantiate the HdTask.
class TaskSIBackend : public TaskBackend
{
public:
    TaskSIBackend(PXR_NS::HdRenderIndex* renderIndex, PXR_NS::SdfPath const& uid);
    ~TaskSIBackend() override;

    void Uninitialize(PXR_NS::HdRenderIndex& renderIndex) override;
    void Insert(PXR_NS::SdfPath const& taskId, TaskInsertSpec const& spec) override;
    void RemoveTask(PXR_NS::SdfPath const& taskId) override;
    PXR_NS::VtValue GetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key) override;
    bool SetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key,
        PXR_NS::VtValue const& value) override;
    void PrintTaskData(std::ostream& out, PXR_NS::SdfPath const& rootPath) const override;
    void MarkTaskParamsDirty(PXR_NS::SdfPathVector const& taskPaths) override;

    PXR_NS::HdRetainedSceneIndexRefPtr const& GetRetainedSceneIndex() const
    {
        return _retainedSceneIndex;
    }

private:
    PXR_NS::HdRetainedSceneIndexRefPtr _retainedSceneIndex;
};

} // namespace HVT_NS

#endif // HVT_HAS_LEGACY_TASK_SCHEMA
