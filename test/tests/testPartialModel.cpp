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

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include "composeTaskHelpers.h"

#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

#include <pxr/pxr.h>

PXR_NAMESPACE_USING_DIRECTIVE

//
// Unit tests to display only part of a model using HdRprimCollection.
//
// These tests demonstrate how to:
// - Render only a subset of prims by setting a root path (include)
// - Exclude specific prims from rendering (exclude)
//

namespace
{

// Default paths for test prims.
const SdfPath gizmosPath("/gizmos");
const SdfPath gridPath = gizmosPath.AppendChild(TfToken("grid"));

/// Helper function to create and run a partial model rendering test using collection filtering.
/// \param context The test context.
/// \param stage The test stage containing the model.
/// \param collection The HdRprimCollection specifying which prims to render.
void RunPartialModelTest(std::shared_ptr<TestHelpers::TestContext> const& context,
    TestHelpers::TestStage& stage, HdRprimCollection const& collection)
{
    // Add a grid as an additional geometry to the scene.
    hvt::ViewportEngine::CreateGrid(stage.stage(), gridPath, GfVec3d(0.0, 0.0, 0.0), true);

    // Create the frame pass instance using the helper.
    auto framePass = TestHelpers::FramePassInstance::CreateInstance(stage.stage(), context->_backend);

    // Render multiple frames to ensure convergence.
    int frameCount = 10;

    auto render = [&]()
    {
        // Set the collection to filter which prims are rendered.
        framePass.sceneFramePass->params().collection = collection;

        TestHelpers::RenderSecondFramePass(framePass, context->width(), context->height(),
            context->presentationEnabled(), stage, {}, true, TestHelpers::ColorDarkGrey, false);

        return --frameCount > 0;
    };

    // Run the render loop.
    context->run(render, framePass.sceneFramePass.get());
}

} // anonymous namespace

// Test: Render only the model, excluding the grid.
// This demonstrates using SetExcludePaths to hide specific geometry.
HVT_TEST(TestViewportToolbox, excludeGrid)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Create a collection that excludes the grid.
    // This will render everything except the grid prims.
    HdRprimCollection collection { hvt::FramePassParams().collection };
    collection.SetExcludePaths({ gridPath });

    RunPartialModelTest(context, stage, collection);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Test: Render only the grid, excluding everything else.
// This demonstrates using SetRootPath to include only specific geometry.
HVT_TEST(TestViewportToolbox, includeOnlyGrid)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Create a collection that only includes the grid.
    // This will render only the grid prims and nothing else.
    HdRprimCollection collection { hvt::FramePassParams().collection };
    collection.SetRootPath(gridPath);

    RunPartialModelTest(context, stage, collection);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Test: Render only the gizmos hierarchy (grids), excluding the main model.
// This demonstrates rendering a specific subtree of the scene graph.
HVT_TEST(TestViewportToolbox, includeOnlyGizmos)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Create a collection that only includes the gizmos hierarchy.
    // This renders only the prims under /gizmos, which includes the grid we added.
    HdRprimCollection collection { hvt::FramePassParams().collection };
    collection.SetRootPath(gizmosPath);

    RunPartialModelTest(context, stage, collection);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Test: Verify that multiple exclude paths work correctly.
// This demonstrates excluding multiple subtrees simultaneously.
HVT_TEST(TestViewportToolbox, excludeMultiplePaths)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Create a second auxiliary geometry (another grid at a different location).
    // Position the second grid further away and offset vertically to clearly see both grids.
    const SdfPath secondGridPath("/gizmos/grid2");
    hvt::ViewportEngine::CreateGrid(stage.stage(), secondGridPath, GfVec3d(5.0, 2.0, 0.0), true);

    // Create a collection that excludes both grids.
    // This demonstrates that multiple paths can be excluded at once.
    HdRprimCollection collection { hvt::FramePassParams().collection };
    collection.SetExcludePaths({ gridPath, secondGridPath });

    RunPartialModelTest(context, stage, collection);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}
