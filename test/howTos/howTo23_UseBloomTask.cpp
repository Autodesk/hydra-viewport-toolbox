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

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/bloomTask.h>

#include <pxr/pxr.h>

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>

//
// How to add the Bloom post-process task before color correction.
//

namespace
{

struct AppParams
{
    float threshold  { 0.5f };
    float intensity  { 0.75f };
    float blurRadius { 6.0f };
};

} // namespace

#if defined(__ANDROID__) || (TARGET_OS_IPHONE == 1)
HVT_TEST(howTo, DISABLED_useBloomTask)
#else
HVT_TEST(howTo, useBloomTask)
#endif
{
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;
    AppParams app;

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
        passDesc.uid         = pxr::SdfPath("/FramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);

        const pxr::SdfPath colorCorrectionTask =
            sceneFramePass->GetTaskManager()->GetTaskPath(
                pxr::HdxPrimitiveTokens->colorCorrectionTask);

        auto fnCommit = [&app](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                          hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
            const pxr::VtValue value    = fnGetValue(pxr::HdTokens->params);
            hvt::BloomTaskParams params = value.Get<hvt::BloomTaskParams>();
            params.threshold            = app.threshold;
            params.intensity            = app.intensity;
            params.blurRadius           = app.blurRadius;
            fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
        };

        sceneFramePass->GetTaskManager()->AddTask<hvt::BloomTask>(hvt::BloomTask::GetToken(),
            hvt::BloomTaskParams(), fnCommit, colorCorrectionTask,
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

        sceneFramePass->Render();

        return --frameCount > 0;
    };

    context->run(render, sceneFramePass.get());

    // Validates the rendering result.

    ASSERT_TRUE(context->validateImages(computedImageName, imageFile));
}
