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

#include <hvt/engine/taskDataContainer.h>

#include <hvt/engine/syncDelegate.h>

#include <pxr/imaging/hd/renderIndex.h>

namespace HVT_NS
{

/// Scene-delegate (SD) based task storage.
///
/// Tasks are inserted directly into the render index (HdRenderIndex::InsertTask<T>, type-erased
/// through TaskInsertSpec::sdCreate) and their values are stored in a SyncDelegate. This is the
/// backend used before the migration to Hydra 2.0 scene indices (commit 7bfc0f1).
class TaskContainerSDImpl : public TaskDataContainer
{
public:
    TaskContainerSDImpl(PXR_NS::HdRenderIndex* renderIndex, SyncDelegatePtr const& syncDelegate);
    ~TaskContainerSDImpl() override = default;

    void Insert(PXR_NS::SdfPath const& taskId, TaskInsertSpec const& spec) override;
    void RemoveTask(PXR_NS::SdfPath const& taskId) override;
    PXR_NS::VtValue GetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key) override;
    bool SetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key,
        PXR_NS::VtValue const& value) override;

private:
    /// The render index (not owned).
    PXR_NS::HdRenderIndex* _renderIndex { nullptr };

    /// The scene delegate used to store task values.
    SyncDelegatePtr _syncDelegate;
};

} // namespace HVT_NS
