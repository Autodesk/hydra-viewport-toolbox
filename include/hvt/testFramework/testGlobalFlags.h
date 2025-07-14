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

namespace HVT_NS
{

namespace TestFramework
{

/// Whether the unit tests are running using the Vulkan backend.
HVT_API extern bool isRunningVulkan();
/// Imposes to run suing the Vulkan backend.
HVT_API extern void enableRunningVulkan( bool enable);

} // namespace TestFramework

} // namespace HVT_NS