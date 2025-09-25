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

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/engine/taskUtils.h>
#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>

//
// How to manually create the default list of tasks?
//
HVT_TEST(howTo, createDefaultListOfTasks)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr framePass;

    // Defines a frame pass.

    {
        // Creates the render index.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        //------------------------------------------------------------------------------------------
        //

        // Creates the frame pass instance.

        hvt::FramePassDescriptor frameDesc;
        frameDesc.renderIndex = renderIndex->RenderIndex();
        frameDesc.uid         = SdfPath("/sceneFramePass");

        // Manually creates the default list of tasks.

        framePass = std::make_unique<hvt::FramePass>(frameDesc.uid.GetText());
        framePass->Initialize(frameDesc);
        framePass->CreatePresetTasks(hvt::FramePass::PresetTaskLists::Default);

        //
        //------------------------------------------------------------------------------------------
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        {
            auto& params = framePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            framePass->Render();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass.get());

    // Validates the rendering result against normally created default list of tasks.

    const std::string imageFile = 
        TestHelpers::gTestNames.suiteName + std::string("/") + std::string("createOneFramePass");

    const std::string computedImageName = TestHelpers::appendParamToImageFile(imageFile);

    ASSERT_TRUE(context->_backend->saveImage(computedImageName));

    ASSERT_TRUE(context->_backend->compareImage(computedImageName, imageFile));
}

//
// How to manually create the default list of tasks?
//
HVT_TEST(howTo, createDefaultListOfTasks2)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr framePass;

    // Defines a frame pass.

    {
        // Creates the render index.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        //------------------------------------------------------------------------------------------
        //

        // Creates the frame pass instance.

        hvt::FramePassDescriptor frameDesc;
        frameDesc.renderIndex = renderIndex->RenderIndex();
        frameDesc.uid         = SdfPath("/sceneFramePass");

        // Manually creates the default list of tasks.

        framePass = std::make_unique<hvt::FramePass>(frameDesc.uid.GetText());
        framePass->Initialize(frameDesc);

        // Note: When the render delegate is Storm, the creation is as below.

        ASSERT_TRUE(hvt::IsStormRenderDelegate(renderIndex->RenderIndex()));

        const auto getLayerSettings = [&framePass]() -> hvt::BasicLayerParams const* {
            return &framePass->params();
        };

        hvt::CreateDefaultTasks(framePass->GetTaskManager(), framePass->GetRenderBufferAccessor(),
            framePass->GetLightingAccessor(), framePass->GetSelectionSettingsAccessor(),
            getLayerSettings);

        //
        //------------------------------------------------------------------------------------------
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        {
            auto& params = framePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            framePass->Render();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass.get());

    // Validates the rendering result against normally created default list of tasks.

    const std::string imageFile = 
        TestHelpers::gTestNames.suiteName + std::string("/") + std::string("createOneFramePass");

    const std::string computedImageName = TestHelpers::appendParamToImageFile(imageFile);

    ASSERT_TRUE(context->_backend->saveImage(computedImageName));

    ASSERT_TRUE(context->_backend->compareImage(computedImageName, imageFile));
}

//
// How to manually create the minimal list of tasks?
//
HVT_TEST(howTo, createMinimalListOfTasks)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr framePass;

    // Defines a frame pass.

    {
        // Creates the render index.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        //------------------------------------------------------------------------------------------
        //

        // Creates the frame pass instance.

        hvt::FramePassDescriptor frameDesc;
        frameDesc.renderIndex = renderIndex->RenderIndex();
        frameDesc.uid         = SdfPath("/sceneFramePass");

        // Manually creates the minimal list of tasks.

        framePass = std::make_unique<hvt::FramePass>(frameDesc.uid.GetText());
        framePass->Initialize(frameDesc);

        const auto getLayerSettings = [&framePass]() -> hvt::BasicLayerParams const* {
            return &framePass->params();
        };

        // Creates the minimal list of tasks to render.
        hvt::CreateMinimalTasks(framePass->GetTaskManager(), framePass->GetRenderBufferAccessor(),
            framePass->GetLightingAccessor(), getLayerSettings);

        //
        //------------------------------------------------------------------------------------------
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        {
            auto& params = framePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            framePass->Render();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, framePass.get());

    // Validates the rendering result.

    const std::string imageFile =
        TestHelpers::gTestNames.suiteName + std::string("/") + TestHelpers::gTestNames.fixtureName;

    const std::string computedImageName = TestHelpers::appendParamToImageFile(imageFile);

    ASSERT_TRUE(context->_backend->saveImage(computedImageName));

    ASSERT_TRUE(context->_backend->compareImage(computedImageName, imageFile));
}
