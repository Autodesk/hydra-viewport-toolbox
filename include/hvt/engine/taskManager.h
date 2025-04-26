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

#include <hvt/engine/syncDelegate.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/path.h>

#include <functional>
#include <list>
#include <memory>

namespace hvt
{

class TaskManager;
using TaskManagerPtr = std::unique_ptr<TaskManager>;

using TaskFlags = unsigned int;

namespace TaskFlagsBits
{
/// The set of task properties that can be combined to define a task.
/// This is useful for filtering tasks based on their properties.
enum : TaskFlags
{
    kExecutableBit  = 0x00000001, // Task that is run by TaskManager::Execute.
    kRenderTaskBit  = 0x00000002, // Task derived from HdxRenderTask.
    kPickingTaskBit = 0x00000004  // Task used for picking.
};

} // namespace TaskFlagsBits

/// A class that maintains an ordered list of Hydra tasks and prepares them for execution with a
/// Hydra engine.
class HVT_API TaskManager
{
public:
    /// A type of function provided to clients that can be used to get task values.
    using GetTaskValueFn = std::function<PXR_NS::VtValue(PXR_NS::TfToken const& key)>;

    /// A type of function provided to clients that can be used to set task values.
    using SetTaskValueFn =
        std::function<void(PXR_NS::TfToken const& key, PXR_NS::VtValue const& value)>;

    /// A type of function provided by clients that is called when the task values are to be
    /// committed, before task execution.
    ///
    /// The client can use fnCommitValue to store values needed during the Sync phase, which will
    /// be made available through the scene delegate. The client can merge any required global
    /// parameters into their values. For example, a single value may be a parameter structure, and
    /// a member of that structure come from the global parameters.
    using CommitTaskFn =
        std::function<void(GetTaskValueFn const& fnGetValue, SetTaskValueFn const& fnSetValue)>;

    /// Constructor.
    /// \param uid The unique identifier.
    /// \param renderIndex The render index.
    /// \param syncDelegate The sync delegate.
    TaskManager(PXR_NS::SdfPath const& uid, PXR_NS::HdRenderIndex* renderIndex,
        SyncDelegatePtr& syncDelegate);

    /// Destructor.
    ~TaskManager();

    /// Gets the unique identifier i.e., path.
    inline PXR_NS::SdfPath GetPath() const { return _uid; }

    /// Gets the render index instance.
    PXR_NS::HdRenderIndex* GetRenderIndex() { return _renderIndex; }

    /// Insert location specifier
    enum class InsertionOrder
    {
        insertBefore,
        insertAfter,
        insertAtEnd
    };

    /// Adds a task to the task manager, with the specified unique ID and CommitTaskFn callback
    /// function for the task.
    /// \param taskName The task instance Name.
    /// \param initialParams The task parameters set during initialization. 
    /// \param fnCommit The task callback i.e., way to update the task parameters.
    /// \param atPos The unique identifier of the task where to insert this new task.
    /// \param order The insertion order relative to atPos.
    /// \param taskFlags The task classification flags.
    ///
    /// \note By default a task is added to the end of the ordered list maintained by the task
    /// manager when the position path is empty or the order is InsertionOrder::insertAtEnd.
    template <typename T, typename TParam>
    PXR_NS::SdfPath AddTask(PXR_NS::TfToken const& taskName, TParam initialParams,
        CommitTaskFn const& fnCommit, PXR_NS::SdfPath const& atPos = PXR_NS::SdfPath(),
        InsertionOrder order = InsertionOrder::insertAtEnd,
        TaskFlags taskFlags  = TaskFlagsBits::kExecutableBit);

    /// Adds a render task to the task manager with the specified unique ID and CommitTaskFn
    /// callback function for the task before a specific task (identified by its path).
    /// \param id The task identifier.
    /// \param initialParams The task parameters set during initialization. 
    /// \param fnCommit The task callback i.e., way to update the task parameters.
    /// \param atPos The unique identifier of the task where to insert this new task.
    ///
    /// \note A task is added at the end of the ordered list maintained by the task
    /// manager when the position path is empty; otherwise, it adds the task before it.
    template <typename T, typename TParam>
    PXR_NS::SdfPath AddRenderTask(PXR_NS::TfToken const& taskName, TParam initialParams,
        CommitTaskFn const& fnCommit, PXR_NS::SdfPath const& atPos = PXR_NS::SdfPath());

    /// Returns true if the specified task has been added and exists in the currently managed task
    /// list.
    /// \param uid The task unique identifier.
    bool HasTask(PXR_NS::SdfPath const& uid) const;

    /// Returns true if the specified task has been added and exists in the currently managed task
    /// list.
    /// \param instanceName The task instance name.
    bool HasTask(PXR_NS::TfToken const& instanceName) const;

    /// Removes the task with the specified task ID.
    /// \param uid The task unique identifier.
    void RemoveTask(PXR_NS::SdfPath const& uid);

    /// Removes the task with the specified task ID.
    /// \param instanceName The task instance name.
    void RemoveTask(PXR_NS::TfToken const& instanceName);

