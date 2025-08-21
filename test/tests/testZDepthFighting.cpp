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

#include <hvt/tasks/depthBiasTask.h>
#include <hvt/tasks/resources.h>

#include <gtest/gtest.h>

TEST(engine, ZDepthFightingTest1)
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

    // By default, the two scenes are rendered with a 'z-depth fighting' issue as the red
    // rectangle is rendered slightly in front of the blue rectangle. But the depth bias task is
    // used to push the red rectangle a little bit away from the blue rectangle.

    // Case 1: Do not add the depth bias task to see the 'z-depth fighting' issue.
    // Case 2: Add the depth bias task but disable it to see the 'z-depth fighting' issue.
    // Case 3: Add the depth bias task and enable it while keeping default values, to see the
    // 'z-depth' issue.
    // Case 4: Add the depth bias task and enable it with some specific values to fix the 'z-depth
    // fighting' issue.

    {
        // Defines the 'Depth Bias' task update function.

        auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const pxr::VtValue value        = fnGetValue(pxr::HdTokens->params);
            hvt::DepthBiasTaskParams params = value.Get<hvt::DepthBiasTaskParams>();

            // false to see the 'z-depth fighting' issue.
            params.depthBiasEnable = false;

            // -0.05f to see the red and 0.05 to see the blue.
            params.depthBiasConstantFactor  = -0.05f;
            params.depthBiasSlopeFactor     = 1.0f;

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
    int frameCount = 10;

    auto render = [&]()
    {
        // Updates the first frame pass.

        {
            auto& params = instance1.sceneFramePass->params();

            params.renderBufferSize  = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.viewport = { { 0, 0 }, { context->width(), context->height() } };

            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace      = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;
            
            // Clears the backgrounds.
            params.clearBackgroundColor = true;
            params.clearBackgroundDepth = true;

            instance1.sceneFramePass->Render();
        }

        // Updates the second frame pass.

        {
            auto& params = instance2.sceneFramePass->params();

            params.renderBufferSize  = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.viewport = { { 0, 0 }, { context->width(), context->height() } };

            params.viewInfo.viewMatrix       = stage2.viewMatrix();
            params.viewInfo.projectionMatrix = stage2.projectionMatrix();
            params.viewInfo.lights           = stage2.defaultLights();
            params.viewInfo.material         = stage2.defaultMaterial();
            params.viewInfo.ambient          = stage2.defaultAmbient();

            params.colorspace      = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            // Do not clear the backgrounds as they contain the previous frame pass result.
            params.clearBackgroundColor = true;
            params.clearBackgroundDepth = true;

            instance2.sceneFramePass->Render();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, instance1.sceneFramePass.get());

    // Validates the rendering result.

    const std::string imageFile = std::string(test_info_->test_suite_name()) + std::string("/") +
        std::string(test_info_->name());
    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile, 1));
}

TEST(engine, ZDepthFightingTest2)
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

    // By default, the two scenes are rendered with a 'z-depth fighting' issue as the red
    // rectangle is rendered slightly in front of the blue rectangle. But the depth bias task is
    // used to push the red rectangle a little bit away from the blue rectangle.

    // Case 1: Do not add the depth bias task to see the 'z-depth fighting' issue.
    // Case 2: Add the depth bias task but disable it to see the 'z-depth fighting' issue.
    // Case 3: Add the depth bias task and enable it while keeping default values, to see the
    // 'z-depth' issue.
    // Case 4: Add the depth bias task and enable it with some specific values to fix the 'z-depth
    // fighting' issue.

    {
        // Defines the 'Depth Bias' task update function.

        auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const pxr::VtValue value        = fnGetValue(pxr::HdTokens->params);
            hvt::DepthBiasTaskParams params = value.Get<hvt::DepthBiasTaskParams>();

            params.depthBiasEnable = false;

            params.depthBiasConstantFactor  = 0.50f;
            params.depthBiasSlopeFactor     = 1.0f;

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
    int frameCount = 10;

    auto render = [&]()
    {
        // Updates the first frame pass.

        {
            auto& params = instance1.sceneFramePass->params();

            params.renderBufferSize  = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.viewport = { { 0, 0 }, { context->width(), context->height() } };

            params.viewInfo.viewMatrix       = stage1.viewMatrix();
            params.viewInfo.projectionMatrix = stage1.projectionMatrix();
            params.viewInfo.lights           = stage1.defaultLights();
            params.viewInfo.material         = stage1.defaultMaterial();
            params.viewInfo.ambient          = stage1.defaultAmbient();

            params.colorspace      = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;
            
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

            params.renderBufferSize  = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.viewport = { { 0, 0 }, { context->width(), context->height() } };

            params.viewInfo.viewMatrix       = stage2.viewMatrix();
            params.viewInfo.projectionMatrix = stage2.projectionMatrix();
            params.viewInfo.lights           = stage2.defaultLights();
            params.viewInfo.material         = stage2.defaultMaterial();
            params.viewInfo.ambient          = stage2.defaultAmbient();

            params.colorspace      = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor  = TestHelpers::ColorYellow;

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

    const std::string imageFile = std::string(test_info_->test_suite_name()) + std::string("/") +
        std::string(test_info_->name());
    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile, 1));
}
