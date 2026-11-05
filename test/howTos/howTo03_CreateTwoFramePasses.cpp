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

#include <pxr/base/plug/registry.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/usd/usdGeom/gprim.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>

//
// How to create two frame passes?
//

// Note: Android and iOS not supported for now.
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(howTo, DISABLED_createTwoFramePasses)
#else
HVT_TEST(howTo, createTwoFramePasses)
#endif
{
    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();

    // Loads the main scene and an arbitrary USD asset (i.e., a cube) to display as an overlay.

    TestHelpers::TestStage mainStage(context->_backend);
    TestHelpers::TestStage overlayStage(context->_backend);

    ASSERT_TRUE(mainStage.open(context->_sceneFilepath));
    ASSERT_TRUE(overlayStage.open(TestHelpers::ResolveAssetPath("usd/cube.usda").string()));

    {
        // The cube asset has inconsistent face winding, so render it double-sided to keep every
        // face lit, and tilt it so it reads as a 3D cube.
        pxr::UsdGeomGprim cube(
            overlayStage.stage()->GetPrimAtPath(pxr::SdfPath("/Root/SimpleCube")));
        ASSERT_TRUE(cube);
        cube.CreateDisplayColorAttr().Set(pxr::VtVec3fArray({ pxr::GfVec3f(0.15f, 0.85f, 1.0f) }));
        cube.CreateDoubleSidedAttr().Set(true);
        cube.AddRotateXYZOp().Set(pxr::GfVec3f(30.0f, 45.0f, 0.0f));

        // Re-frames the overlay camera on the rotated cube.
        overlayStage.updateCameraAndLights(overlayStage.computeStageBounds());
    }

    struct FramePassInstances
    {
        hvt::RenderIndexProxyPtr renderIndex;
        hvt::FramePassPtr sceneFramePass;
    } mainFramePass, overlayFramePass;

    // Defines the main frame pass i.e., the one containing the scene to display.

    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(mainFramePass.renderIndex, renderDesc);

        // Creates the scene index containing the model.

        pxr::HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(mainStage.stage());
        mainFramePass.renderIndex->RenderIndex()->InsertSceneIndex(
            sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex         = mainFramePass.renderIndex->RenderIndex();
        passDesc.uid                 = pxr::SdfPath("/sceneFramePass");
        mainFramePass.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Defines a secondary frame pass containing the overlay.

    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(overlayFramePass.renderIndex, renderDesc);

        // Creates the scene index containing the overlay model.

        pxr::HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(overlayStage.stage());
        overlayFramePass.renderIndex->RenderIndex()->InsertSceneIndex(
            sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex            = overlayFramePass.renderIndex->RenderIndex();
        passDesc.uid                    = pxr::SdfPath("/overlayFramePass");
        overlayFramePass.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
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

            params.viewInfo.viewMatrix       = mainStage.viewMatrix();
            params.viewInfo.projectionMatrix = mainStage.projectionMatrix();
            params.viewInfo.lights           = mainStage.defaultLights();
            params.viewInfo.material         = mainStage.defaultMaterial();
            params.viewInfo.ambient          = mainStage.defaultAmbient();

            // No color management so both frame passes write the same color space.
            params.colorspace      = pxr::HdxColorCorrectionTokens->disabled;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            // Do not display right now, wait for the second frame pass.
            params.enablePresentation = false;

            mainFramePass.sceneFramePass->Render();

            // Force GPU sync. Wait for all GPU commands to complete before proceeding.
            // This ensures render operations are fully finished before the next frame
            // or validation step, preventing race conditions and ensuring consistent results.
            context->_backend->waitForGPUIdle();
        }

        // Gets the color AOV from the main frame pass so the overlay draws on top of it. The depth
        // AOV is not shared because the overlay uses its own camera, so its depth values are not
        // comparable with the main scene ones.

        auto& pass = mainFramePass.sceneFramePass;
        hvt::RenderBufferBindings inputAOVs =
            pass->GetRenderBufferBindingsForNextPass({ pxr::HdAovTokens->color });

        // Updates the overlay frame pass.

        {
            // Defines arbitrary dimensions and position for the overlay.
            const int width  = context->width() / 4;
            const int height = context->height() / 4;
            const int posX   = 10;
            const int posY   = 10;

            auto& params = overlayFramePass.sceneFramePass->params();

            params.renderBufferSize = pxr::GfVec2i(context->width(), context->height());
            params.viewInfo.framing = hvt::ViewParams::GetDefaultFraming(posX, posY, width, height);

            // The overlay uses its own camera, framed on the cube.
            params.viewInfo.viewMatrix       = overlayStage.viewMatrix();
            params.viewInfo.projectionMatrix = overlayStage.projectionMatrix();
            params.viewInfo.lights           = overlayStage.defaultLights();
            params.viewInfo.material         = overlayStage.defaultMaterial();
            params.viewInfo.ambient          = overlayStage.defaultAmbient();

            params.colorspace = pxr::HdxColorCorrectionTokens->disabled;

            // Do not clear the color as it contains the previous frame pass result.
            params.clearBackgroundColor = false;
            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor       = TestHelpers::ColorYellow;

            params.enablePresentation = context->presentationEnabled();

            // Gets the list of tasks to render but use the color buffer from the main frame pass.
            const pxr::HdTaskSharedPtrVector renderTasks =
                overlayFramePass.sceneFramePass->GetRenderTasks(inputAOVs);

            overlayFramePass.sceneFramePass->Render(renderTasks);
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, overlayFramePass.sceneFramePass.get());

    // Validates the rendering result.

    ASSERT_TRUE(context->validateImages(computedImageName, imageFile));
}
