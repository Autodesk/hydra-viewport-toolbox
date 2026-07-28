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

#include <hvt/engine/taskBackend.h>

#include <pxr/pxr.h>

#include <pxr/base/tf/getenv.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

#if HVT_SI_TASK_BACKEND_SUPPORTED

namespace
{

// The mutable backend selection flag, initialized once from the environment.
bool& _UseLegacySceneDelegateFlag()
{
    static bool flag = TfGetenvBool("HVT_USE_LEGACY_SCENE_DELEGATE", /*default=*/false);
    return flag;
}

} // anonymous namespace

bool UseLegacySceneDelegate()
{
    return _UseLegacySceneDelegateFlag();
}

void SetUseLegacySceneDelegate(bool useLegacySceneDelegate)
{
    _UseLegacySceneDelegateFlag() = useLegacySceneDelegate;
}

#else

bool UseLegacySceneDelegate()
{
    // The scene-index backend is unavailable on this USD version; always use scene-delegate.
    return true;
}

void SetUseLegacySceneDelegate(bool /*useLegacySceneDelegate*/)
{
    // No-op: the scene-index backend cannot be built on this USD version.
}

#endif

} // namespace HVT_NS
