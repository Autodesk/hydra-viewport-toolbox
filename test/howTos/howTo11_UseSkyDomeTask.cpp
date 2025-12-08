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
#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>

//
// How to use the SkyDome render task?
//
#if defined(__ANDROID__) || (TARGET_OS_IPHONE == 1)
HVT_TEST(howTo, DISABLED_useSkyDomeTask)
#else
HVT_TEST(howTo, useSkyDomeTask)
#endif
{
    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

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

        // Adds the 'SkyDome' task to the frame pass.

        {
            // Get the first render task path.

            auto renderTasks =
                sceneFramePass->GetTaskManager()->GetTasks(hvt::TaskFlagsBits::kRenderTaskBit);
            ASSERT_FALSE(renderTasks.empty());
            SdfPath firstRenderTaskPath = renderTasks[0]->GetId();

            // Define a getter for the layer settings.

            const auto getLayerSettings = [&sceneFramePass]() -> hvt::BasicLayerParams const* {
                return &sceneFramePass->params();
            };

            // Create the SkyDomeTask and insert it before the first existing render task.

            hvt::CreateSkyDomeTask(sceneFramePass->GetTaskManager(),
                sceneFramePass->GetRenderBufferAccessor(), getLayerSettings, firstRenderTaskPath,
                hvt::TaskManager::InsertionOrder::insertBefore);
        }
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).

    int frameCount = 10;

    GlfSimpleLightVector lights = stage.defaultLights();

    // Add a dome light to the default stage lights.
    // This dome light is required to activate the SkyDome.

    GlfSimpleLight domeLight;
    domeLight.SetID(SdfPath("DomeLight"));
    domeLight.SetIsDomeLight(true);
    lights.push_back(domeLight);

    auto render = [&]() {
        // Updates the main frame pass.

        auto& params = sceneFramePass->params();

        params.renderBufferSize  = GfVec2i(context->width(), context->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = lights;
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        sceneFramePass->Render();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, sceneFramePass.get());

    // Validates the rendering result.

    // WebGPU & Linux needs a small threshold to use baseline images.
    ASSERT_TRUE(context->validateImages(computedImageName, imageFile, 20));
}
