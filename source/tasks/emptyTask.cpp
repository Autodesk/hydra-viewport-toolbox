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

#include <hvt/tasks/emptyTask.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hf/perfLog.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

EmptyTask::EmptyTask(HdSceneDelegate* /* delegate */, SdfPath const& uid) : HdxTask(uid) {}

void EmptyTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (*dirtyBits & HdChangeTracker::DirtyParams)
    {
        EmptyTaskParams params;
        if (!_GetTaskParams(delegate, &params))
        {
            return;
        }

        if (_params != params)
        {
            _params = params;
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void EmptyTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
}

void EmptyTask::Execute(HdTaskContext* /* ctx */)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
}

const TfToken& EmptyTask::GetToken()
{
    static const TfToken token { "emptyTask", TfToken::Immortal };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, EmptyTaskParams const& /* pv */)
{
    out << "EmptyTask Params: (default)";
    return out;
}

bool operator==(EmptyTaskParams const& /* lhs */, EmptyTaskParams const& /* rhs */)
{
    return true;
}

bool operator!=(EmptyTaskParams const& lhs, EmptyTaskParams const& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
