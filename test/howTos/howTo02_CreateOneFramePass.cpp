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

#include <hvt/testFramework/testContextCreator.h>

#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

//
// How to create one frame pass using Storm?
//
TEST(howTo, createOneFramePass)
{
    // Helper to create the Hgi implementation.

    auto context = hvt::TestFramework::CreateTestContext();

    hvt::TestFramework::TestStage stage(context->_backend);
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

        pxr::HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = pxr::SdfPath("/sceneFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        // Updates the main frame pass.

        auto& params = sceneFramePass->params();

        params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());

        params.viewInfo.viewport         = { { 0, 0 }, { context->width(), context->height() } };
        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = pxr::HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = hvt::TestFramework::ColorDarkGrey;
        params.selectionColor  = hvt::TestFramework::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        sceneFramePass->Render();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, sceneFramePass.get());

    // Validates the rendering result.

    const std::string imageFile = std::string(test_info_->test_suite_name()) +
        std::string("/") + std::string(test_info_->name());

    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile));
}
