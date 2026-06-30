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

#include <pxr/imaging/hdx/task.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

#include <algorithm>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

///////////////////////////////////////////////////////////////////////////////
// Task entry helpers (operating on the TaskList)

template <typename TaskEntryType>
bool CheckTaskFlags(TaskEntryType const& taskEntry, TaskFlags taskFlags)
{
    return (taskEntry.flags & taskFlags);
}

template <typename TaskListType>
auto GetTaskEntry(TaskListType& tasks, SdfPath const& uid)
{
    return std::find_if(tasks.begin(), tasks.end(),
        [&uid](typename TaskListType::value_type const& taskEntry)
        { return taskEntry.uid == uid; });
}

template <typename TaskListType>
auto GetTaskEntry(TaskListType& tasks, TfToken const& instanceName)
{
    return std::find_if(tasks.begin(), tasks.end(),
        [&instanceName](typename TaskListType::value_type const& taskEntry)
        { return taskEntry.uid.GetNameToken() == instanceName; });
}

template <typename TaskListType>
void RemoveTaskImpl(
    TaskListType& tasks, typename TaskListType::iterator& itTaskEntry, TaskDataContainer& container)
{
    if (itTaskEntry != tasks.end())
    {
        container.RemoveTask(itTaskEntry->uid);
        tasks.erase(itTaskEntry);
    }
}

template <class TaskListType>
void EnableTaskImpl(TaskListType& tasks, typename TaskListType::iterator& itTaskEntry, bool enable)
{
    if (itTaskEntry != tasks.end())
    {
        itTaskEntry->isEnabled = enable;
    }
}

template <class TaskListType, class TCommitFn>
void SetTaskCommitFnImpl(
    TaskListType& tasks, typename TaskListType::iterator& itTaskEntry, TCommitFn const& fnCommit)
{
    if (itTaskEntry != tasks.end())
    {
        itTaskEntry->fnCommit = fnCommit;
    }
}

///////////////////////////////////////////////////////////////////////////////
// TaskManager implementation

TaskManager::TaskManager(SdfPath const& uid, HdRenderIndex* renderIndex,
    std::shared_ptr<TaskDataContainer> container) :
    _uid(uid), _renderIndex(renderIndex), _container(std::move(container))
{
}

TaskManager::~TaskManager()
{
    for (TaskEntry const& taskEntry : _tasks)
    {
        _container->RemoveTask(taskEntry.uid);
    }
}

bool TaskManager::HasTask(SdfPath const& uid) const
{
    return GetTaskEntry(_tasks, uid) != _tasks.end();
}

bool TaskManager::HasTask(TfToken const& instanceName) const
{
    return GetTaskEntry(_tasks, instanceName) != _tasks.end();
}

void TaskManager::RemoveTask(SdfPath const& uid)
{
    TaskList::iterator it = GetTaskEntry(_tasks, uid);
    RemoveTaskImpl(_tasks, it, *_container);
}

void TaskManager::RemoveTask(TfToken const& instanceName)
{
    TaskList::iterator it = GetTaskEntry(_tasks, instanceName);
    RemoveTaskImpl(_tasks, it, *_container);
}

void TaskManager::EnableTask(SdfPath const& uid, bool enable)
{
    TaskList::iterator it = GetTaskEntry(_tasks, uid);
    EnableTaskImpl(_tasks, it, enable);
}

void TaskManager::EnableTask(TfToken const& instanceName, bool enable)
{
    TaskList::iterator it = GetTaskEntry(_tasks, instanceName);
    EnableTaskImpl(_tasks, it, enable);
}

void TaskManager::SetTaskCommitFn(TfToken const& taskName, CommitTaskFn const& fnCommit)
{
    TaskList::iterator it = GetTaskEntry(_tasks, taskName);
    SetTaskCommitFnImpl(_tasks, it, fnCommit);
}

void TaskManager::SetTaskCommitFn(SdfPath const& uid, CommitTaskFn const& fnCommit)
{
    TaskList::iterator it = GetTaskEntry(_tasks, uid);
    SetTaskCommitFnImpl(_tasks, it, fnCommit);
}

