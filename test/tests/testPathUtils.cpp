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

#include <RenderingFramework/TestHelpers.h>

#include <filesystem>
#include <fstream>

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

TEST(TestPathUtils, EmscriptenDefaultResourceDirectory)
{
    // SetResourceDirectory has been called by other tests; always reset to default.
    hvt::SetResourceDirectory({});

    // Whatever is the platform, the returned path must always be an absolute path.
    const auto& dir = hvt::GetResourceDirectory();
    EXPECT_TRUE(dir.is_absolute());

#if defined(__EMSCRIPTEN__)
    EXPECT_EQ(dir, std::filesystem::path("/Resources"));
#endif
}

#if defined(__EMSCRIPTEN__)

TEST(TestPathUtils, EmscriptenDefaultIsCwdIndependent)
{
    // Preserve the current path.
    struct CwdGuard
    {
        CwdGuard() { currentPath = std::filesystem::current_path(); }
        ~CwdGuard() { std::filesystem::current_path(currentPath); }
        std::filesystem::path currentPath;
    } guard;

    // Verify the default resource directory does not change when cwd changes.
    hvt::SetResourceDirectory({});
    const auto before = hvt::GetResourceDirectory();

    std::filesystem::current_path("/tmp");
    hvt::SetResourceDirectory({});

    const auto after = hvt::GetResourceDirectory();
    EXPECT_EQ(before, after);

    // Must remain the same whatever is the current path.
    EXPECT_EQ(after, std::filesystem::path("/Resources"));
}

#endif

namespace
{

void writeEmptyFile(std::filesystem::path const& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    ASSERT_TRUE(file.is_open());
}

/// Restores the data-root lists after tests that call \c SetTestDataRoot / \c AddTestDataRoot.
struct RestoredTestDataRoots
{
    RestoredTestDataRoots()
        : _root(TestHelpers::getAssetsDataFolder().parent_path().parent_path())
    {
    }
    ~RestoredTestDataRoots() { TestHelpers::SetTestDataRoot(_root); }

    std::filesystem::path _root;
};

} // namespace

TEST(TestPathUtils, ResolveAssetPath_SearchesMultipleRoots)
{
    RestoredTestDataRoots restore;

    const auto tempBase =
        std::filesystem::temp_directory_path() / "hvt_test_resolve_asset_path";
    const auto root1 = tempBase / "root1";
    const auto root2 = tempBase / "root2";

    writeEmptyFile(root1 / "data" / "assets" / "in_first.txt");
    writeEmptyFile(root2 / "data" / "assets" / "in_second.txt");

    TestHelpers::SetTestDataRoot(root1);
    TestHelpers::AddTestDataRoot(root2);

    const auto first = TestHelpers::ResolveAssetPath("in_first.txt");
    const auto second = TestHelpers::ResolveAssetPath("in_second.txt");

    EXPECT_TRUE(std::filesystem::exists(first));
    EXPECT_TRUE(std::filesystem::exists(second));
    EXPECT_EQ(first, root1 / "data" / "assets" / "in_first.txt");
    EXPECT_EQ(second, root2 / "data" / "assets" / "in_second.txt");

    std::filesystem::remove_all(tempBase);
}

TEST(TestPathUtils, ResolveBaselinePath_UsesPlatformBaselineNaming)
{
    RestoredTestDataRoots restore;

    const auto tempBase =
        std::filesystem::temp_directory_path() / "hvt_test_resolve_baseline_path";
    const auto root = tempBase / "baseline_root";
    TestHelpers::SetTestDataRoot(root);

    const std::string logicalName = "hvt_resolve_baseline_test";
    const std::string onDisk = TestHelpers::HydraRendererContext::getFilename(
        root / "data" / "baselines", logicalName);
    writeEmptyFile(std::filesystem::path(onDisk));

    const auto resolved = TestHelpers::ResolveBaselinePath(logicalName);
    EXPECT_EQ(resolved, std::filesystem::path(onDisk));
    EXPECT_TRUE(std::filesystem::exists(resolved));

    std::filesystem::remove_all(tempBase);
}
