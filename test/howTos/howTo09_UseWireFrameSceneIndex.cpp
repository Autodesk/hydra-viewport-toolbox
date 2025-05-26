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
#include <hvt/sceneIndex/displayStyleOverrideSceneIndex.h>
#include <hvt/sceneIndex/wireFrameSceneIndex.h>
#include <hvt/tasks/composeTask.h>

#include <pxr/imaging/hd/utils.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>

#include <gtest/gtest.h>

// FIXME: Android unit test framework does not report the error message, make it impossible to fix
// issues. Refer to OGSMOD-5546.
//
// FIXME: wireframe does not work on macOS/Metal.
// Refer to https://forum.aousd.org/t/hdstorm-mesh-wires-drawing-issue-in-usd-24-05-on-macos/1523
//
#if defined(__ANDROID__) || defined(__APPLE__)
TEST(howTo, DISABLED_useWireFrameCollectionRepr)
#else
TEST(howTo, useWireFrameCollectionRepr)
#endif
{
    // This unit test demonstrates how display a wire frame of the model using the collection
    // representation.

    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Defines the main frame pass i.e., the one containing the scene to display.

    {
        // Step 1 - Creates the render index by providing the hgi driver and the requested render
        // name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Step 2 - Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Step 3 - Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/sceneFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Renders 10 times (i.e., arbitrary number to guaranty best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        {
            auto& params = sceneFramePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());

            params.viewInfo.viewport   = { { 0, 0 }, { context->width(), context->height() } };
            params.viewInfo.viewMatrix = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            params.enablePresentation = context->presentationEnabled();

            // Changes the geometry representation.
            params.collection =
                HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->wire));

            sceneFramePass->Render();
        }

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

// FIXME: Android unit test framework does not report the error message, make it impossible to fix
// issues. Refer to OGSMOD-5546.
//
// FIXME: wireframe does not work on macOS/Metal.
// Refer to https://forum.aousd.org/t/hdstorm-mesh-wires-drawing-issue-in-usd-24-05-on-macos/1523
//
#if defined(__ANDROID__) || defined(__APPLE__)
TEST(howTo, DISABLED_useWireFrameSceneIndex)
#else
TEST(howTo, useWireFrameSceneIndex)
#endif
{
    // This unit test demonstrates how display a wire frame of the model using a scene index
    // filtering.

    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;
    HdSceneIndexBaseRefPtr sceneIndex;

    // Defines the main frame pass i.e., the one containing the scene to display.

    {
        // Step 1 - Creates the render index by providing the hgi driver and the requested render
        // name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Step 2 - Adds the 'wireframe' scene index.

        sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        sceneIndex = hvt::DisplayStyleOverrideSceneIndex::New(sceneIndex);
        sceneIndex = hvt::WireFrameSceneIndex::New(sceneIndex);

        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Step 3 - Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/sceneFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        {
            auto& params = sceneFramePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());

            params.viewInfo.viewport   = { { 0, 0 }, { context->width(), context->height() } };
            params.viewInfo.viewMatrix = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            params.enablePresentation = context->presentationEnabled();

            sceneFramePass->Render();
        }

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
