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

#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestHelpers.h>

#include <hvt/engine/framePassUtils.h>
#include <hvt/engine/viewportEngine.h>

#include <hvt/tasks/copyDepthToDepthMsaaTask.h>
#include <hvt/tasks/depthBiasTask.h>
#include <hvt/tasks/resources.h>

#include <gtest/gtest.h>

TEST(engine, ZDepthFightingTestNoMultisampling)
{
    // The unit test demonstrates how to fix the 'z-depth fighting' between two frame passes using
    // the depth bias task.

    // Large image to better see the 'z-depth' issue.
    auto context = TestHelpers::CreateTestContext(1024, 768);

    // Use a specific scene to better highlight the issue.
    TestHelpers::TestStage stage1(context->_backend);
    const std::string filepath1 =
#if defined(__APPLE__)
        TestHelpers::getAssetsDataFolder().string() + "/usd/test_zdepth_fight_red_only_osx.usd";
#else
        TestHelpers::getAssetsDataFolder().string() + "/usd/test_zdepth_fight_red_only.usd";
#endif
    ASSERT_TRUE(stage1.open(filepath1));

    TestHelpers::FramePassInstance instance1 =
        TestHelpers::FramePassInstance::CreateInstance(stage1.stage(), context->_backend);

    // Use a specific scene to better highlight the issue.
    TestHelpers::TestStage stage2(context->_backend);
    const std::string filepath2 =
        TestHelpers::getAssetsDataFolder().string() + "/usd/test_zdepth_fight_blue_only.usd";
    ASSERT_TRUE(stage2.open(filepath2));

    TestHelpers::FramePassInstance instance2 =
        TestHelpers::FramePassInstance::CreateInstance(stage2.stage(), context->_backend);

    // By default, the two scenes are rendered with a 'z-depth fighting'. The depth bias task is
    // used to apply an offset on the existing depth buffer.
    {
        // Defines the 'Depth Bias' task update function.
        auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const pxr::VtValue value        = fnGetValue(pxr::HdTokens->params);
            hvt::DepthBiasTaskParams params = value.Get<hvt::DepthBiasTaskParams>();

            params.depthBiasEnable = true;
            
            auto renderParams           = instance1.sceneFramePass->params().renderParams;
            // offset in view space units, positive value draws towards the camera
            params.viewSpaceDepthOffset = 0.1f;
            params.view.cameraID        = renderParams.camera;
            params.view.framing         = renderParams.framing;

            fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
        };

        // Adds the 'Depth Bias'.
        const pxr::SdfPath refTask = instance1.sceneFramePass->GetTaskManager()->GetTaskPath(
            pxr::HdxPrimitiveTokens->presentTask);

        instance1.sceneFramePass->GetTaskManager()->AddTask<hvt::DepthBiasTask>(
            hvt::DepthBiasTask::GetToken(), hvt::DepthBiasTaskParams(), fnCommit, refTask,
            hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 100;

    auto render = [&]()
    {
        // Updates the first frame pass.
        {
            auto& params = instance1.sceneFramePass->params();

            params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace          = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor     = TestHelpers::ColorDarkGrey;
            params.selectionColor      = TestHelpers::ColorYellow;
            params.msaaSampleCount     = 1;
            params.enableMultisampling = false;

            // Clears the backgrounds.
            params.clearBackgroundColor = true;
            params.clearBackgroundDepth = true;

            // Do not display right now, wait for the second frame pass.
            params.enablePresentation = false;

            instance1.sceneFramePass->Render();
        }

        // Gets the input AOV's from the first frame pass and use them in all overlays so the
        // overlay's draw into the same color and depth buffers.

        pxr::HdRenderBuffer* colorBuffer =
            instance1.sceneFramePass->GetRenderBuffer(pxr::HdAovTokens->color);

        pxr::HdRenderBuffer* depthBuffer =
            instance1.sceneFramePass->GetRenderBuffer(pxr::HdAovTokens->depth);

        const std::vector<std::pair<pxr::TfToken const&, pxr::HdRenderBuffer*>> inputAOVs = {
            { pxr::HdAovTokens->color, colorBuffer }, { pxr::HdAovTokens->depth, depthBuffer }
        };

        // Updates the second frame pass.
        {
            auto& params = instance2.sceneFramePass->params();

            params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            // need to use the same view parameters as the first frame to not clip stuff
            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();

            params.viewInfo.lights   = stage2.defaultLights();
            params.viewInfo.material = stage2.defaultMaterial();
            params.viewInfo.ambient  = stage2.defaultAmbient();

            params.colorspace          = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor     = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor      = TestHelpers::ColorYellow;
            params.msaaSampleCount     = 1;
            params.enableMultisampling = false;

            // Do not clear the backgrounds as they contain the previous frame pass result.
            params.clearBackgroundColor = false;
            params.clearBackgroundDepth = false;

            // Gets the list of tasks to render but use the render buffers from the main frame pass.
            const pxr::HdTaskSharedPtrVector renderTasks =
                instance2.sceneFramePass->GetRenderTasks(inputAOVs);

            instance2.sceneFramePass->Render(renderTasks);
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).
    context->run(render, instance1.sceneFramePass.get());

    // Validates the rendering result.
    const std::string imageFile { test_info_->name() };
    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile, 1));
}

