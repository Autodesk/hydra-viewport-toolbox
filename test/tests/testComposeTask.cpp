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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

#include "composeTaskHelpers.h"
#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/framePassUtils.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/sceneIndex/boundingBoxSceneIndex.h>
#include <hvt/sceneIndex/displayStyleOverrideSceneIndex.h>
#include <hvt/sceneIndex/wireFrameSceneIndex.h>
#include <hvt/tasks/resources.h>

#include <gtest/gtest.h>

namespace
{
constexpr int imageWidth { 1024 };
constexpr int imageHeight { 768 };
} // namespace

// NOTE: Android unit test framework does not report the error message making it impossible to fix
// issues. Refer to OGSMOD-5546.
//
// NOTE: wireframe does not work on macOS/Metal.
// Refer to https://forum.aousd.org/t/hdstorm-mesh-wires-drawing-issue-in-usd-24-05-on-macos/1523
//
#if defined(__ANDROID__) || defined(__APPLE__)
HVT_TEST(TestViewportToolbox, DISABLED_compose_ComposeTask)
#else
HVT_TEST(TestViewportToolbox, compose_ComposeTask)
#endif
{
    // This unit test uses the 'Storm' render delegate for the two frame passes, to demonstrate that
    // the color composition of the two frame passes plus the share of the depth render buffer
    // works.

    auto context = TestHelpers::CreateTestContext(imageWidth, imageHeight);

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines the first frame pass using the Storm render delegate.

    framePass1 = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend);

    // Defines the second frame pass using the Storm render delegate.

    framePass2 = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend);

    // Adds the 'Compose' task to the second frame pass.

    TestHelpers::AddComposeTask(framePass1, framePass2);

    // Render 10 times (i.e., arbitrary number to guaranty best result).
    int frameCount = 10;

    auto render = [&]()
    {
        TestHelpers::RenderFirstFramePass(framePass1, context->width(), context->height(), stage);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        // Gets the depth from the first frame pass and use it so the overlays draw into the same
        // depth buffer (because depth has always the same bit depth i.e., 32-bit float for all
        // the render delegates).
        auto& pass = framePass1.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs =
            pass->GetRenderBufferBindingsForNextPass({ pxr::HdAovTokens->depth });

        {
            hvt::FramePassParams& params = framePass2.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            // Do not color manage a wire frame.
            params.colorspace = HdxColorCorrectionTokens->disabled;
            // No alpha is needed by the alpha blending.
            params.backgroundColor = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor  = TestHelpers::ColorYellow;

            // Only display the wire fame of the model.
            params.collection =
                HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->wire));

            params.enablePresentation = context->presentationEnabled();

            // Gets the list of tasks to render but use the render buffers from the main frame pass.
            const HdTaskSharedPtrVector renderTasks =
                framePass2.sceneFramePass->GetRenderTasks(inputAOVs);

            framePass2.sceneFramePass->Render(renderTasks);
        }

        return --frameCount > 0;
    };

    // Run the render loop.

    context->run(render, framePass2.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// NOTE: Android unit test framework does not report the error message making it impossible to fix
// issues. Refer to OGSMOD-5546.
//
// NOTE: wireframe does not work on macOS/Metal.
// Refer to https://forum.aousd.org/t/hdstorm-mesh-wires-drawing-issue-in-usd-24-05-on-macos/1523
//
#if defined(__ANDROID__) || defined(__APPLE__)
HVT_TEST(TestViewportToolbox, DISABLED_compose_ShareTextures)
#else
HVT_TEST(TestViewportToolbox, compose_ShareTextures)
#endif
{
    // This unit test uses the 'Storm' render delegate for the two frame passes, to demonstrate that
    // the color the share of the color & depth render buffers works.

    auto context = TestHelpers::CreateTestContext(imageWidth, imageHeight);

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines the first frame pass using the Storm render delegate.

    framePass1 = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend);

    // Defines the second frame pass using the Storm render delegate.

    framePass2 = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend);

    // Render 10 times (i.e., arbitrary number to guaranty best result).
    int frameCount = 10;

    auto render = [&]()
    {
        TestHelpers::RenderFirstFramePass(framePass1, context->width(), context->height(), stage);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        // Gets the input AOV's from the first frame pass and use them in all overlays so the
        // overlays draw into the same color and depth buffers.

        auto& pass                          = framePass1.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs = pass->GetRenderBufferBindingsForNextPass(
            { pxr::HdAovTokens->color, pxr::HdAovTokens->depth }, false);

        {
            hvt::FramePassParams& params = framePass2.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            // No color management.
            params.colorspace = HdxColorCorrectionTokens->disabled;
            // Do not clear the background as it contains the previous frame pass result.
            params.clearBackgroundColor = false;
            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Only display the wire fame of the model.
            params.collection =
                HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->wire));

            params.enablePresentation = context->presentationEnabled();

            // Gets the list of tasks to render but use the render buffers from the main frame pass.
            const HdTaskSharedPtrVector renderTasks =
                framePass2.sceneFramePass->GetRenderTasks(inputAOVs);

            framePass2.sceneFramePass->Render(renderTasks);
        }

        return --frameCount > 0;
    };

    // Run the render loop.

    context->run(render, framePass2.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

//
// The following unit tests are testing that the ComposeTask correctly composes frame passes
// whatever are the two frame pass types e.g., model vs scene index filters.
//
// Note: The ColorCorrectionTask is always disabled to avoid slight color differences between the
// difference unit tests and then, better detect failures if it happens one day.
//
// Note: The 'Bounding Box' is used (instead of the 'WireFrame' one) because it works on all
// desktop platforms.

// OGSMOD-7344: Disabled for iOS as the result is not stable.
// OGSMOD-8067: Disabled for Android due to baseline inconsistency between runs.
#if TARGET_OS_IPHONE == 1 || defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_compose_ComposeTask2)
#else
HVT_TEST(TestViewportToolbox, compose_ComposeTask2)
#endif
{
    // This unit test uses the 'Storm' render delegate for the two frame passes, to demonstrate that
    // the compose task works. But the first frame pass displays the bounding box of the model and
    // the second one displays the model.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);

    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines first frame pass i.e., render the bounding box only with Storm.

    {
        // Creates the render index using the Storm render delegate.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver = &context->_backend->hgiDriver();
        // Use the 'Storm' render delegate as it supports 'Basis Curves' needed by the bounding box.
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass1.renderIndex, renderDesc);

        // Creates the scene index with the bounding box scene index filtering.

        framePass1.sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        // Adds the bounding box scene index filter.
        framePass1.sceneIndex = hvt::BoundingBoxSceneIndex::New(framePass1.sceneIndex);
        framePass1.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass1.sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex      = framePass1.renderIndex->RenderIndex();
        passDesc.uid              = SdfPath("/sceneFramePass1");
        framePass1.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Defines the second frame pass using the Storm render delegate.

    framePass2 = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend, "/sceneFramePass2");

    // Adds the 'Compose' task to the second frame pass i.e., compose the color AOV.

    TestHelpers::AddComposeTask(framePass1, framePass2);

    // Render 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        TestHelpers::RenderFirstFramePass(framePass1, context->width(), context->height(), stage);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        // Gets the depth from the first frame pass and use it so the overlays draw into the same
        // depth buffer (because depth has always the same bit depth i.e., 32-bit float for all
        // the render delegates).

        // No need to share the color AOV as the ComposeTask will take care of it.
        hvt::RenderBufferBindings inputAOVs =
            framePass1.sceneFramePass->GetRenderBufferBindingsForNextPass({ pxr::HdAovTokens->depth });

        // NoAlpha mandatory for the blending used by ComposeTask.
        TestHelpers::RenderSecondFramePass(framePass2, context->width(), context->height(),
            context->presentationEnabled(), stage, inputAOVs, true, TestHelpers::ColorBlackNoAlpha,
            false);

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass2.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// OGSMOD-7344: Disabled for iOS as the result is not stable.
// OGSMOD-8067: Disabled for Android due to baseline inconsistency between runs.
#if TARGET_OS_IPHONE == 1 || defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_compose_ComposeTask3)
#else
HVT_TEST(TestViewportToolbox, compose_ComposeTask3)
#endif
{
    // This unit test performs the same validation than the 'Compose_ComposeTask2' unit test but the
    // first frame pass displays the model and the second one display the bounding box for the
    // model.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);

    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines first frame pass using the Storm render delegate.

    framePass1 = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend);

    // Defines the second frame pass.

    {
        // Creates the render index using the Storm render delegate.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver = &context->_backend->hgiDriver();
        // Use the 'Storm' render delegate to render the scene.
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass2.renderIndex, renderDesc);

        // Creates the scene index with the bounding box scene index filtering.

        framePass2.sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        // Adds the bounding box scene index filter.
        framePass2.sceneIndex = hvt::BoundingBoxSceneIndex::New(framePass2.sceneIndex);
        framePass2.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass2.sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex      = framePass2.renderIndex->RenderIndex();
        passDesc.uid              = SdfPath("/sceneFramePass2");
        framePass2.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);

        // Adds the 'Compose' task to the second frame pass i.e., compose the color AOV.

        TestHelpers::AddComposeTask(framePass1, framePass2);
    }

    // Render 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        TestHelpers::RenderFirstFramePass(framePass1, context->width(), context->height(), stage);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        // Gets the depth from the first frame pass and use it so the overlays draw into the same
        // depth buffer (because depth has always the same bit depth i.e., 32-bit float for all
        // the render delegates).

        // No need to share the color AOV as the ComposeTask will take care of it.
        hvt::RenderBufferBindings inputAOVs =
            framePass1.sceneFramePass->GetRenderBufferBindingsForNextPass({ pxr::HdAovTokens->depth });

        // NoAlpha mandatory for the blending used by ComposeTask.
        TestHelpers::RenderSecondFramePass(framePass2, context->width(), context->height(),
            context->presentationEnabled(), stage, inputAOVs, true, TestHelpers::ColorBlackNoAlpha,
            false);

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass2.sceneFramePass.get());

    // Validates the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// NOTE: Android unit test regularly intermittently fails, not always rendering the bounding box.
