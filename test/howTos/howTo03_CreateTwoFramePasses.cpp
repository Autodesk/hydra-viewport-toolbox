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
#include <hvt/tasks/resources.h>

#include <pxr/base/plug/registry.h>

#include <gtest/gtest.h>

//
// How to create two frame passes?
//

// TODO: Vulkan  introduces result inconsistencies.
// TODO: Linux result image could have one pixel difference between runs. Refer to OGSMOD-6304
// NOTE: With 'origin/dev', the tripod axis is too far (or too small). That's fixed in 'adsk/dev' . 
// NOTE: It turns out "axisTripod.usda" has coplanar geometry and it can create random
//       inconsistencies on all platforms. Refer to OGSMOD-6304.
#if (TARGET_OS_IPHONE == 1) || (defined(_WIN32) && defined(ENABLE_VULKAN)) || defined(__linux__) || !defined(ADSK_OPENUSD_PENDING)
TEST(howTo, DISABLED_createTwoFramePasses)
#else
TEST(howTo, DISABLED_createTwoFramePasses)
#endif
{
    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);

    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    struct FramePassInstances
    {
        hvt::RenderIndexProxyPtr renderIndex;
        hvt::FramePassPtr sceneFramePass;
    } mainFramePass, manipulatorFramePass;

    // Defines the main frame pass i.e., the one containing the scene to display.

    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(mainFramePass.renderIndex, renderDesc);

        // Creates the scene index containing the model.

        pxr::HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        mainFramePass.renderIndex->RenderIndex()->InsertSceneIndex(
            sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex         = mainFramePass.renderIndex->RenderIndex();
        passDesc.uid                 = pxr::SdfPath("/sceneFramePass");
        mainFramePass.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Defines a secondary frame pass containing a manipulator.

    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(manipulatorFramePass.renderIndex, renderDesc);

        // Loads an arbitrary USD asset e.g., a manipulator in this case.

        auto manipulatorStage = hvt::ViewportEngine::CreateStageFromFile(
            hvt::GetGizmoPath("axisTripod.usda").generic_u8string());

        // Creates the scene index containing the model.

        pxr::HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(manipulatorStage);
        manipulatorFramePass.renderIndex->RenderIndex()->InsertSceneIndex(
            sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex                = manipulatorFramePass.renderIndex->RenderIndex();
        passDesc.uid                        = pxr::SdfPath("/sceneFramePass");
        manipulatorFramePass.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        // Updates the main frame pass.

        {
            auto& params = mainFramePass.sceneFramePass->params();

            params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = pxr::HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            // Do not display right now, wait for the second frame pass.
            params.enablePresentation = false;

            mainFramePass.sceneFramePass->Render();
        }

        // Gets the input AOV's from the first frame pass and use them in all overlays so the
        // overlay's draw into the same color and depth buffers.

        auto& pass = mainFramePass.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs = pass->GetRenderBufferBindingsForNextPass(
            { pxr::HdAovTokens->color, pxr::HdAovTokens->depth });

        // Updates the manipulator frame pass.

        {

            // Defines arbitrary dimensions for the manipulator.
            const int width  = context->width() / 4;
            const int height = context->height() / 4;

            // Defines arbitrary positions for the manipulator.
            const int posX = 10;
            const int posY = 10;

            auto& params = manipulatorFramePass.sceneFramePass->params();

            params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(posX, posY, width, height);

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            // No color management on gizmos.
            params.colorspace = pxr::HdxColorCorrectionTokens->disabled;

            // Do not clear the background as it contains the previous frame pass result.
            params.clearBackgroundColor = false;
            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor       = TestHelpers::ColorYellow;

            // Gets the list of tasks to render but use the render buffers from the main frame pass.
            const pxr::HdTaskSharedPtrVector renderTasks =
                manipulatorFramePass.sceneFramePass->GetRenderTasks(inputAOVs);

            manipulatorFramePass.sceneFramePass->Render(renderTasks);
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, mainFramePass.sceneFramePass.get());

    // Validates the rendering result.

    const std::string imageFile = std::string(test_info_->test_suite_name()) + std::string("/") +
        std::string(test_info_->name());
    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile, 1));
}
