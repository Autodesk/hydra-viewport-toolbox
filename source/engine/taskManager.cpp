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

#include <hvt/engine/taskManager.h>

#include <hvt/engine/taskUtils.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hgi/enums.h>  //Needed for HgiCompareFunctionLEqual;
#include <pxr/imaging/hgi/tokens.h> //Needed for HgiTokens->OpenGL
#include <pxr/imaging/hdx/task.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

TaskManager::TaskManager(
    SdfPath const& uid, HdRenderIndex* renderIndex, SyncDelegatePtr& syncDelegate) :
    _uid(uid), _renderIndex(renderIndex), _syncDelegate(syncDelegate)
{
}

TaskManager::~TaskManager()
{
    // Iterate the task entries, and remove the corresponding tasks from the render index.
    // NOTE: The task list itself is automatically destroyed.
    for (TaskEntry const& taskEntry : _tasks)
    {
        _renderIndex->RemoveTask(taskEntry.uid);
    }
}

bool TaskManager::HasTask(SdfPath const& uid) const
{
    auto itExisting = _GetTaskEntry(uid);
    return (itExisting != _tasks.end());
}

void TaskManager::RemoveTask(SdfPath const& uid)
{
    auto it = _GetTaskEntry(uid);
    if (it != _tasks.end())
    {
        // Erase the task entry, and remove the task from the render index.
        _tasks.erase(it);
        _renderIndex->RemoveTask(uid);
    }
}

void TaskManager::EnableTask(SdfPath const& uid, bool enable)
{
    auto it = _GetTaskEntry(uid);
    if (it != _tasks.end())
    {
        // Set the is-enabled flag on the task entry.
        it->isEnabled = enable;
    }
}

HdTaskSharedPtrVector TaskManager::CommitTaskValues(TaskFlags taskFlags)
{
    HdTaskSharedPtrVector enabledTasks;

    // Prepare the tasks for execution by getting the updated values they need, and adding them to
    // the list executed by the engine.
    // NOTE: This is a lazy update, which is simpler than immediately doing work when global
    // parameters are updated, and removes the need for a separate function to set task values.
    for (const auto& taskEntry : _tasks)
    {
        // Skip any tasks that are disabled.
        if (!taskEntry.isEnabled || !(taskEntry.flags & taskFlags))
        {
            continue;
        }

        if (taskEntry.fnCommit)
        {
            // Bind the fnGetValue and fnSetValue callbacks to the actual TaskManager::GetTaskValue
            // and TaskManager::SetTaskValue functions. This binding provides the necessary
            // parameters so the task commit function does not have to provide the task id and the
            // TaskManager (this) instance.
            using namespace std::placeholders;
            auto fnGetValue = std::bind(&TaskManager::GetTaskValue, this, taskEntry.uid, _1);
            auto fnSetValue = std::bind(&TaskManager::SetTaskValue, this, taskEntry.uid, _1, _2);

            // Call the supplied CommitTaskFn to make sure the values needed by the task are
            // available on the sync delegate, merged with the global parameters as needed.
            taskEntry.fnCommit(fnGetValue, fnSetValue);
        }

        // Add the task object (from the render index) to the list of tasks to execute.
        // NOTE: Doing this here allows for dynamic filtering of tasks later if needed.
        enabledTasks.push_back(_renderIndex->GetTask(taskEntry.uid));
    }

    return enabledTasks;
}

void TaskManager::Execute(HdEngine* engine)
{
    // Run the commit task value function for each enabled tasks.
    HdTaskSharedPtrVector enabledTasks = CommitTaskValues(TaskFlagsBits::kExecutableBit);

    // Return if no tasks were prepared for execution.
    if (enabledTasks.empty())
    {
        return;
    }

    // Execute the engine with the list of tasks.
    engine->Execute(_renderIndex, &enabledTasks);
}

TaskManager::TaskList::iterator TaskManager::_GetTaskEntry(SdfPath const& uid)
{
    // A function that compares task entries by the task ID only.
    auto compare = [&uid](TaskEntry const& taskEntry) -> bool { return taskEntry.uid == uid; };

    // Find and return the iterator of the task entry with the specified task ID.
    return std::find_if(_tasks.begin(), _tasks.end(), compare);
}

