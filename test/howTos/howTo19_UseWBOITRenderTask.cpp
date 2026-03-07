// Copyright 2026 Autodesk, Inc.
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

#include <RenderingFramework/TestFlags.h>
#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/viewportEngine.h>

#include <pxr/pxr.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

//
// How to use the WBOIT (Weighted Blended Order-Independent Transparency) render task?
//
// WBOIT is an alternative to the default linked-list OIT for rendering translucent geometry.
// It approximates correct transparency ordering using weighted blending, which is faster and
// uses less memory than per-pixel linked lists, but may produce artifacts with overlapping
// surfaces of similar depth.
//
// To enable WBOIT, set TaskCreationOptions::useWbOit = true in the FramePassDescriptor before
// creating the frame pass. The CreateDefaultTasks helper will then use WbOitRenderTask and
// WbOitResolveTask instead of HdxOitRenderTask and HdxOitResolveTask.
//

HVT_TEST(howTo, useWBOITRenderTask)
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(
        (TestHelpers::getAssetsDataFolder() / "usd/translucent_cube.usda").string()));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Step 1: Create the renderer and scene index as usual.

    {
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Enable WBOIT through the FramePassDescriptor's TaskCreationOptions.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex                    = renderIndex->RenderIndex();
        passDesc.uid                            = SdfPath("/FramePass");
        passDesc.taskCreationOptions.useWbOit   = true;

        sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Step 2: Render normally. The WBOIT pipeline is fully integrated with the standard
    // frame pass rendering.

    int frameCount = 10;
    auto render    = [&]()
    {
        auto& params = sceneFramePass->params();

        params.renderBufferSize  = GfVec2i(context->width(), context->height());
        params.viewInfo.framing  =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace             = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor        = TestHelpers::ColorDarkGrey;
        params.selectionColor         = TestHelpers::ColorYellow;

        params.enablePresentation     = context->presentationEnabled();

        sceneFramePass->Render();
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    context->run(render, sceneFramePass.get());

    ASSERT_TRUE(context->validateImages(computedImageName, imageFile));
}
