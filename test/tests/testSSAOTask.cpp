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

#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/ssaoTask.h>

#include <pxr/pxr.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

// SSAO render task with color correction disabled.
#if defined(__ANDROID__)
HVT_TEST(TestSSAOTask, DISABLED_ssao_withoutColorCorrection)
#else
HVT_TEST(TestSSAOTask, ssao_withoutColorCorrection)
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
    }

    // Adds ssao custom task to the frame pass before the color correction task.
    {
        auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
            const VtValue value        = fnGetValue(HdTokens->params);
            hvt::SSAOTaskParams params = value.Get<hvt::SSAOTaskParams>();
     
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

        const SdfPath colorCorrectionTask =
            sceneFramePass->GetTaskManager()->GetTaskPath(HdxPrimitiveTokens->colorCorrectionTask);

        sceneFramePass->GetTaskManager()->AddTask<hvt::SSAOTask>(hvt::SSAOTask::GetToken(),
            hvt::SSAOTaskParams(), fnCommit, colorCorrectionTask,
            hvt::TaskManager::InsertionOrder::insertBefore);
    }

    int frameCount = 10;

    auto render = [&]() {
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

    uint8_t threshold = 1;
    uint16_t pixelCountThreshold = 1;
#if TARGET_OS_IPHONE
    pixelCountThreshold = 50;
#endif
ASSERT_TRUE(
    context->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName, threshold, pixelCountThreshold));
}
