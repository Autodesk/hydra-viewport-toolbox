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

#include <hvt/tasks/emptyRenderTask.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/imaging/hd/perfLog.h>
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

EmptyRenderTask::EmptyRenderTask(HdSceneDelegate* /* delegate */, SdfPath const& uid) : HdxTask(uid)
{
}

EmptyRenderTask::~EmptyRenderTask() = default;

TfToken const& EmptyRenderTask::GetToken()
{
    static TfToken const token { "emptyRenderTask", TfToken::Immortal };
    return token;
}

void EmptyRenderTask::_Sync(
    HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        EmptyRenderTaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            _params = params;
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void EmptyRenderTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
}

void EmptyRenderTask::Execute(HdTaskContext* /* ctx */)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
}

std::ostream& operator<<(std::ostream& out, EmptyRenderTaskParams const& /* pv */)
{
    out << "EmptyRenderTaskParams";
    return out;
}

bool operator==(EmptyRenderTaskParams const& /* lhs */, EmptyRenderTaskParams const& /* rhs */)
{
    return true;
}

bool operator!=(EmptyRenderTaskParams const& lhs, EmptyRenderTaskParams const& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
