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

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/framePassUtils.h>
#include <hvt/engine/viewport.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/blurTask.h>
#include <hvt/tasks/fxaaTask.h>
#include <hvt/tasks/resources.h>

#include <gtest/gtest.h>

HVT_TEST(TestViewportToolbox, TestFramePasses_MainOnly)
{
    // This unit test uses a frame pass to render a USD 3D model using Storm.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr _renderIndex;
    hvt::FramePassPtr _sceneFramePass;

    // Main scene Frame Pass.
    {
        // Creates the render index.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(_renderIndex, renderDesc);

        // Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        _renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the FramePass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = _renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/sceneFramePass");
        _sceneFramePass      = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Render 10 times (i.e., arbitrary number to guaranty best result).
    int frameCount = 10;

    auto render = [&]()
    {
        hvt::FramePassParams& params = _sceneFramePass->params();

        params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        _sceneFramePass->Render();

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    try
    {
        // Run the render loop.
        context->run(render, _sceneFramePass.get());

        // Validate the rendering result.
        const std::string computedImagePath = TestHelpers::getComputedImagePath();
        ASSERT_TRUE(
            context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
    }
    catch (const std::exception& ex)
    {
        FAIL() << __FILE__ << ":" << __LINE__ << ": " << ex.what() << "\n";
    }
}

// OGSMOD-8067 - Disabled for Android due to baseline inconsistency between runs
#if defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestFramePasses_MainWithBlur)
#else
HVT_TEST(TestViewportToolbox, TestFramePasses_MainWithBlur)
#endif
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr _renderIndex;
    hvt::FramePassPtr _sceneFramePass;

    static const float blurValue = 8.0f;

    // Main scene Frame Pass.
    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(_renderIndex, renderDesc);

        // Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        _renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = _renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/sceneFramePass");
        _sceneFramePass      = hvt::ViewportEngine::CreateFramePass(passDesc);

        // Creates & adds the blur custom task.

        {
            // Defines the blur task update function.

            auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                                hvt::TaskManager::SetTaskValueFn const& fnSetValue)
            {
                const VtValue value        = fnGetValue(HdTokens->params);
                hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
                params.blurAmount          = blurValue;
                fnSetValue(HdTokens->params, VtValue(params), false);
            };

            // Adds the blur task.

            const SdfPath pos = _sceneFramePass->GetTaskManager()->GetTaskPath(
                HdxPrimitiveTokens->presentTask);

            SdfPath blurPath =
                _sceneFramePass->GetTaskManager()->GetTaskPath(hvt::BlurTask::GetToken());

            if (blurPath.IsEmpty())
            {
                _sceneFramePass->GetTaskManager()->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(),
                    hvt::BlurTaskParams(), fnCommit, pos,
                    hvt::TaskManager::InsertionOrder::insertBefore);
            }
        }
    }

    // Render 10 frames.
    int frameCount = 10;
    auto render    = [&]()
    {
        // Update the scene frame pass.

        auto& params = _sceneFramePass->params();

        params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->disabled;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        _sceneFramePass->Render();

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    // Run the render loop.
    context->run(render, _sceneFramePass.get());

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// FIXME: The result image is not stable between runs on macOS. Refer to OGSMOD-8206.
// Note: As Android is now built on macOS platform, the same challenge exists!
#if defined(__APPLE__) || defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestFramePasses_MainWithFxaa)
#else
HVT_TEST(TestViewportToolbox, TestFramePasses_MainWithFxaa)
#endif
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr _renderIndex;
    hvt::FramePassPtr _sceneFramePass;

    // Main scene Frame Pass.
    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(_renderIndex, renderDesc);

        // Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        _renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = _renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/sceneFramePass");
        _sceneFramePass      = hvt::ViewportEngine::CreateFramePass(passDesc);

        // Creates & adds the fxaa custom task.

        {
            // Defines the anti-aliasing task update function.

            auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                                hvt::TaskManager::SetTaskValueFn const& fnSetValue)
            {
                auto framing = _sceneFramePass->params().renderParams.framing;

                const VtValue value        = fnGetValue(HdTokens->params);
                hvt::FXAATaskParams params = value.Get<hvt::FXAATaskParams>();
                params.pixelToUV           = GfVec2f(
                    1.0f / framing.dataWindow.GetWidth(),
                    1.0f / framing.dataWindow.GetHeight()
                );
                fnSetValue(HdTokens->params, VtValue(params), false);
            };

            // Adds the anti-aliasing task i.e., 'fxaaTask'.

            const SdfPath colorCorrectionTask = _sceneFramePass->GetTaskManager()->GetTaskPath(
                HdxPrimitiveTokens->colorCorrectionTask);

            SdfPath fxaaPath =
                _sceneFramePass->GetTaskManager()->GetTaskPath(hvt::FXAATask::GetToken());
            if (fxaaPath.IsEmpty())
            {
                // Note: Inserts the FXAA render task into the task list after color correction.

                fxaaPath = _sceneFramePass->GetTaskManager()->AddTask<hvt::FXAATask>(
                    hvt::FXAATask::GetToken(), hvt::FXAATaskParams(), fnCommit, colorCorrectionTask,
                    hvt::TaskManager::InsertionOrder::insertAfter);
            }
        }
    }

    // Render 10 frames.
    int frameCount = 10;
    auto render    = [&]()
    {
        // Update the scene frame pass.

        auto& params = _sceneFramePass->params();

        params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        _sceneFramePass->Render();

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    // Run the render loop.
    context->run(render, _sceneFramePass.get());

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

//
// The unit test is an example of a single frame pass using a scene index.
//
HVT_TEST(TestViewportToolbox, TestFramePasses_SceneIndex)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Step 1 - Create the render index.

    hvt::RenderIndexProxyPtr renderIndex;

    hvt::RendererDescriptor renderIndexDesc;
    renderIndexDesc.hgiDriver    = &context->_backend->hgiDriver();
    renderIndexDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(renderIndex, renderIndexDesc);

    // Step 2 - Create the scene index.

    HdSceneIndexBaseRefPtr sceneIndex;

    hvt::USDSceneIndexDescriptor sceneIndexDesc;
    // Set the USD model stage.
    sceneIndexDesc.stage = stage.stage();
    // Set the render index.
    sceneIndexDesc.renderIndex = renderIndex->RenderIndex();

    // Create the scene index.
    UsdImagingStageSceneIndexRefPtr stageSceneIndex;
    UsdImagingSelectionSceneIndexRefPtr selectionSceneIndex;
    hvt::ViewportEngine::CreateUSDSceneIndex(
        sceneIndex, stageSceneIndex, selectionSceneIndex, sceneIndexDesc);

    // Step 3 - Create the frame pass.

    hvt::FramePassPtr sceneFramePass;

    hvt::FramePassDescriptor passDesc;
    passDesc.renderIndex = renderIndex->RenderIndex();
    passDesc.uid         = SdfPath("/mainScenePass");
    sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);

    // Step 4 - Define the lambda updading the render pass.

    // Render 3 frames.
    int frameCount = 3;
    auto render    = [&]()
    {
        // Update the scene frame pass.

        hvt::FramePassParams& params = sceneFramePass->params();

        params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace                       = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor                  = TestHelpers::ColorDarkGrey;
        sceneFramePass->params().selectionColor = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        sceneFramePass->Render();

        return --frameCount > 0;
    };

    // Step 5 - Run the render loop.

    // Note: Refer to OpenGLTestcontext.cpp for an OpenGL implementation example.
    //
    // The pseudo code is:
    // while(!doQuit)
    // {
    //     render();
    // }
    //
    context->run(render, sceneFramePass.get());

    // Step 6 - Validate the expected result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Note: The second frame pass is not displayed on Android. Refer to OGSMOD-7277.
