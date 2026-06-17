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

#include <hvt/engine/sceneIndexMode.h>

#include <pxr/pxr.h>

#include <pxr/base/tf/getenv.h>

// legacyTaskSchema.h (and the SI task backend that consumes it) was introduced in USD 25.05
// (PXR_VERSION 2505) and does not exist before then (e.g. 24.11/25.02). When it is unavailable
// the SI backend cannot be built, so the switch is forced to SD.
#ifndef HVT_HAS_LEGACY_TASK_SCHEMA
#define HVT_HAS_LEGACY_TASK_SCHEMA (PXR_VERSION >= 2505)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

#if HVT_HAS_LEGACY_TASK_SCHEMA

namespace
{

// The mutable backend selection flag, initialized once from the environment.
bool& _UseSceneIndexFlag()
{
    static bool flag = TfGetenvBool("HVT_USE_SCENE_INDEX", /*default=*/true);
    return flag;
}

} // anonymous namespace

bool UseSceneIndex()
{
    return _UseSceneIndexFlag();
}

void SetUseSceneIndex(bool useSceneIndex)
{
    _UseSceneIndexFlag() = useSceneIndex;
}

#else

bool UseSceneIndex()
{
    // The scene-index backend is unavailable on this USD version; always use scene-delegate.
    return false;
}

void SetUseSceneIndex(bool /*useSceneIndex*/)
{
    // No-op: the scene-index backend cannot be built on this USD version.
}

#endif

} // namespace HVT_NS
