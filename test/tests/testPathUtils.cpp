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

#include <hvt/tasks/resources.h>

#include <gtest/gtest.h>

#include <filesystem>

// ===========================================================================
// GetDefaultResourceDirectory / GetDefaultMaterialXDirectory
// ===========================================================================

TEST(TestPathUtils, GetDefaultResourceDirectory_IsNotEmpty)
{
    const auto& dir = hvt::GetResourceDirectory();
    EXPECT_FALSE(dir.empty());
}

TEST(TestPathUtils, GetDefaultResourceDirectory_IsAbsolute)
{
    const auto& dir = hvt::GetResourceDirectory();
    EXPECT_TRUE(dir.is_absolute());
}

TEST(TestPathUtils, SetResourceDirectory_OverridesDefault)
{
    const auto saved = hvt::GetResourceDirectory();

    const std::filesystem::path custom("/tmp/custom_resources");
    hvt::SetResourceDirectory(custom);
    EXPECT_EQ(hvt::GetResourceDirectory(), custom);

    hvt::SetResourceDirectory(saved);
    EXPECT_EQ(hvt::GetResourceDirectory(), saved);
}

TEST(TestPathUtils, GetGizmoPath_AppendsCorrectly)
{
    const auto saved = hvt::GetResourceDirectory();

    const std::filesystem::path base("/tmp/res");
    hvt::SetResourceDirectory(base);

    auto gizmoPath = hvt::GetGizmoPath("myGizmo.usda");
    EXPECT_EQ(gizmoPath, std::filesystem::path("/tmp/res/gizmos/myGizmo.usda"));

    hvt::SetResourceDirectory(saved);
}

TEST(TestPathUtils, GetShaderPath_AppendsCorrectly)
{
    const auto saved = hvt::GetResourceDirectory();

    const std::filesystem::path base("/tmp/res");
    hvt::SetResourceDirectory(base);

    auto shaderPath = hvt::GetShaderPath("blur.glslfx");
    EXPECT_EQ(shaderPath, std::filesystem::path("/tmp/res/shaders/blur.glslfx"));

    hvt::SetResourceDirectory(saved);
}
