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

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/blurTask.h>

#include <gtest/gtest.h>

//
// How to create a custom render task?
//
// TODO: The result image is not stable between runs on macOS, skip on that platform for now
// Disabled for Android due to baseline inconsistancy between runners. Refer to OGSMOD-8067
#if defined(__APPLE__) || defined(__ANDROID__)
TEST(howTo, DISABLED_createACustomRenderTask)
#else
TEST(howTo, createACustomRenderTask)
#endif
{
    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Lets define the application parameters.
    struct AppParams
    {
        float blur { 8.0f };
        //...
    } app;

    // Defines the main frame pass i.e., the one containing the scene to display.

    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Creates the scene index containing the model.

        pxr::HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = pxr::SdfPath("/FramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);

        // Adds the 'blur' custom task to the frame pass.

        {
            // Defines the blur task update function.

            auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                                hvt::TaskManager::SetTaskValueFn const& fnSetValue)
            {
                const pxr::VtValue value   = fnGetValue(pxr::HdTokens->params);
                hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
                params.blurAmount          = app.blur;
                fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
            };

            // Adds the blur task i.e., 'blurTask' before the color correction one.

            const pxr::SdfPath colorCorrectionTask = sceneFramePass->GetTaskManager()->GetTaskPath(
                pxr::HdxPrimitiveTokens->colorCorrectionTask);

            sceneFramePass->GetTaskManager()->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(),
                hvt::BlurTaskParams(), fnCommit, colorCorrectionTask,
                hvt::TaskManager::InsertionOrder::insertBefore);
        }
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        // Updates the main frame pass.

        auto& params = sceneFramePass->params();

        params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        // Adding a color space automatically enables the color correction task.
        params.colorspace = pxr::HdxColorCorrectionTokens->sRGB;

        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        sceneFramePass->Render();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, sceneFramePass.get());

    // Validates the rendering result.

    const std::string imageFile = std::string(test_info_->test_suite_name()) + std::string("/") +
        std::string(test_info_->name());

    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile));
}