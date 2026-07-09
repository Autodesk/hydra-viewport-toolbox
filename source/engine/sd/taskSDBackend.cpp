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

#include "taskSDBackend.h"

#include <ostream>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hd/changeTracker.h>
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

TaskSDBackend::TaskSDBackend(
    HdRenderIndex* renderIndex, SdfPath const& uid) :
    _renderIndex(renderIndex), _syncDelegate(std::make_shared<SyncDelegate>(uid, renderIndex))
{
}

TaskSDBackend::~TaskSDBackend()
{
    _syncDelegate = nullptr;
}

void TaskSDBackend::Uninitialize(HdRenderIndex& /*renderIndex*/) {}

void TaskSDBackend::CreateTask(SdfPath const& taskId, TaskCreateInfo const& spec)
{
    // Insert the task into the render index through the scene delegate (type-erased per task type
    // T by TaskCreateInfo::sdCreate, which calls HdRenderIndex::InsertTask<T>).
    spec.sdCreate(_renderIndex, _syncDelegate.get(), taskId);

    // Store the initial task parameters in the scene delegate.
    _syncDelegate->SetValue(taskId, HdTokens->params, spec.params);
}

void TaskSDBackend::RemoveTask(SdfPath const& taskId)
{
    _renderIndex->RemoveTask(taskId);
}

VtValue TaskSDBackend::GetValue(SdfPath const& taskId, TfToken const& key)
{
    return _syncDelegate->GetValue(taskId, key);
}

bool TaskSDBackend::SetValue(SdfPath const& taskId, TfToken const& key, VtValue const& newValue)
{
    // If the sync delegate already has a value, and the value is unchanged, return early.
    // DESIGN NOTE: See OGSMOD-6765
    // We have to make SURE that all task parameters operator==(lhs, rhs) are fully implemented for
    // this update strategy to work, since there is now no way to FORCE a parameter update and dirty
    // flag.
    if (const VtValue* previousValue = _syncDelegate->GetValuePtr(taskId, key))
    {
        if (newValue == (*previousValue))
        {
            return true;
        }
    }

    // Set the value on the sync delegate.
    _syncDelegate->SetValue(taskId, key, newValue);

    // Set the appropriate task dirty bit based on the key value, and mark the task dirty on the
    // render index.
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
        _renderIndex->GetChangeTracker().MarkTaskDirty(taskId, dirtyBits);
    }

    return true;
}

void TaskSDBackend::PrintTaskData(std::ostream& out, SdfPath const& /*rootPath*/) const
{
    if (_syncDelegate)
    {
        out << *_syncDelegate;
    }
}

void TaskSDBackend::MarkTaskParamsDirty(SdfPathVector const& taskPaths)
{
    for (SdfPath const& taskPath : taskPaths)
    {
        _renderIndex->GetChangeTracker().MarkTaskDirty(taskPath, HdChangeTracker::DirtyParams);
    }
}

} // namespace HVT_NS
