//
// Copyright 2025 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/tasks/blurTask.h>
#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

//
// How to create a custom render task?
//
// FIXME: The result image is not stable between runs on macOS, so this test is temporarily not
// executed on that platform.
#if defined(__APPLE__)
TEST(HowTo, DISABLED_CreateACustomRenderTask)
#else
TEST(HowTo, CreateACustomRenderTask)
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
                                hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
                    const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
                    hvt::BlurTaskParams params =
                        value.Get<hvt::BlurTaskParams>();
                    params.blurAmount = app.blur;
                    fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
                };

            // Adds the blur task i.e., 'blurTask' before the color correction one.

            const pxr::SdfPath colorCorrectionTask = sceneFramePass->GetTaskManager()->GetTaskPath(
                pxr::HdxPrimitiveTokens->colorCorrectionTask);

            const pxr::SdfPath blurPath =
                sceneFramePass->GetTaskManager()->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(),
                    fnCommit, colorCorrectionTask, hvt::TaskManager::InsertionOrder::insertBefore);

            // Sets the default value.

            hvt::BlurTaskParams blurParams;
            blurParams.blurAmount = app.blur;
            sceneFramePass->GetTaskManager()->SetTaskValue(
                blurPath, pxr::HdTokens->params, pxr::VtValue(blurParams));
        }
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        auto& params = sceneFramePass->params();

        params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());

        params.viewInfo.viewport         = { { 0, 0 }, { context->width(), context->height() } };
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
