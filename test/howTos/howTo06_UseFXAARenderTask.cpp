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

#include <hvt/testFramework/testContextCreator.h>

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/fxaaTask.h>

#include <gtest/gtest.h>

//
// How to use the FXAA render task?
//
#if defined(__APPLE__)
TEST(howTo, DISABLED_useFXAARenderTask)
#else
TEST(howTo, useFXAARenderTask)
#endif
{
    // Helper to create the Hgi implementation.

    auto context = hvt::TestFramework::CreateTestContext();

    hvt::TestFramework::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Overdo FXAA resolution to produce image that cleary shows the effect.
    static constexpr float fxaaResolution = 0.02f;

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

        // Adds the 'FXAA' custom task to the frame pass.

        {
            // Defines the anti-aliasing task update function.

            auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                                hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
                const VtValue value        = fnGetValue(HdTokens->params);
                hvt::FXAATaskParams params = value.Get<hvt::FXAATaskParams>();
                params.resolution          = fxaaResolution;
                fnSetValue(HdTokens->params, VtValue(params));
            };

            // Adds the anti-aliasing task i.e., 'fxaaTask'.

            const SdfPath colorCorrectionTask = sceneFramePass->GetTaskManager()->GetTaskPath(
                HdxPrimitiveTokens->colorCorrectionTask);

            // Note: Inserts the FXAA render task into the task list after color correction.

            sceneFramePass->GetTaskManager()->AddTask<hvt::FXAATask>(TfToken("fxaaTask"),
                hvt::FXAATaskParams(), fnCommit, colorCorrectionTask,
                hvt::TaskManager::InsertionOrder::insertAfter);
        }
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        auto& params = sceneFramePass->params();

        params.viewInfo.viewport = { { 0, 0 }, { context->width(), context->height() } };
        params.renderBufferSize  = GfVec2i(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = hvt::TestFramework::ColorDarkGrey;
        params.selectionColor  = hvt::TestFramework::ColorYellow;

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