TaskManager::TaskList::const_iterator TaskManager::_GetTaskEntry(SdfPath const& uid) const
{
    // A function that compares task entries by the task ID only.
    auto compare = [&uid](TaskEntry const& taskEntry) -> bool { return taskEntry.uid == uid; };

    // Find and return the iterator of the task entry with the specified task ID.
    return std::find_if(_tasks.begin(), _tasks.end(), compare);
}

VtValue TaskManager::GetTaskValue(SdfPath const& uid, TfToken const& key)
{
    return _syncDelegate->GetValue(uid, key);
}

void TaskManager::SetTaskValue(SdfPath const& uid, TfToken const& key, VtValue const& value)
{
    if (uid.IsEmpty())
    {
        TF_CODING_ERROR("Task id cannot be empty.");
        return;
    }

    // If the sync delegate already has a value, and the value is unchanged, return early.
    // DESIGN NOTE: See OGSMOD-6765
    // We should double-check this automatic comparison to make sure all use cases are covered.
    // This was causing a crash with AovInputTaskParams, for which the compare function would
    // only check BufferPaths but wouldn't check the buffer pointers.
    // This was causing an early out here, instead of updating the delegate value.
    //
    // In other words, we have to make SURE that all task parameters operator==(lhs, rhs) are
    // fully implemented for this update strategy to work, since there is now no way to FORCE a
    // parameter update and dirty flag, unlike with the previous implementation where delegate
    // parameters could be forcibly set and marked dirty.
    //
    // I am leaving this note as a warning, and if we feel this is something we could need to have
    // a workaround for in the future, we could add an optional parameter of type HdDirtyBits
    // to TaskManager::SetTaskValue() with the default value of HdChangeTracker::Clean for an
    // automatic comparison, or force the update in case we are stuck with an Hdx task parameter
    // that has an incomplete operator==.
    if (_syncDelegate->HasValue(uid, key))
    {
        VtValue const& committedValue = _syncDelegate->GetValue(uid, key);
        if (value == committedValue)
        {
            return;
        }
    }

    // Set the value on the sync delegate.
    _syncDelegate->SetValue(uid, key, value);

    // Set the appropriate task dirty bit based on the key value, and mark the task dirty on the
    // render index.
    // NOTE: This function only handles changes to task values, hence the name "CommitTaskValue".
    // Changes to non-task values, e.g. for lights, should be handled directly on the sync delegate.
    HdDirtyBits dirtyBits = HdChangeTracker::Clean;
    if (key == HdTokens->params)
    {
        dirtyBits |= HdChangeTracker::DirtyParams;
    }
    else if (key == HdTokens->collection)
    {
        dirtyBits |= HdChangeTracker::DirtyCollection;
    }
    else if (key == HdTokens->renderTags)
    {
        dirtyBits |= HdChangeTracker::DirtyRenderTags;
    }

    if (dirtyBits != HdChangeTracker::Clean)
    {
        _renderIndex->GetChangeTracker().MarkTaskDirty(uid, dirtyBits);
    }
}

HdTaskSharedPtrVector const TaskManager::GetTasks(TaskFlags taskFlags) const
{
    HdTaskSharedPtrVector filteredTasks;

    for (TaskEntry const& task : _tasks)
    {
        if (!(task.flags & taskFlags) || !task.isEnabled)
        {
            continue;
        }

        HdTaskSharedPtr pTask = _renderIndex->GetTask(task.uid);
        filteredTasks.push_back(pTask);
    }
    return filteredTasks;
}

SdfPath TaskManager::GetTaskPath(TfToken const& instanceName)
{
    const SdfPath taskId = BuildTaskPath(instanceName);
    return HasTask(taskId) ? taskId : SdfPath::EmptyPath();
}

SdfPath TaskManager::BuildTaskPath(TfToken const& instanceName)
{
    return _uid.AppendChild(instanceName);
}

bool TaskManager::IsConverged() const
{
    bool converged = true;

    for (TaskEntry const& task : _tasks)
    {
        HdTaskSharedPtr pTask                    = _renderIndex->GetTask(task.uid);
        std::shared_ptr<HdxTask> progressiveTask = std::dynamic_pointer_cast<HdxTask>(pTask);
        if (progressiveTask)
        {
            converged = converged && progressiveTask->IsConverged();
            if (!converged)
            {
                break;
            }
        }
    }

    return converged;
}

} // namespace hvt
