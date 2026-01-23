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
#include <hvt/tasks/blurTask.h>

#include <pxr/imaging/hd/mergingSceneIndex.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <gtest/gtest.h>

#include <pxr/pxr.h>

PXR_NAMESPACE_USING_DIRECTIVE

//
// Unit tests for partial display of a model.
//
// These tests demonstrate how to display only part of a rendered model
// using CameraUtilFraming.
//
// CameraUtilFraming consists of:
// - Data window: The region of the render buffer containing the rendered content.
//                Typically set to the full render buffer size.
// - Display window: The region on screen where the content will be shown.
//                   This determines what portion of the model is visible and where.
// - Pixel aspect ratio: Usually 1.0 for square pixels.
//
// By setting a smaller display window than the data window, you can clip
// the rendered content to show only a portion of the model.
//

namespace
{

/// Helper function to render the model with a specific framing configuration.
/// \param context The test context.
/// \param stage The test stage containing the model.
/// \param framing The CameraUtilFraming defining data window and display window.
void RunPartialDisplayTest(std::shared_ptr<TestHelpers::TestContext> const& context,
    TestHelpers::TestStage& stage, CameraUtilFraming const& framing)
{
    // Create the frame pass instance using the helper.
    auto framePass = TestHelpers::FramePassInstance::CreateInstance(stage.stage(), context->_backend);

    // Render multiple frames to ensure convergence.
    int frameCount = 10;

    auto render = [&]()
    {
        hvt::FramePassParams& params = framePass.sceneFramePass->params();

        params.renderBufferSize = GfVec2i(context->width(), context->height());
        params.viewInfo.framing = framing;

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace         = HdxColorCorrectionTokens->disabled;
        params.backgroundColor    = TestHelpers::ColorDarkGrey;
        params.selectionColor     = TestHelpers::ColorYellow;
        params.enablePresentation = context->presentationEnabled();

        framePass.sceneFramePass->Render();

        return --frameCount > 0;
    };

    // Run the render loop.
    context->run(render, framePass.sceneFramePass.get());
}

} // anonymous namespace

