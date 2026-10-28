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

#include <pxr/pxr.h>

#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/tokens.h>

#include <RenderingFramework/TestFlags.h>
#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/greyScaleTask.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

//
// How to use the grey-scale render task?
//

// OGSMOD-8067 - Inconsistency between runs on Android.
#if defined(__ANDROID__)
HVT_TEST(howTo, DISABLED_useGreyScaleRenderTask)
#else
HVT_TEST(howTo, useGreyScaleRenderTask)
#endif
{
    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    {
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/FramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);

        {
            const SdfPath presentTask =
                sceneFramePass->GetTaskManager()->GetTaskPath(HdxPrimitiveTokens->presentTask);

            sceneFramePass->GetTaskManager()->AddTask<hvt::GreyScaleTask>(
                hvt::GreyScaleTask::GetToken(), hvt::GreyScaleTaskParams(), nullptr, presentTask,
                hvt::TaskManager::InsertionOrder::insertBefore);
        }
    }

    int frameCount = 10;

    auto render = [&]()
    {
        auto& params = sceneFramePass->params();

        params.renderBufferSize = GfVec2i(context->width(), context->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->disabled;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        sceneFramePass->Render();

        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    context->run(render, sceneFramePass.get());

    constexpr uint8_t threshold            = 1;
    constexpr uint16_t pixelCountThreshold = 100;
    ASSERT_TRUE(
        context->validateImages(computedImageName, imageFile, threshold, pixelCountThreshold));
}
