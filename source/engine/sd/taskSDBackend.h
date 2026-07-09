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

#include "syncDelegate.h"

#include <pxr/imaging/hd/renderIndex.h>

namespace HVT_NS
{

/// Scene-delegate (SD) based task backend.
///
/// Tasks are inserted directly into the render index (HdRenderIndex::InsertTask<T>, type-erased
/// through TaskCreateInfo::sdCreate) and their values are stored in a SyncDelegate. This is the
/// backend used before the migration to Hydra 2.0 scene indices (commit 7bfc0f1).
class TaskSDBackend : public TaskBackend
{
public:
    TaskSDBackend(PXR_NS::HdRenderIndex* renderIndex, PXR_NS::SdfPath const& uid);
    ~TaskSDBackend() override;

    void Uninitialize(PXR_NS::HdRenderIndex& renderIndex) override;
    void CreateTask(PXR_NS::SdfPath const& taskId, TaskCreateInfo const& spec) override;
    void RemoveTask(PXR_NS::SdfPath const& taskId) override;
    PXR_NS::VtValue GetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key) override;
    bool SetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key,
        PXR_NS::VtValue const& value) override;
    void PrintTaskData(std::ostream& out, PXR_NS::SdfPath const& rootPath) const override;
    void MarkTaskParamsDirty(PXR_NS::SdfPathVector const& taskPaths) override;

    SyncDelegatePtr const& GetSyncDelegate() const { return _syncDelegate; }

private:
    PXR_NS::HdRenderIndex* _renderIndex { nullptr };
    SyncDelegatePtr _syncDelegate;
};

} // namespace HVT_NS
