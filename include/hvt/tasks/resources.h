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
#pragma once

#include <hvt/api.h>

#include <filesystem>

namespace hvt
{

/// Set the resource directory path.
/// \note It contains a default directory if not set.
HVT_API extern void SetResourceDirectory(std::filesystem::path const& resourceDir);

/// Get the resource directory path.
/// \note That's the root directory of the 'shader', 'gizmos', etc. files i.e., the resources.
HVT_API extern const std::filesystem::path GetResourceDirectory();

/// Returns the gizmos file path.
HVT_API extern const std::filesystem::path GetGizmoPath(std::string const& gizmoFile);

/// Returns the shader file path.
HVT_API extern const std::filesystem::path GetShaderPath(std::string const& shaderFile);

} // namespace hvt