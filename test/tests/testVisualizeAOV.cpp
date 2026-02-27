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

#include "composeTaskHelpers.h"
#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/framePassUtils.h>
#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

#include <pxr/pxr.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

void TestDisplayAOV(std::shared_ptr<TestHelpers::TestContext>& context, pxr::TfToken const& aovToken)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    TestHelpers::TestStage stage(context->_backend);

    // Use a dedicated scene with three rectangles at different depths for better depth visualization.
    auto filepath =
        (TestHelpers::getAssetsDataFolder() / "usd" / "depth_test_rectangles.usda").generic_u8string();
    ASSERT_TRUE(stage.open(filepath));

    // Defines a frame pass.

    auto framePass = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend);

    // Render 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        // Display the AOV buffer.
        auto& params        = framePass.sceneFramePass->params();
        params.visualizeAOV = aovToken;

        TestHelpers::RenderSecondFramePass(framePass, context->width(), context->height(),
            context->presentationEnabled(), stage, {}, true, TestHelpers::ColorDarkGrey, true);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass.sceneFramePass.get());
}

} // namespace

HVT_TEST(TestViewportToolbox, display_color_AOV)
{
    // This unit test validates the display of the color AOV buffer.

    auto context = TestHelpers::CreateTestContext();

    // Display the color AOV buffer.

    TestDisplayAOV(context, pxr::HdAovTokens->color);

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

HVT_TEST(TestViewportToolbox, display_depth_AOV)
{
    // This unit test validates the display of the depth AOV buffer.
    // Uses a scene with three rectangles at different depths to clearly show depth variation.

    auto context = TestHelpers::CreateTestContext();

    // Display the depth AOV buffer.

    TestDisplayAOV(context, pxr::HdAovTokens->depth);

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

HVT_TEST(TestViewportToolbox, display_Neye_AOV)
{
    // This unit test validates the display of the eye-space normal (Neye) AOV buffer.

    auto context = TestHelpers::CreateTestContext();

    // Display the Neye AOV buffer.

    TestDisplayAOV(context, pxr::HdAovTokens->Neye);

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Disabled on macOS/Metal: primId values are non-deterministic between runs.
#if defined(__APPLE__)
HVT_TEST(TestViewportToolbox, DISABLED_display_primId_AOV)
#else
HVT_TEST(TestViewportToolbox, display_primId_AOV)
#endif
{
    // This unit test validates the display of the primitive ID (primId) AOV buffer.

    auto context = TestHelpers::CreateTestContext();

    // Display the primId AOV buffer.

    TestDisplayAOV(context, pxr::HdAovTokens->primId);

    // Validate the rendering result.

    uint8_t pixelValueThreshold  = 1;
    uint16_t pixelCountThreshold = 1;

#if defined(ENABLE_VULKAN) && defined(WIN32)
    if (GetParam() == HgiTokens->Vulkan)
    {
        // There are some differences between the Windows/OpenGL & Windows/Vulkan generated images
        // but the result remains valid.
        pixelValueThreshold = 1;
        pixelCountThreshold = 400;
    }
#endif

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName, 
        pixelValueThreshold, pixelCountThreshold));
}

HVT_TEST(TestViewportToolbox, display_Neye_AOV_withTwoSceneIndices)
{
    // This unit test validates a way to display the Neye AOV buffer when using two different scenes.

    // Note: This test mainly validates that the AOV buffer is displayed correctly when using two different 
    // scenes. It highlights the need to output the depth buffer when visualizing the Neye & primId AOV 
    // buffers.

    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);

    auto filepath = 
        (TestHelpers::getAssetsDataFolder() / "usd" / "default_scene.usdz").generic_u8string();

    // Note: Because of some limitation of the Unit Test Framework, the scene stage must also be 
    // created here as it used by the framework to get the view and projection matrices.
    ASSERT_TRUE(stage.open(filepath));

    // Defines a frame pass.

    TestHelpers::FramePassInstance framePass;

    {
        // Creates the render index with the Storm render delegate.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass.renderIndex, renderDesc);

        // Creates the two scene indices and merges them.

        auto sceneStage1 = hvt::ViewportEngine::CreateStageFromFile(filepath);
        auto sceneIndex1 = hvt::ViewportEngine::CreateUSDSceneIndex(sceneStage1);
    
        auto sceneStage2 = hvt::ViewportEngine::CreateStageFromFile(context->_sceneFilepath);
        {
            // Get the root prim from scene stage.
            UsdPrim rootPrim = sceneStage2->GetPrimAtPath(SdfPath("/mesh_0"));

            // Add a zoom (scale transform) to the root prim.
            UsdGeomXformable xformable(rootPrim);
            if (xformable) {
                // Create a scale transform for zoom.
                static constexpr double zoomFactor = 20.0;
                GfVec3d scale(zoomFactor, zoomFactor, zoomFactor);
                
                // Get or create the xformOp for scale.
                UsdGeomXformOp scaleOp = xformable.AddScaleOp(UsdGeomXformOp::PrecisionDouble);
                scaleOp.Set(scale);
            }
        }
        auto sceneIndex2 = hvt::ViewportEngine::CreateUSDSceneIndex(sceneStage2);

        // Merges the scene indices.

        HdMergingSceneIndexRefPtr mergingSceneIndex = HdMergingSceneIndex::New();
        mergingSceneIndex->AddInputScene(sceneIndex1, SdfPath::AbsoluteRootPath());
        mergingSceneIndex->AddInputScene(sceneIndex2, SdfPath::AbsoluteRootPath());
        framePass.sceneIndex = mergingSceneIndex;

        framePass.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass.sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex     = framePass.renderIndex->RenderIndex();
        passDesc.uid             = SdfPath("/sceneFramePass");
        framePass.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Render 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        // Display the Neye AOV buffer.
        auto& params        = framePass.sceneFramePass->params();
        params.visualizeAOV = pxr::HdAovTokens->Neye;

        TestHelpers::RenderSecondFramePass(framePass, context->width(), context->height(),
            context->presentationEnabled(), stage, {}, true, TestHelpers::ColorDarkGrey, true);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

HVT_TEST(TestViewportToolbox, display_color_AOV_with_switches)
{
    // This unit test validates the display of the color AOV buffer with several
    // changes before the end result.
    
    // Note: Some AOVs need a color-like GPU texture to render and others do not
    // as their buffer is float & vec4. So, the unit validates that the extra buffer
    // is correctly managed i.e., created/deleted/etc.

    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);

    // Use a dedicated scene with three rectangles at different depths for better visualization.
    auto filepath =
        (TestHelpers::getAssetsDataFolder() / "usd" / "depth_test_rectangles.usda").generic_u8string();
    ASSERT_TRUE(stage.open(filepath));

    // Defines a frame pass.

    auto framePass = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend);

    static constexpr int MaxRuns { 10 };

    // Arbitrary order but ends with color.
    static const pxr::TfToken aovs[MaxRuns]
    {
        pxr::HdAovTokens->color, pxr::HdAovTokens->color, pxr::HdAovTokens->Neye,
        pxr::HdAovTokens->depth, pxr::HdAovTokens->color, pxr::HdAovTokens->Neye,
        pxr::HdAovTokens->color, HdAovTokens->Neye, pxr::HdAovTokens->depth,
        pxr::HdAovTokens->color
    };

    // Render 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = MaxRuns;

    auto render = [&]()
    {
        // Display an arbitrary AOV buffer.
        auto& params        = framePass.sceneFramePass->params();
        params.visualizeAOV = aovs[frameCount-1];

        TestHelpers::RenderSecondFramePass(framePass, context->width(), context->height(),
            context->presentationEnabled(), stage, {}, true, TestHelpers::ColorDarkGrey, true);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}