    /// Sets whether the task with the specified task ID is enabled, i.e. will be included during
    /// task execution.
    /// \param uid The task unique identifier.
    /// \param enable Enables or not the task's execution.
    void EnableTask(PXR_NS::SdfPath const& uid, bool enable);

    /// Sets whether the task with the specified task ID is enabled, i.e. will be included during
    /// task execution.
    /// \param instanceName The task instance name.
    /// \param enable Enables or not the task's execution.
    void EnableTask(PXR_NS::TfToken const& instanceName, bool enable);

    /// Runs the task commit function for each enabled task.
    /// \param taskFlags The task classification flags.
    PXR_NS::HdTaskSharedPtrVector CommitTaskValues(TaskFlags taskFlags);

    /// Executes the enabled tasks.
    void Execute(PXR_NS::HdEngine* engine);

    /// Gets the task value with the specified task unique identifier and key.
    PXR_NS::VtValue GetTaskValue(PXR_NS::SdfPath const& uid, PXR_NS::TfToken const& key);

    /// Sets the task value with the specified task unique identifier and key.
    void SetTaskValue(
        PXR_NS::SdfPath const& uid, PXR_NS::TfToken const& key, PXR_NS::VtValue const& value);

    /// Get the list of render tasks (derived from HdxRenderTask).
    PXR_NS::SdfPathVector const& GetRenderTasks() { return _renderTaskIds; }

    /// Returns true if the rendering task list has converged.
    bool IsConverged() const;

    /// Gets the list of tasks matching the specified task flags.
    /// \param taskFlags The task type.
    /// \return A list of tasks.
    PXR_NS::HdTaskSharedPtrVector const GetTasks(TaskFlags taskFlags) const;

    /// Gets the task unique identifier from its name.
    /// \param token The task instance name.
    /// \return A reference to the task unique identifier or an empty path if not found.
    /// \note The returned task SdfPath reference is valid until the task is removed.
    const PXR_NS::SdfPath& GetTaskPath(PXR_NS::TfToken const& instanceName) const;

    /// Builds the task unique identifier.
    /// \param instanceName The task instance name.
    /// \return The unique identifier.
    PXR_NS::SdfPath BuildTaskPath(PXR_NS::TfToken const& instanceName);

private:
    /// The description of a task, as maintained by the task manager.
    struct TaskEntry
    {
        /// The task unique identifier.
        PXR_NS::SdfPath uid;
        /// The task commit callback i.e., method to update the task's parameters.
        CommitTaskFn fnCommit;
        /// Defines if the task is enabled or not.
        bool isEnabled = false;
        /// Defines the task flags.
        TaskFlags flags = TaskFlagsBits::kExecutableBit;
    };

    const PXR_NS::SdfPath& _AddTask(pxr::TfToken const& taskName, CommitTaskFn const& fnCommit,
        PXR_NS::SdfPath const& atPos, InsertionOrder order, TaskFlags taskFlags);

    /// A type for an ordered list of task entries.
    using TaskList = std::list<TaskEntry>;

    /// The unique identifier for this task manager.
    const PXR_NS::SdfPath _uid;

    /// The render index used to insert and remove Tasks.
    PXR_NS::HdRenderIndex* _renderIndex { nullptr };

    /// The scene delegate used to provide task data.
    SyncDelegatePtr _syncDelegate;

    /// The list of tasks maintained by the task manager.
    TaskList _tasks;

    /// The list of render tasks that are derived from HdxRenderTask.
    PXR_NS::SdfPathVector _renderTaskIds;
};

template <typename T, typename TParam>
PXR_NS::SdfPath TaskManager::AddTask(PXR_NS::TfToken const& taskName, TParam initialParams,
    CommitTaskFn const& fnCommit, PXR_NS::SdfPath const& atPos, InsertionOrder order,
    TaskFlags taskFlags)
{
    PXR_NS::SdfPath const& taskId = _AddTask(taskName, fnCommit, atPos, order, taskFlags);

    if (taskId != PXR_NS::SdfPath::EmptyPath())
    {
        // Add the task to the render index, associated with the internal parameters scene delegate.
        // NOTE: This is the scene delegate that the task receives in its Sync() function.
        _renderIndex->InsertTask<T>(_syncDelegate.get(), taskId);
    }

    SetTaskValue(taskId, pxr::HdTokens->params, pxr::VtValue(initialParams));

    return taskId;
}

template <typename T, typename TParam>
PXR_NS::SdfPath TaskManager::AddRenderTask(PXR_NS::TfToken const& taskName, TParam initialParams,
    CommitTaskFn const& fnCommit, PXR_NS::SdfPath const& atPos)
{
    // Rendering tasks are both renderable and derived from HdxRenderTask.
    static const TaskFlags renderTaskFlags =
        TaskFlagsBits::kRenderTaskBit | TaskFlagsBits::kExecutableBit;

    PXR_NS::SdfPath renderTaskId = AddTask<T>(
        taskName, initialParams, fnCommit, atPos, InsertionOrder::insertBefore, renderTaskFlags);
    if (!renderTaskId.IsEmpty())
    {
        _renderTaskIds.push_back(renderTaskId);
    }

    return renderTaskId;
}

} // namespace hvt
