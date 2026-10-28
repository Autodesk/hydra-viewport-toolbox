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
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/emptyTask.h>

#include <pxr/imaging/hdx/tokens.h>

#include <gtest/gtest.h>

//
// How to register the empty (no-op) render task?
//

#if defined(__ANDROID__)
HVT_TEST(howTo, DISABLED_useEmptyTask)
#else
HVT_TEST(howTo, useEmptyTask)
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

        pxr::HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = pxr::SdfPath("/sceneFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);

        pxr::SdfPath const presentTask =
            sceneFramePass->GetTaskManager()->GetTaskPath(pxr::HdxPrimitiveTokens->presentTask);

        sceneFramePass->GetTaskManager()->AddTask<hvt::EmptyTask>(hvt::EmptyTask::GetToken(),
            hvt::EmptyTaskParams(), nullptr, presentTask,
            hvt::TaskManager::InsertionOrder::insertBefore);
    }

    int frameCount = 10;

    auto render = [&]()
    {
        auto& params = sceneFramePass->params();

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

        params.enablePresentation = context->presentationEnabled();

        sceneFramePass->Render();

        return --frameCount > 0;
    };

    context->run(render, sceneFramePass.get());

    // The empty task does not modify the framebuffer; match the default frame-pass baseline.
    ASSERT_TRUE(context->validateImages(computedImageName, "howTo/createOneFramePass"));
}
