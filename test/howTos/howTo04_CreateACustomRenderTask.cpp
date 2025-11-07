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

#include <RenderingFramework/TestFlags.h>

//
// How to create a custom render task?
//

// OGSMOD-8067 - Disabled for Android due to baseline inconsistency between runs
#if defined(__ANDROID__)
HVT_TEST(howTo, DISABLED_createACustomRenderTask)
#else
HVT_TEST(howTo, createACustomRenderTask)
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

            // Adds the blur task.

            const pxr::SdfPath pos =
                sceneFramePass->GetTaskManager()->GetTaskPath(pxr::HdxPrimitiveTokens->presentTask);

            sceneFramePass->GetTaskManager()->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(),
                hvt::BlurTaskParams(), fnCommit, pos,
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

        params.colorspace      = pxr::HdxColorCorrectionTokens->disabled;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        sceneFramePass->Render();

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, sceneFramePass.get());

    // Validates the rendering result.

    ASSERT_TRUE(context->validateImages(computedImageName, imageFile));
}