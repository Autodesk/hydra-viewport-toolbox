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

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/ssaoTask.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>

//
// How to use the SSAO render task?
//

// OGSMOD-8067 - Disabled for Android due to baseline inconsistency between runs.
#if defined(__ANDROID__)
HVT_TEST(howTo, DISABLED_useSSAORenderTask)
#else
HVT_TEST(howTo, useSSAORenderTask)
#endif
{
    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Defines the application parameters.

    struct AppParams
    {
        hvt::AmbientOcclusionProperties ao;
    } app;

    // Defines the main frame pass i.e., the one containing the scene to display.

    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/FramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Adds ssao custom task to the frame pass

    {
        // Defines ssao task update function

        auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
            const VtValue value        = fnGetValue(HdTokens->params);
            hvt::SSAOTaskParams params = value.Get<hvt::SSAOTaskParams>();
            params.ao                  = app.ao;

            auto renderParams                = sceneFramePass->params().renderParams;
            params.view.cameraID             = renderParams.camera;
            params.view.framing              = renderParams.framing;
            params.view.overrideWindowPolicy = renderParams.overrideWindowPolicy;

            params.ao.isEnabled         = true;
            params.ao.isShowOnlyEnabled = true;
            params.ao.amount            = 2.0f;
            params.ao.sampleRadius      = 10.0f;

            fnSetValue(HdTokens->params, VtValue(params));
        };

        // Adds the ssao task i.e., 'ssaoTask' before the color correction one.

        const SdfPath colorCorrectionTask =
            sceneFramePass->GetTaskManager()->GetTaskPath(HdxPrimitiveTokens->colorCorrectionTask);

        sceneFramePass->GetTaskManager()->AddTask<hvt::SSAOTask>(hvt::SSAOTask::GetToken(),
            hvt::SSAOTaskParams(), fnCommit, colorCorrectionTask,
            hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        auto& params = sceneFramePass->params();

        params.renderBufferSize  = GfVec2i(context->width(), context->height());
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

        // Renders the render tasks.
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