TEST(engine, ZDepthFightingTestMultisampling)
{
    // The unit test demonstrates how to fix the 'z-depth fighting' between two frame passes using
    // the depth bias task.

    // Large image to better see the 'z-depth' issue.
    auto context = TestHelpers::CreateTestContext(1024, 768);

    // Use a specific scene to better highlight the issue.
    TestHelpers::TestStage stage1(context->_backend);
    const std::string filepath1 =
#if defined(__APPLE__)
        TestHelpers::getAssetsDataFolder().string() + "/usd/test_zdepth_fight_red_only_osx.usd";
#else
        TestHelpers::getAssetsDataFolder().string() + "/usd/test_zdepth_fight_red_only.usd";
#endif
    ASSERT_TRUE(stage1.open(filepath1));

    TestHelpers::FramePassInstance instance1 =
        TestHelpers::FramePassInstance::CreateInstance(stage1.stage(), context->_backend);

    // Use a specific scene to better highlight the issue.
    TestHelpers::TestStage stage2(context->_backend);
    const std::string filepath2 =
        TestHelpers::getAssetsDataFolder().string() + "/usd/test_zdepth_fight_blue_only.usd";
    ASSERT_TRUE(stage2.open(filepath2));

    TestHelpers::FramePassInstance instance2 =
        TestHelpers::FramePassInstance::CreateInstance(stage2.stage(), context->_backend);

    // By default, the two scenes are rendered with a 'z-depth fighting'. The depth bias task is
    // used to apply an offset on the existing depth buffer.
    {
        // Defines the 'Depth Bias' task update function.
        auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const pxr::VtValue value        = fnGetValue(pxr::HdTokens->params);
            hvt::DepthBiasTaskParams params = value.Get<hvt::DepthBiasTaskParams>();

            params.depthBiasEnable = true;
            
            auto renderParams           = instance1.sceneFramePass->params().renderParams;
            // offset in view space units, positive value draws towards the camera
            params.viewSpaceDepthOffset = 0.1f;
            params.view.cameraID        = renderParams.camera;
            params.view.framing         = renderParams.framing;

            fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
        };

        // Adds the 'Depth Bias'.
        const pxr::SdfPath refTask = instance1.sceneFramePass->GetTaskManager()->GetTaskPath(
            pxr::HdxPrimitiveTokens->presentTask);

        instance1.sceneFramePass->GetTaskManager()->AddTask<hvt::DepthBiasTask>(
            hvt::DepthBiasTask::GetToken(), hvt::DepthBiasTaskParams(), fnCommit, refTask,
            hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Add the CopyDepthToDepthMsaa task to copy the depth-biased depth to the MSAA depth buffer
    {
        // Defines the 'CopyDepthToDepthMsaa' task update function.
        auto fnCommitCopyDepth = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                                     hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
            hvt::CopyDepthToDepthMsaaTaskParams params;
            if (value.IsHolding<hvt::CopyDepthToDepthMsaaTaskParams>()) {
                params = value.Get<hvt::CopyDepthToDepthMsaaTaskParams>();
            }
            
            // Set source as resolved depth and target as MSAA depth
            params.sourceDepthAovName = pxr::HdAovTokens->depth;
            params.targetDepthAovName = pxr::TfToken("depthMSAA");

            fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
        };

        // Add the CopyDepthToDepthMsaa task after the DepthBias task
        const pxr::SdfPath depthBiasTaskPath = instance1.sceneFramePass->GetTaskManager()->GetTaskPath(
            hvt::DepthBiasTask::GetToken());

        instance1.sceneFramePass->GetTaskManager()->AddTask<hvt::CopyDepthToDepthMsaaTask>(
            hvt::CopyDepthToDepthMsaaTask::GetToken(), hvt::CopyDepthToDepthMsaaTaskParams(), 
            fnCommitCopyDepth, depthBiasTaskPath,
            hvt::TaskManager::InsertionOrder::insertAfter);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 100;

    auto render = [&]()
    {
        // Updates the first frame pass.
        {
            auto& params = instance1.sceneFramePass->params();

            params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace          = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor     = TestHelpers::ColorDarkGrey;
            params.selectionColor      = TestHelpers::ColorYellow;
            params.msaaSampleCount     = 4;
            params.enableMultisampling = true;

            // Clears the backgrounds.
            params.clearBackgroundColor = true;
            params.clearBackgroundDepth = true;

            // Do not display right now, wait for the second frame pass.
            params.enablePresentation = false;

            instance1.sceneFramePass->Render();
        }

        // Gets the input AOV's from the first frame pass and use them in all overlays so the
        // overlay's draw into the same color and depth buffers.

        pxr::HdRenderBuffer* colorBuffer =
            instance1.sceneFramePass->GetRenderBuffer(pxr::HdAovTokens->color);

        pxr::HdRenderBuffer* depthBuffer =
            instance1.sceneFramePass->GetRenderBuffer(pxr::HdAovTokens->depth);

        const std::vector<std::pair<pxr::TfToken const&, pxr::HdRenderBuffer*>> inputAOVs = {
            { pxr::HdAovTokens->color, colorBuffer }, { pxr::HdAovTokens->depth, depthBuffer }
        };

        // Updates the second frame pass.
        {
            auto& params = instance2.sceneFramePass->params();

            params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            // need to use the same view parameters as the first frame to not clip stuff
            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();

            params.viewInfo.lights   = stage2.defaultLights();
            params.viewInfo.material = stage2.defaultMaterial();
            params.viewInfo.ambient  = stage2.defaultAmbient();

            params.colorspace          = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor     = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor      = TestHelpers::ColorYellow;
            params.msaaSampleCount     = 4;
            params.enableMultisampling = true;

            // Do not clear the backgrounds as they contain the previous frame pass result.
            params.clearBackgroundColor = false;
            params.clearBackgroundDepth = false;

            // Gets the list of tasks to render but use the render buffers from the main frame pass.
            const pxr::HdTaskSharedPtrVector renderTasks =
                instance2.sceneFramePass->GetRenderTasks(inputAOVs);

            instance2.sceneFramePass->Render(renderTasks);
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).
    context->run(render, instance1.sceneFramePass.get());

    // Validates the rendering result.
    const std::string imageFile { test_info_->name() };
    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile, 1));
}
