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

#include <hvt/engine/engine.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/trace/trace.h>
#include <pxr/imaging/hd/debugCodes.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/tokens.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

Engine::Engine() : _taskContext() {}

Engine::~Engine() = default;

void Engine::SetTaskContextData(TfToken const& id, VtValue const& data)
{
    // See if the token exists in the context and if not add it.
    std::pair<HdTaskContext::iterator, bool> result = _taskContext.emplace(id, data);
    if (!result.second)
    {
        // Item wasn't new, so need to update it
        result.first->second = data;
    }
}

bool Engine::GetTaskContextData(TfToken const& id, VtValue* data) const
{
    if (!TF_VERIFY(data, "data is nullptr"))
    {
        return false;
    }

    auto const& it = _taskContext.find(id);
    if (it != _taskContext.end())
    {
        *data = it->second;
        return true;
    }

    return false;
}

void Engine::RemoveTaskContextData(TfToken const& id)
{
    _taskContext.erase(id);
}

void Engine::ClearTaskContextData()
{
    _taskContext.clear();
}

void Engine::Execute(HdRenderIndex* index, HdTaskSharedPtrVector* tasks)
{
    TRACE_FUNCTION();

    if ((index == nullptr) || (tasks == nullptr))
    {
        TF_CODING_ERROR("Passed nullptr to Engine::Execute()");
        return;
    }

    // Some render tasks may need access to the same rendering context / driver
    // as the render delegate. For example some tasks use Hgi.
    _taskContext[HdTokens->drivers] = VtValue(index->GetDrivers());

    // --------------------------------------------------------------------- //
    // DATA DISCOVERY PHASE
    // --------------------------------------------------------------------- //
    // Discover all required input data needed to render the required render
    // prim representations. At this point, we must read enough data to
    // establish the resource dependency graph, but we do not yet populate CPU-
    // or GPU-memory with data.

    // As a result of the next call, the resource registry will be populated
    // with both BufferSources that need to be resolved (possibly generating
    // data on the CPU) and computations to run on the CPU/GPU.

    TF_DEBUG(HD_ENGINE_PHASE_INFO)
        .Msg("\n"
             "==============================================================\n"
             "  Engine [Data Discovery Phase](RenderIndex::SyncAll)         \n"
             "--------------------------------------------------------------\n");
    {
        TRACE_FUNCTION_SCOPE("Data Discovery");
        index->SyncAll(tasks, &_taskContext);
    }

    // --------------------------------------------------------------------- //
    // PREPARE PHASE
    // --------------------------------------------------------------------- //
    // Now that all Prims have obtained obtained their current states
    // we can now prepare the task system for rendering.
    //
    // While sync operations are change-tracked, so are only performed if
    // something is dirty, prepare operations are done for every execution.
    //
    // As tasks are synced first, they cannot resolve their bindings at sync
    // time, so this is where tasks perform their inter-prim communication.
    //
    // The prepare phase is also where a task manages the resources it needs
    // for the render phase.
    TF_DEBUG(HD_ENGINE_PHASE_INFO)
        .Msg("\n"
             "==============================================================\n"
             "  Engine [Prepare Phase](Task::Prepare)                       \n"
             "--------------------------------------------------------------\n");
    {
        TRACE_FUNCTION_SCOPE("Task Prepare");
        for (auto const& task : *tasks)
        {
            task->Prepare(&_taskContext, index);
        }
    }

    // --------------------------------------------------------------------- //
    // DATA COMMIT PHASE
    // --------------------------------------------------------------------- //
    // Having acquired handles to the data needed to update various resources,
    // we let the render delegate 'commit' these resources. These resources may
    // reside either on the CPU/GPU/both; that depends on the render delegate
    // implementation.
    TF_DEBUG(HD_ENGINE_PHASE_INFO)
        .Msg("\n"
             "==============================================================\n"
             "  Engine [Data Commit Phase](RenderDelegate::CommitResources) \n"
             "--------------------------------------------------------------\n");
    {
        TRACE_FUNCTION_SCOPE("Data Commit");
        HdRenderDelegate* renderDelegate = index->GetRenderDelegate();
        renderDelegate->CommitResources(&index->GetChangeTracker());
    }

    // --------------------------------------------------------------------- //
    // EXECUTE PHASE
    // --------------------------------------------------------------------- //
    // Having updated all the necessary data buffers, we can finally execute
    // the rendering tasks.
    TF_DEBUG(HD_ENGINE_PHASE_INFO)
    .Msg("\n"
         "==============================================================\n"
         "  Engine [Execute Phase](Task::Execute)                       \n"
         "--------------------------------------------------------------\n");
    {
        TRACE_FUNCTION_SCOPE("Task Execution");
        for (auto const& task : *tasks)
        {
            task->Execute(&_taskContext);
        }
    }
}

void Engine::Execute(HdRenderIndex* const index, SdfPathVector const& taskPaths)
{
    HdTaskSharedPtrVector tasks;
    tasks.reserve(taskPaths.size());
    for (SdfPath const& taskPath : taskPaths)
    {
        if (taskPath.IsEmpty())
        {
            TF_CODING_ERROR("Empty task path given to Engine::Execute()");
            continue;
        }
        HdTaskSharedPtr task = index->GetTask(taskPath);
        if (!task)
        {
            TF_CODING_ERROR(
                "No task at %s in render index in Engine::Execute()", taskPath.GetText());
            continue;
        }
        tasks.push_back(std::move(task));
    }
    Execute(index, &tasks);
}

bool Engine::AreTasksConverged(HdRenderIndex* const index, SdfPathVector const& taskPaths)
{
    for (SdfPath const& taskPath : taskPaths)
    {
        if (taskPath.IsEmpty())
        {
            TF_CODING_ERROR("Empty task path given to Engine::AreTasksConverged()");
            continue;
        }
        HdTaskSharedPtr const task = index->GetTask(taskPath);
        if (!task)
        {
            TF_CODING_ERROR(
                "No task at %s in render index in Engine::AreTasksConverged()", taskPath.GetText());
            continue;
        }
        if (!task->IsConverged())
        {
            return false;
        }
    }

    return true;
}

} // namespace HVT_NS
