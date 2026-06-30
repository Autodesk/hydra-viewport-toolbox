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

#include <hvt/engine/taskDataContainer.h>

#include "taskContainerSDImpl.h"
#if HVT_HAS_LEGACY_TASK_SCHEMA
#include "taskContainerSIImpl.h"
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

std::shared_ptr<TaskDataContainer> MakeTaskContainerSD(
    HdRenderIndex* renderIndex, SyncDelegatePtr const& syncDelegate)
{
    return std::make_unique<TaskContainerSDImpl>(renderIndex, syncDelegate);
}

#if HVT_HAS_LEGACY_TASK_SCHEMA
std::shared_ptr<TaskDataContainer> MakeTaskContainerSI(
    HdRenderIndex* renderIndex, HdRetainedSceneIndexRefPtr const& retainedSceneIndex)
{
    return std::make_unique<TaskContainerSIImpl>(renderIndex, retainedSceneIndex);
}
#endif

} // namespace HVT_NS
