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

#include <hvt/api.h>

#include <pxr/pxr.h>

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/usd/sdf/path.h>

#include <functional>
#include <iosfwd>
#include <memory>
#include <utility>

// legacyTaskSchema.h / legacyTaskFactory.h (HdLegacyTaskFactorySharedPtr, HdMakeLegacyTaskFactory)
// were introduced in USD 25.05 (PXR_VERSION 2505) and do not exist before then (e.g. 24.11/25.02).
// The scene-delegate (SD) task backend is the only one available before that version; the
// scene-index (SI) task backend requires them.
#ifndef HVT_SI_TASK_BACKEND_SUPPORTED
#define HVT_SI_TASK_BACKEND_SUPPORTED (PXR_VERSION >= 2505)
#endif

#if HVT_SI_TASK_BACKEND_SUPPORTED
#include <pxr/imaging/hd/legacyTaskSchema.h>
#include <pxr/imaging/hd/legacyTaskFactory.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderIndex;
class HdSceneDelegate;
PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS
{

/// \name Backend selection.
/// @{

/// Returns whether the legacy scene-delegate (SD) task backend is selected.
///
/// HVT supports two rendering backends:
///  - scene-index (SI): the Hydra 2.0 retained-scene-index based path (default);
///  - scene-delegate (SD): the legacy HdSceneDelegate based path.
///
/// The backend is selected at FramePass construction time. The default is SI (this function
/// returns false), overridable through the \c HVT_USE_LEGACY_SCENE_DELEGATE environment variable
/// and at runtime via SetUseLegacySceneDelegate().
///
/// \note On USD versions that lack HdLegacyTaskSchema (e.g. 24.11) the SI backend cannot be built,
/// so this always returns true there regardless of the environment variable or
/// SetUseLegacySceneDelegate().
HVT_API bool UseLegacySceneDelegate();

/// Selects the rendering backend used by FramePass instances created afterwards.
/// \param useLegacySceneDelegate true to use the legacy scene-delegate (SD) backend, false for the
///        scene-index (SI) backend.
/// \note Has no effect on USD versions where the SI backend is unavailable (the backend then stays
/// SD). Already-constructed FramePass instances keep the backend they were created with.
HVT_API void SetUseLegacySceneDelegate(bool useLegacySceneDelegate);

/// @}

/// Backend-independent description of how to create/insert a task.
///
/// The SI backend uses \p siFactory (a legacy task factory consumed by the retained scene index).
/// The SD backend uses \p sdCreate (a type-erased lambda that inserts the task into the render
/// index through the scene delegate). \p params holds the initial task parameters for both.
struct TaskCreateInfo
{
    /// Type-erased task creator for the scene-delegate (SD) backend.
    using SdTaskCreatorFn = std::function<void(PXR_NS::HdRenderIndex* renderIndex,
        PXR_NS::HdSceneDelegate* sceneDelegate, PXR_NS::SdfPath const& taskId)>;

    /// SD backend: inserts the task into the render index via the scene delegate.
    SdTaskCreatorFn sdCreate;

#if HVT_SI_TASK_BACKEND_SUPPORTED
    /// SI backend: legacy task factory used to instantiate the HdTask from the scene index.
    PXR_NS::HdLegacyTaskFactorySharedPtr siFactory;
#endif

    /// The initial task parameters.
    PXR_NS::VtValue params;
};

/// Builds a TaskCreateInfo for the task type \p T, ready to be consumed by TaskBackend::CreateTask.
///
/// Populates \p params plus the scene-delegate (SD) creator that inserts the task into the render
/// index and, on builds where the scene-index (SI) task backend is enabled, the legacy task factory
/// consumed by the retained scene index.
template <typename T>
TaskCreateInfo MakeTaskCreateInfo(PXR_NS::VtValue params)
{
    TaskCreateInfo createInfo;
    createInfo.params = std::move(params);

    createInfo.sdCreate = [](PXR_NS::HdRenderIndex* renderIndex,
                              PXR_NS::HdSceneDelegate* sceneDelegate, PXR_NS::SdfPath const& id)
    { renderIndex->InsertTask<T>(sceneDelegate, id); };

#if HVT_SI_TASK_BACKEND_SUPPORTED
    static PXR_NS::HdLegacyTaskFactorySharedPtr const siFactory =
        PXR_NS::HdMakeLegacyTaskFactory<T>();
    createInfo.siFactory = siFactory;
#endif

    return createInfo;
}

/// Abstract backend for TaskManager tasks: storage, registration and value access.
///
/// TaskManager owns a TaskBackendSharedPtr and delegates only the backend-specific task
/// storage, registration and value access to it. The scene-index (SI) and scene-delegate (SD)
/// implementations both derive from this interface, so the backend can be selected at runtime
/// without changing TaskManager itself.
class TaskBackend
{
public:
    virtual ~TaskBackend() = default;

    /// Detaches backend resources from the render index.
    /// Must be called before the render index is destroyed.
    virtual void Uninitialize(PXR_NS::HdRenderIndex& renderIndex) = 0;

    /// Creates/registers the task with the given id from the insert spec.
    virtual void CreateTask(PXR_NS::SdfPath const& taskId, TaskCreateInfo const& spec) = 0;

    /// Removes the task with the given id from storage.
    virtual void RemoveTask(PXR_NS::SdfPath const& taskId) = 0;

    /// Gets a task value (params, collection or renderTags).
    virtual PXR_NS::VtValue GetValue(
        PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key) = 0;

    /// Sets a task value (params, collection or renderTags).
    /// \return True if the value was accepted (changed or unchanged), false on error.
    virtual bool SetValue(PXR_NS::SdfPath const& taskId, PXR_NS::TfToken const& key,
        PXR_NS::VtValue const& value) = 0;

    /// Prints a debugging summary of all stored task data to the given stream.
    virtual void PrintTaskData(std::ostream& out, PXR_NS::SdfPath const& rootPath) const = 0;

    /// Marks the parameters of the given tasks as dirty so they re-sync on the next commit.
    virtual void MarkTaskParamsDirty(PXR_NS::SdfPathVector const& taskPaths) = 0;
};

using TaskBackendSharedPtr = std::shared_ptr<TaskBackend>;

} // namespace HVT_NS
