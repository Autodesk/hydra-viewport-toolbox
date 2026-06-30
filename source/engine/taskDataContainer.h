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
#include <hvt/engine/taskManager.h>

#include <pxr/pxr.h>

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>


#include <functional>

// legacyTaskSchema.h / legacyTaskFactory.h (HdLegacyTaskFactorySharedPtr, HdMakeLegacyTaskFactory)
// were introduced in USD 25.05 (PXR_VERSION 2505) and do not exist before then (e.g. 24.11/25.02).
// The scene-index (SI) task backend requires them; the scene-delegate (SD) backend does not.
#ifndef HVT_HAS_LEGACY_TASK_SCHEMA
#define HVT_HAS_LEGACY_TASK_SCHEMA (PXR_VERSION >= 2505)
#endif

#if HVT_HAS_LEGACY_TASK_SCHEMA
#include <pxr/imaging/hd/legacyTaskSchema.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderIndex;
class HdSceneDelegate;
PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS
{



/// Abstract storage/registration strategy for TaskManager tasks.
///
/// TaskManager owns a std::unique_ptr<TaskDataContainer> and delegates only the backend-specific
/// task storage, registration and value access to it. The scene-index (SI) and scene-delegate (SD)
/// implementations both derive from this interface, so the backend can be selected at runtime
/// without changing TaskManager itself.
class TaskDataContainer
{
public:
    virtual ~TaskDataContainer() = default;

    /// Creates/registers the task with the given id from the insert spec.
    virtual void Insert(PXR_NS::SdfPath const& taskId, TaskInsertSpec const& spec) = 0;

    /// Removes the task with the given id from storage.
    virtual void RemoveTask(PXR_NS::SdfPath const& taskId) = 0;

    /// Gets a task value (params, collection or renderTags).
    virtual PXR_NS::VtValue GetValue(
        PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key) = 0;

    /// Sets a task value (params, collection or renderTags).
    /// \return True if the value was accepted (changed or unchanged), false on error.
    virtual bool SetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key,
        PXR_NS::VtValue const& value) = 0;
};

} // namespace HVT_NS