// Refer to OGSMOD-7309.
#if defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_compose_ShareTextures4)
#else
HVT_TEST(TestViewportToolbox, compose_ShareTextures4)
#endif
{
    // This unit test performs the same validation than the 'Compose_ComposeTask3' unit test except
    // that it shares the color & depth render buffers i.e., does not use the compose task.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);

    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines first frame pass using the Storm render delegate.

    framePass1 = TestHelpers::FramePassInstance::CreateInstance(
        "HdStormRendererPlugin", stage.stage(), context->_backend);

    // Defines the second frame pass.

    {
        // Creates the render index the Storm render delegate.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass2.renderIndex, renderDesc);

        // Creates the scene index with the bounding box scene index filtering.

        framePass2.sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        // Adds the bounding box scene index filter.
        framePass2.sceneIndex = hvt::BoundingBoxSceneIndex::New(framePass2.sceneIndex);
        framePass2.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass2.sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex      = framePass2.renderIndex->RenderIndex();
        passDesc.uid              = SdfPath("/sceneFramePass2");
        framePass2.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Render 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        TestHelpers::RenderFirstFramePass(framePass1, context->width(), context->height(), stage);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        // Gets the input AOV's from the first frame pass and use them in all overlays so the
        // overlays draw into the same color and depth buffers.

        auto& pass                          = framePass1.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs = pass->GetRenderBufferBindingsForNextPass(
            { pxr::HdAovTokens->color, pxr::HdAovTokens->depth }, false);

        // When sharing the render buffers, do not clear the background as it contains the rendering
        // result of the previous frame pass.
        TestHelpers::RenderSecondFramePass(framePass2, context->width(), context->height(),
            context->presentationEnabled(), stage, inputAOVs, false, TestHelpers::ColorDarkGrey, false);

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass2.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}