// Test: Display only the left half of the model.
// This demonstrates using framing to clip the right portion of the rendered model.
HVT_TEST(TestViewportToolbox, displayLeftHalf)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    const auto width = context->width();
    const auto height = context->height();

    // Create framing: full data window, left half display window.
    const CameraUtilFraming framing = {
        { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
        { { 0, 0 }, { width / 2, height } },
        1.0f
    };

    RunPartialDisplayTest(context, stage, framing);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Test: Display only the right half of the model.
// This demonstrates using framing to clip the left portion of the rendered model.
HVT_TEST(TestViewportToolbox, displayRightHalf)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    const auto width = context->width();
    const auto height = context->height();

    // Create framing: full data window, right half display window.
    const CameraUtilFraming framing = {
        { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
        { { width / 2, 0 }, { width, height } },
        1.0f
    };

    RunPartialDisplayTest(context, stage, framing);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Test: Display only the top half of the model.
// This demonstrates using framing to clip the bottom portion of the rendered model.
HVT_TEST(TestViewportToolbox, displayTopHalf)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    const auto width = context->width();
    const auto height = context->height();

    // Create framing: full data window, top half display window.
    const CameraUtilFraming framing = {
        { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
        { { 0, 0 }, { width, height / 2 } },
        1.0f
    };

    RunPartialDisplayTest(context, stage, framing);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Test: Display only the center quarter of the model with offset.
// This demonstrates a more complex clipping scenario.
HVT_TEST(TestViewportToolbox, displayCenterQuarter)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    const auto width = context->width();
    const auto height = context->height();

    // Display a quarter-size window offset from center.
    const int quarterWidth = width / 4;
    const int quarterHeight = height / 4;
    const int offsetX = width / 3;
    const int offsetY = height / 3;

    // Create framing: full data window, center quarter display window.
    const CameraUtilFraming framing = {
        { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
        { { offsetX, offsetY }, { offsetX + quarterWidth, offsetY + quarterHeight } },
        1.0f
    };

    RunPartialDisplayTest(context, stage, framing);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

/// Helper function to run two frame passes test with different displays.
/// First frame pass displays with blur effect, second displays center quarter.
/// \param context The test context.
/// \param stage The test stage.
/// \param clearBackgroundColor Whether to clear the background color in the second frame pass.
void RunTwoFramePassesTest(std::shared_ptr<TestHelpers::TestContext> const& context,
    TestHelpers::TestStage& stage, bool clearBackgroundColor)
{
    auto filepath =
        (TestHelpers::getAssetsDataFolder() / "usd" / "default_scene.usdz").generic_u8string();

    TestHelpers::FramePassInstance framePass1, framePass2;
    HdMergingSceneIndexRefPtr mergingSceneIndex;

    // Create two scene indices and merge them.

    {
        auto sceneStage1 = hvt::ViewportEngine::CreateStageFromFile(filepath);
        auto sceneIndex1 = hvt::ViewportEngine::CreateUSDSceneIndex(sceneStage1);

        auto sceneStage2 = hvt::ViewportEngine::CreateStageFromFile(context->_sceneFilepath);
        {
            // Get the root prim from scene stage.
            UsdPrim rootPrim = sceneStage2->GetPrimAtPath(SdfPath("/mesh_0"));

            // Add a zoom (scale transform) to the root prim.
            UsdGeomXformable xformable(rootPrim);
            if (xformable)
            {
                static constexpr double zoomFactor = 20.0;
                GfVec3d scale(zoomFactor, zoomFactor, zoomFactor);

                UsdGeomXformOp scaleOp = xformable.AddScaleOp(UsdGeomXformOp::PrecisionDouble);
                scaleOp.Set(scale);
            }
        }
        auto sceneIndex2 = hvt::ViewportEngine::CreateUSDSceneIndex(sceneStage2);

        // Merge the scene indices.
        mergingSceneIndex = HdMergingSceneIndex::New();
        mergingSceneIndex->AddInputScene(sceneIndex1, SdfPath::AbsoluteRootPath());
        mergingSceneIndex->AddInputScene(sceneIndex2, SdfPath::AbsoluteRootPath());
    }

    // Create the first frame pass instance.

    {
        // Create the render index with the Storm render delegate.
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass1.renderIndex, renderDesc);

        framePass1.sceneIndex = mergingSceneIndex;

        framePass1.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass1.sceneIndex, SdfPath::AbsoluteRootPath());

        // Create the first frame pass instance.
        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex      = framePass1.renderIndex->RenderIndex();
        passDesc.uid              = SdfPath("/sceneFramePass1");
        framePass1.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);

        // Add a strong blur effect to the first frame pass.

        static constexpr float blurValue = 8.0f;
        hvt::TaskManagerPtr& taskManager = framePass1.sceneFramePass->GetTaskManager();

        auto fnCommit = [](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                           hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const VtValue value        = fnGetValue(HdTokens->params);
            hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
            params.blurAmount          = blurValue;
            fnSetValue(HdTokens->params, VtValue(params));
        };

        const SdfPath& insertPos = taskManager->GetTaskPath(HdxPrimitiveTokens->presentTask);
        taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), hvt::BlurTaskParams(),
            fnCommit, insertPos, hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Create the second frame pass instance.

    {
        // Create the render index with the Storm render delegate.
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass2.renderIndex, renderDesc);

        framePass2.sceneIndex = mergingSceneIndex;

        framePass2.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass2.sceneIndex, SdfPath::AbsoluteRootPath());

        // Create the second frame pass instance.
        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex      = framePass2.renderIndex->RenderIndex();
        passDesc.uid              = SdfPath("/sceneFramePass2");
        framePass2.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    const auto width  = context->width();
    const auto height = context->height();

    // Render loop.
    int frameCount = 10;

    auto render = [&]()
    {
        // First frame pass: full display with blur effect.

        {
            TestHelpers::RenderFirstFramePass(framePass1, width, height, stage);

            context->_backend->waitForGPUIdle();
        }

        // Second frame pass: display center quarter only.
        // Note: Cannot use RenderSecondFramePass as it overwrites the framing.

        {
            // Define center quarter framing.
            const int quarterWidth  = width / 4;
            const int quarterHeight = height / 4;
            const int offsetX       = width / 3;
            const int offsetY       = height / 3;

            auto& pass   = framePass2.sceneFramePass;
            auto& params = pass->params();

            params.renderBufferSize = GfVec2i(width, height);
            params.viewInfo.framing = {
                { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
                { { offsetX, offsetY }, { offsetX + quarterWidth, offsetY + quarterHeight } },
                1.0f
            };

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace     = HdxColorCorrectionTokens->disabled;
            params.selectionColor = TestHelpers::ColorYellow;

            params.clearBackgroundColor = clearBackgroundColor;
            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;

            params.clearBackgroundDepth = false;
            params.backgroundDepth      = 1.0f;

            params.enablePresentation = context->presentationEnabled();

            // Get color & depth from the first frame pass to share the buffers.
            hvt::RenderBufferBindings inputAOVs =
                framePass1.sceneFramePass->GetRenderBufferBindingsForNextPass(
                    { HdAovTokens->color, HdAovTokens->depth });

            // Get render tasks with input AOVs and render.
            const HdTaskSharedPtrVector renderTasks = pass->GetRenderTasks(inputAOVs);
            pass->Render(renderTasks);

            context->_backend->waitForGPUIdle();
        }

        return --frameCount > 0;
    };

    context->run(render, framePass2.sceneFramePass.get());
}

// Test: Two frame passes with clearBackgroundColor = false.
// The second frame pass preserves the blurred background from the first pass.
HVT_TEST(TestViewportToolbox, TestFramePasses_WithDifferentDisplays_KeepBackground)
{
    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);

    auto filepath =
        (TestHelpers::getAssetsDataFolder() / "usd" / "default_scene.usdz").generic_u8string();
    ASSERT_TRUE(stage.open(filepath));

    RunTwoFramePassesTest(context, stage, false);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Test: Two frame passes with clearBackgroundColor = true.
// The second frame pass clears the background, discarding the blur effect.
HVT_TEST(TestViewportToolbox, TestFramePasses_WithDifferentDisplays_ClearBackground)
{
    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);

    auto filepath =
        (TestHelpers::getAssetsDataFolder() / "usd" / "default_scene.usdz").generic_u8string();
    ASSERT_TRUE(stage.open(filepath));

    RunTwoFramePassesTest(context, stage, true);

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}