const SdfPath& TaskManager::_AddTask(TfToken const& taskName, CommitTaskFn const& fnCommit,
    SdfPath const& atPos, InsertionOrder order, TaskFlags taskFlags)
{
    const SdfPath taskId = BuildTaskPath(taskName);
    if (HasTask(taskId))
    {
        TF_CODING_ERROR(
            "Requested task %s already exists: %s", taskName.GetText(), taskId.GetText());
        return SdfPath::EmptyPath();
    }

    auto itInsert = _tasks.end();
    if (!atPos.IsEmpty() && order != InsertionOrder::insertAtEnd)
    {
        itInsert = GetTaskEntry(_tasks, atPos);
        if (itInsert == _tasks.end())
        {
            TF_CODING_ERROR("Insert point task does not exist: %s", atPos.GetAsString().c_str());
            return SdfPath::EmptyPath();
        }
    }

    auto it = _tasks.insert((order != InsertionOrder::insertAfter) ? itInsert : ++itInsert,
        { taskId, fnCommit, true, taskFlags });

    return it->uid;
}

void TaskManager::_InsertTask(SdfPath const& taskId, TaskInsertSpec& insertSpec)
{
    _container->Insert(taskId, insertSpec);
}

HdTaskSharedPtrVector TaskManager::CommitTaskValues(TaskFlags taskFlags)
{
    HdTaskSharedPtrVector enabledTasks;

    for (const auto& taskEntry : _tasks)
    {
        if (!taskEntry.isEnabled || !CheckTaskFlags(taskEntry, taskFlags))
        {
            continue;
        }

        if (taskEntry.fnCommit)
        {
            using namespace std::placeholders;
            auto fnGetValue = std::bind(&TaskManager::GetTaskValue, this, taskEntry.uid, _1);
            auto fnSetValue = std::bind(&TaskManager::SetTaskValue, this, taskEntry.uid, _1, _2);

            taskEntry.fnCommit(fnGetValue, fnSetValue);
        }

        enabledTasks.push_back(_renderIndex->GetTask(taskEntry.uid));
    }

    return enabledTasks;
}

void TaskManager::Execute(Engine* engine)
{
    HdTaskSharedPtrVector enabledTasks = CommitTaskValues(TaskFlagsBits::kExecutableBit);

    if (enabledTasks.empty())
    {
        return;
    }

    engine->Execute(_renderIndex, &enabledTasks);
}

VtValue TaskManager::GetTaskValue(SdfPath const& uid, TfToken const& key)
{
    if (uid.IsEmpty())
    {
        TF_CODING_ERROR("Task id cannot be empty.");
        return VtValue();
    }

    return _container->GetValue(uid, key);
}

bool TaskManager::SetTaskValue(SdfPath const& uid, TfToken const& key, VtValue const& newValue)
{
    if (uid.IsEmpty())
    {
        TF_CODING_ERROR("Task id cannot be empty.");
        return false;
    }

    return _container->SetValue(uid, key, newValue);
}

HdTaskSharedPtrVector const TaskManager::GetTasks(TaskFlags taskFlags) const
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdTaskSharedPtrVector filteredTasks;

    for (TaskEntry const& task : _tasks)
    {
        if (!CheckTaskFlags(task, taskFlags) || !task.isEnabled)
        {
            continue;
        }

        HdTaskSharedPtr pTask = _renderIndex->GetTask(task.uid);
        filteredTasks.push_back(pTask);
    }
    return filteredTasks;
}

HdTaskSharedPtr TaskManager::GetTask(SdfPath const& uid) const
{
    return _renderIndex->GetTask(uid);
}

SdfPath const& TaskManager::GetTaskPath(TfToken const& instanceName) const
{
    TaskList::const_iterator itExisting = GetTaskEntry(_tasks, instanceName);
    if (itExisting != _tasks.end())
    {
        return itExisting->uid;
    }

    return SdfPath::EmptyPath();
}

void TaskManager::GetTaskPaths(
    TaskFlags taskFlags, bool ignoreDisabled, SdfPathVector& outTaskPaths) const
{
    outTaskPaths.clear();
    for (TaskEntry const& task : _tasks)
    {
        if (CheckTaskFlags(task, taskFlags) && (task.isEnabled || !ignoreDisabled))
        {
            outTaskPaths.push_back(task.uid);
        }
    }
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
        // Only consider tasks that are actually executed during a normal
        // render (kExecutableBit). Tasks with other flags only, e.g.
        // picking-only tasks (kPickingTaskBit), are executed separately, 
        // so their convergence state is not relevant here.
        if (!CheckTaskFlags(task, TaskFlagsBits::kExecutableBit))
        {
            continue;
        }

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

} // namespace HVT_NS