// Note: The two frame passes are displayed in the left part on iOS. Refer to OGSMOD-7278.
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestFramePasses_MultiViewports)
#else
HVT_TEST(TestViewportToolbox, TestFramePasses_MultiViewports)
#endif
{
    // The unit test mimics two viewports using frame passes.
    // The goal is to highlight 1) how to create two frame passes with different models,
    // and 2) how to define where to display the frame passes.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines the first frame pass.

    TestHelpers::TestStage stage1(context->_backend);

    ASSERT_TRUE(stage1.open(context->_sceneFilepath));

    // Creates the first frame pass with the default scene.
    framePass1 = TestHelpers::FramePassInstance::CreateInstance(stage1.stage(), context->_backend);

    // Defines the second frame pass.

    TestHelpers::TestStage stage2(context->_backend);

    // Works with a different scene.
    const std::string filepath =
        TestHelpers::getAssetsDataFolder().string() + "/usd/default_scene.usdz";
    ASSERT_TRUE(stage2.open(filepath));

    // Creates the second frame pass using a different scene.
    framePass2 = TestHelpers::FramePassInstance::CreateInstance(stage2.stage(), context->_backend);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    const int width  = context->width();
    const int height = context->height();

    auto render = [&]()
    {
        {
            hvt::FramePassParams& params = framePass1.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the left part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, height);

            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace           = HdxColorCorrectionTokens->disabled;
            params.clearBackgroundColor = true;
            params.backgroundColor      = TestHelpers::ColorDarkGrey;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Delays the display to the next frame pass.
            params.enablePresentation = false;

            // Renders the frame pass.
            framePass1.sceneFramePass->Render();

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        // Gets the input AOV's from the first frame pass and use them in all overlays so the
        // overlay's draw into the same color and depth buffers.

        auto& pass                          = framePass1.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs = pass->GetRenderBufferBindingsForNextPass(
            { pxr::HdAovTokens->color, pxr::HdAovTokens->depth });

        {
            auto& params = framePass2.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the right part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, 0, width / 2, height);

            params.viewInfo.viewMatrix       = stage2.viewMatrix();
            params.viewInfo.projectionMatrix = stage2.projectionMatrix();
            params.viewInfo.lights           = stage2.defaultLights();
            params.viewInfo.material         = stage2.defaultMaterial();
            params.viewInfo.ambient          = stage2.defaultAmbient();

            params.colorspace = HdxColorCorrectionTokens->disabled;
            // Do not clear the background as the texture contains the rendering of the previous
            // frame pass.
            params.clearBackgroundColor = false;
            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor       = TestHelpers::ColorYellow;

            params.enablePresentation = context->presentationEnabled();

            // Gets the list of tasks to render but use the render buffers from the first frame
            // pass.
            const HdTaskSharedPtrVector renderTasks =
                framePass2.sceneFramePass->GetRenderTasks(inputAOVs);

            framePass2.sceneFramePass->Render(renderTasks);

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass2.sceneFramePass.get());

    // Validates the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Note: The second frame pass is not displayed on Android. Refer to OGSMOD-7277.
// Note: The two frame passes are displayed in the left part on iOS. Refer to OGSMOD-7278.
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestFramePasses_MultiViewportsClearDepth)
#else
HVT_TEST(TestViewportToolbox, TestFramePasses_MultiViewportsClearDepth)
#endif
{
    // The unit test mimics two viewports using frame passes.
    // The goal is to check that the depth buffer is cleared in the second frame pass.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines the first frame pass.

    TestHelpers::TestStage stage1(context->_backend);

    ASSERT_TRUE(stage1.open(context->_sceneFilepath));

    // Creates the first frame pass with the default scene.
    framePass1 = TestHelpers::FramePassInstance::CreateInstance(stage1.stage(), context->_backend);

    // Defines the second frame pass.

    TestHelpers::TestStage stage2(context->_backend);

    // Works with a different scene.
    const std::string filepath =
        TestHelpers::getAssetsDataFolder().string() + "/usd/default_scene.usdz";
    ASSERT_TRUE(stage2.open(filepath));

    // Creates the second frame pass using a different scene.
    framePass2 = TestHelpers::FramePassInstance::CreateInstance(stage2.stage(), context->_backend);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    const int width  = context->width();
    const int height = context->height();

    auto render = [&]()
    {
        {
            hvt::FramePassParams& params = framePass1.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the left part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, height);

            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace           = HdxColorCorrectionTokens->disabled;
            params.clearBackgroundColor = true;
            params.clearBackgroundDepth = true;
            params.backgroundColor      = TestHelpers::ColorDarkGrey;
            params.backgroundDepth      = 1.0f;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Only visualizes the depth.
            params.visualizeAOV = HdAovTokens->depth;

            // Usually false but display the depth aov in that case.
            params.enablePresentation = context->presentationEnabled();

            // Renders the frame pass.
            framePass1.sceneFramePass->Render();

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        // Gets the 'depth' input AOV from the first frame pass and use it in all overlays so the
        // overlay's draw into the same depth buffer.

        auto& pass                          = framePass1.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs = pass->GetRenderBufferBindingsForNextPass(
            {pxr::HdAovTokens->depth });

        {
            auto& params = framePass2.sceneFramePass->params();

            params.renderBufferSize          = GfVec2i(width, height);
            // To display on the right part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, 0, width / 2, height);

            params.viewInfo.viewMatrix       = stage2.viewMatrix();
            params.viewInfo.projectionMatrix = stage2.projectionMatrix();
            params.viewInfo.lights           = stage2.defaultLights();
            params.viewInfo.material         = stage2.defaultMaterial();
            params.viewInfo.ambient          = stage2.defaultAmbient();

            params.colorspace = HdxColorCorrectionTokens->disabled;
            // Do not clear the background as the texture contains the rendering of the previous
            // frame pass.
            params.clearBackgroundColor = false;
            // But clear the depth buffer.
            params.clearBackgroundDepth = true;
            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Only visualizes the depth.
            params.visualizeAOV = HdAovTokens->depth;

            params.enablePresentation = context->presentationEnabled();

            // Gets the list of tasks to render but use the render buffers from the first frame
            // pass.
            const HdTaskSharedPtrVector renderTasks =
                framePass2.sceneFramePass->GetRenderTasks(inputAOVs);

            framePass2.sceneFramePass->Render(renderTasks);

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass2.sceneFramePass.get());

    // Validates the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Note: The second frame pass is not displayed on Android. Refer to OGSMOD-7277.
#if defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestFramePasses_TestDynamicAovInputs)
#else
HVT_TEST(TestViewportToolbox, TestFramePasses_TestDynamicAovInputs)
#endif
{
    // The unit test mimics two viewports using frame passes.
    // The goal is to highlight 1) how to create two frame passes with different models,
    // 2) how to define where to display the frame passes,
    // and 3) to simulate an AOV list change.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines the first frame pass.

    TestHelpers::TestStage stage1(context->_backend);

    ASSERT_TRUE(stage1.open(context->_sceneFilepath));

    // Creates the first frame pass with the default scene.
    framePass1 = TestHelpers::FramePassInstance::CreateInstance(stage1.stage(), context->_backend);

    // Defines the second frame pass.

    TestHelpers::TestStage stage2(context->_backend);

    // Works with a different scene.
    const std::string filepath =
        TestHelpers::getAssetsDataFolder().string() + "/usd/default_scene.usdz";
    ASSERT_TRUE(stage2.open(filepath));

    // Creates the second frame pass using a different scene.
    framePass2 = TestHelpers::FramePassInstance::CreateInstance(stage2.stage(), context->_backend);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    const int width  = context->width();
    const int height = context->height();

    auto render = [&]()
    {
        // Test dynamically switching buffer reuse from frame pass 1.
        bool isSharingBuffers = frameCount > 5;

        {
            hvt::FramePassParams& params = framePass1.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the left part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, height);

            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace           = HdxColorCorrectionTokens->disabled;
            params.clearBackgroundColor = true;
            params.backgroundColor      = TestHelpers::ColorDarkGrey;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Delays the display to the next frame pass.
            params.enablePresentation = false;

            // Renders the frame pass.
            framePass1.sceneFramePass->Render();

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        // Draw the 2nd pass into the 1st pass color and depth buffers if sharing.
        const hvt::RenderBufferBindings inputAOVs = isSharingBuffers
            ? framePass1.sceneFramePass->GetRenderBufferBindingsForNextPass(
                  { HdAovTokens->color, HdAovTokens->depth })
            : hvt::RenderBufferBindings();

        {
            auto& params = framePass2.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the right part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, 0, width / 2, height);

            params.viewInfo.viewMatrix       = stage2.viewMatrix();
            params.viewInfo.projectionMatrix = stage2.projectionMatrix();
            params.viewInfo.lights           = stage2.defaultLights();
            params.viewInfo.material         = stage2.defaultMaterial();
            params.viewInfo.ambient          = stage2.defaultAmbient();

            params.colorspace = HdxColorCorrectionTokens->disabled;

            // New buffers need to be cleared, to avoid issues with uninitialized texture content. 
            params.clearBackgroundColor = !isSharingBuffers;
            params.clearBackgroundDepth = !isSharingBuffers;
            params.backgroundColor      = TestHelpers::ColorDarkGrey;
            params.selectionColor       = TestHelpers::ColorYellow;

            params.enablePresentation = context->presentationEnabled();

            // Gets the list of tasks to render but use the render buffers from the first frame
            // pass.
            const HdTaskSharedPtrVector renderTasks =
                framePass2.sceneFramePass->GetRenderTasks(inputAOVs);

            framePass2.sceneFramePass->Render(renderTasks);

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass2.sceneFramePass.get());

    HdRenderBuffer* pass1color = framePass1.sceneFramePass->GetRenderBuffer(HdAovTokens->color);
    HdRenderBuffer* pass1depth = framePass1.sceneFramePass->GetRenderBuffer(HdAovTokens->depth);
    HdRenderBuffer* pass2color = framePass2.sceneFramePass->GetRenderBuffer(HdAovTokens->color);
    HdRenderBuffer* pass2depth = framePass2.sceneFramePass->GetRenderBuffer(HdAovTokens->depth);

    // Make sure each pass has valid render output buffers.

    ASSERT_TRUE(pass1color && pass1depth && pass2color && pass2depth);

    // Make sure the buffers haven't been shared for the last render loop.

    ASSERT_TRUE(pass1color != pass2color);
    ASSERT_TRUE(pass1depth != pass2depth);

    // Validates the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Note: The second frame pass is not displayed on Android. Refer to OGSMOD-7277.
// Note: The two frame passes are displayed in the left part on iOS. Refer to OGSMOD-7278.
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestFramePasses_ClearDepthBuffer)
#else
HVT_TEST(TestViewportToolbox, TestFramePasses_ClearDepthBuffer)
#endif
{
    // The unit test mimics two viewports using frame passes.
    // The goal is to check that the depth buffer is cleared in the second frame pass.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines the first frame pass.

    TestHelpers::TestStage stage1(context->_backend);

    ASSERT_TRUE(stage1.open(context->_sceneFilepath));

    // Creates the first frame pass with the default scene.
    framePass1 = TestHelpers::FramePassInstance::CreateInstance(stage1.stage(), context->_backend);

    // Defines the second frame pass.

    TestHelpers::TestStage stage2(context->_backend);

    // Works with a different scene.
    const std::string filepath =
        TestHelpers::getAssetsDataFolder().string() + "/usd/default_scene.usdz";
    ASSERT_TRUE(stage2.open(filepath));

    // Creates the second frame pass using a different scene.
    framePass2 = TestHelpers::FramePassInstance::CreateInstance(stage2.stage(), context->_backend);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    const int width  = context->width();
    const int height = context->height();

    auto render = [&]()
    {
        {
            hvt::FramePassParams& params = framePass1.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the left part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, height);
            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace           = HdxColorCorrectionTokens->disabled;
            params.clearBackgroundColor = true;
            params.clearBackgroundDepth = true;
            params.backgroundColor      = TestHelpers::ColorDarkGrey;
            params.backgroundDepth      = 1.0f;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Only visualizes the depth.
            params.visualizeAOV = HdAovTokens->depth;

            // Do not display the depth aov.
            params.enablePresentation = false;

            // Renders the frame pass.
            framePass1.sceneFramePass->Render();

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        // Gets the 'depth' input AOV from the first frame pass and use it in all overlays so the
        // overlay's draw into the same depth buffer.

        auto& pass                          = framePass1.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs = pass->GetRenderBufferBindingsForNextPass(
            { pxr::HdAovTokens->depth });
        {
            auto& params = framePass2.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the right part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, 0, width / 2, height);
            params.viewInfo.viewMatrix       = stage2.viewMatrix();
            params.viewInfo.projectionMatrix = stage2.projectionMatrix();
            params.viewInfo.lights           = stage2.defaultLights();
            params.viewInfo.material         = stage2.defaultMaterial();
            params.viewInfo.ambient          = stage2.defaultAmbient();

            params.colorspace = HdxColorCorrectionTokens->disabled;

            // Clear depth for the first 5 frames, then stop
            // clearing for the final render (after frame 5). This will validate
            // the clear does not "stick" once it is enabled.
            params.clearBackgroundDepth = (frameCount > 5);

            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Only visualizes the depth.
            params.visualizeAOV = HdAovTokens->depth;

            params.enablePresentation = context->presentationEnabled();

            // Gets the list of tasks to render but use the render buffers from the first frame
            // pass.
            const HdTaskSharedPtrVector renderTasks =
                framePass2.sceneFramePass->GetRenderTasks(inputAOVs);

            framePass2.sceneFramePass->Render(renderTasks);

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass2.sceneFramePass.get());

    // Validates the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

// Note: The second frame pass is not displayed on Android. Refer to OGSMOD-7277.
// Note: The two frame passes are displayed in the left part on iOS. Refer to OGSMOD-7278.
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox,
    DISABLED_TestFramePasses_ClearColorBuffer)
#else
HVT_TEST(TestViewportToolbox, TestFramePasses_ClearColorBuffer)
#endif
{
    // The unit test mimics two viewports using frame passes.
    // The goal is to check that the color buffer is cleared in the second frame pass.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::FramePassInstance framePass1, framePass2;

    // Defines the first frame pass.

    TestHelpers::TestStage stage1(context->_backend);

    ASSERT_TRUE(stage1.open(context->_sceneFilepath));

    // Creates the first frame pass with the default scene.
    framePass1 = TestHelpers::FramePassInstance::CreateInstance(stage1.stage(), context->_backend);

    // Defines the second frame pass.

    TestHelpers::TestStage stage2(context->_backend);

    // Works with a different scene.
    const std::string filepath =
        TestHelpers::getAssetsDataFolder().string() + "/usd/default_scene.usdz";
    ASSERT_TRUE(stage2.open(filepath));

    // Creates the second frame pass using a different scene.
    framePass2 = TestHelpers::FramePassInstance::CreateInstance(stage2.stage(), context->_backend);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    const int width  = context->width();
    const int height = context->height();

    auto render = [&]()
    {
        {
            hvt::FramePassParams& params = framePass1.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the left part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, height);
            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace           = HdxColorCorrectionTokens->disabled;
            params.clearBackgroundColor = true;
            params.clearBackgroundDepth = true;
            params.backgroundColor      = TestHelpers::ColorDarkGrey;
            params.backgroundDepth      = 1.0f;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Only visualizes the color.
            params.visualizeAOV = HdAovTokens->color;

            // Do not display the color aov.
            params.enablePresentation = false;

            // Renders the frame pass.
            framePass1.sceneFramePass->Render();

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        // Gets the 'color' input AOV from the first frame pass and use it in all overlays so the
        // overlay's draw into the same color buffer.

        auto& pass                          = framePass1.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs = pass->GetRenderBufferBindingsForNextPass(
            { pxr::HdAovTokens->color });

        {
            auto& params = framePass2.sceneFramePass->params();

            params.renderBufferSize = GfVec2i(width, height);
            // To display on the right part of the viewport.
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(width / 2, 0, width / 2, height);
            params.viewInfo.viewMatrix       = stage2.viewMatrix();
            params.viewInfo.projectionMatrix = stage2.projectionMatrix();
            params.viewInfo.lights           = stage2.defaultLights();
            params.viewInfo.material         = stage2.defaultMaterial();
            params.viewInfo.ambient          = stage2.defaultAmbient();

            params.colorspace = HdxColorCorrectionTokens->disabled;

            // Clear color for the first 5 frames, then stop
            // clearing for the final render (after frame 5). This will validate
            // the clear does not "stick" once it is enabled.
            params.clearBackgroundColor = (frameCount > 5);

            params.backgroundColor = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor  = TestHelpers::ColorYellow;

            // Only visualizes the color.
            params.visualizeAOV = HdAovTokens->color;

            params.enablePresentation = context->presentationEnabled();

            // Gets the list of tasks to render but use the render buffers from the first frame
            // pass.
            const HdTaskSharedPtrVector renderTasks =
                framePass2.sceneFramePass->GetRenderTasks(inputAOVs);

            framePass2.sceneFramePass->Render(renderTasks);

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass2.sceneFramePass.get());

    // Validates the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

HVT_TEST(TestViewportToolbox, TestFramePasses_DisplayClipping1)
{
    // This unit test uses a frame pass to only display a part of the USD 3D model.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    TestHelpers::FramePassInstance framePass =
        TestHelpers::FramePassInstance::CreateInstance(stage.stage(), context->_backend);

    // Render 10 times (i.e., arbitrary number to guaranty best result).
    int frameCount = 10;

    auto render = [&]()
    {
        hvt::FramePassParams& params = framePass.sceneFramePass->params();

        const auto width = context->width();
        const auto height = context->height();

        params.renderBufferSize = pxr::GfVec2i(width, height);
        // Takes all the rendered image but only displays the left part.
        params.viewInfo.framing = {
            // Data window: full render buffer.
            { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
            // Display window: left part only.
            { { 0, 0 }, { width / 2, height } }, 
            1.0f
        };

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        framePass.sceneFramePass->Render();

        return --frameCount > 0;
    };

    // Run the render loop.

    context->run(render, framePass.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

HVT_TEST(TestViewportToolbox, TestFramePasses_DisplayClipping2)
{
    // This unit test uses a frame pass to display only the center quarter of the USD 3D model
    // with additional offset, creating a more complex clipping scenario.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    TestHelpers::FramePassInstance framePass =
        TestHelpers::FramePassInstance::CreateInstance(stage.stage(), context->_backend);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        hvt::FramePassParams& params = framePass.sceneFramePass->params();

        const auto width = context->width();
        const auto height = context->height();

        params.renderBufferSize = pxr::GfVec2i(width, height);
        
        // More complex clipping: Display only the center quarter with a slight offset
        // Render buffer covers the full image size.
        // Display region shows a quarter-size window offset slightly from center.
        const int quarterWidth = width / 4;
        const int quarterHeight = height / 4;
        const int offsetX = width / 3;   // Offset from left (33% from left edge)
        const int offsetY = height / 3;  // Offset from top (33% from top edge)

        params.viewInfo.framing = { 
            // Data window: full render buffer.
            { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
            // Display window: center quarter with offset.
            { { offsetX, offsetY }, { offsetX + quarterWidth, offsetY + quarterHeight } }, 
            1.0f 
        };

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        framePass.sceneFramePass->Render();

        return --frameCount > 0;
    };

    // Run the render loop.

    context->run(render, framePass.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}
