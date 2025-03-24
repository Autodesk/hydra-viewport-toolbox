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

#include <hvt/tasks/resources.h>

#include "source/utils/pathUtils.h"

namespace hvt
{

namespace
{

// The resource root path.
std::filesystem::path sResourceDir = "";

} //anonymous namespace

void SetResourceDirectory(std::filesystem::path const& resourceDir)
{
    sResourceDir = resourceDir;
}

const std::filesystem::path GetResourceDirectory()
{
    if (sResourceDir.empty())
    {
        sResourceDir = hvt::GetDefaultResourceDirectory();
    }

    return sResourceDir;
}

const std::filesystem::path GetGizmoPath(std::string const& gizmoFile)
{
    auto resourceDir = GetResourceDirectory();
    return resourceDir.append("Gizmos").append(gizmoFile);
}

const std::filesystem::path GetShaderPath(std::string const& shaderFile)
{
    auto resourceDir = GetResourceDirectory();
    return resourceDir.append("Shaders").append(shaderFile);
}

} // namespace hvt
