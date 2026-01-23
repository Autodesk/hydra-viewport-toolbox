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
#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/tasks/clearBufferTask.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>

namespace
{
    using namespace pxr;
    using namespace hvt;

SdfPath CreateClearBufferTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsWeakPtr, TfToken const& taskName, GfVec4f const& clearColor,
    float clearDepth, SdfPath const& atPos, TaskManager::InsertionOrder order)
{
    auto fnCommit = [renderSettingsWeakPtr, clearColor, clearDepth](
                        TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            auto params = fnGetValue(HdTokens->params).Get<ClearBufferTaskParams>();

            // Set the clear values
            params.clearColor = clearColor;
            params.clearDepth = clearDepth;

            // Get AOV bindings from the render buffer settings
            params.aovBindings = renderBufferSettings->GetAovParamCache().aovBindingsNoClear;

            for (size_t i = 0; i < params.aovBindings.size(); ++i)
            {
                if (i == 0)
                    params.aovBindings[i].clearValue = VtValue(clearColor);

            }

            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    ClearBufferTaskParams initialParams;
    initialParams.clearColor = clearColor;
    initialParams.clearDepth = clearDepth;

    return taskManager->AddTask<ClearBufferTask>(
        taskName, initialParams, fnCommit, atPos, order);

    /* auto taskTok = ClearBufferTask::GetToken();

    if (taskTok == TfToken() || taskManager == nullptr || order ==
    TaskManager::InsertionOrder::insertAfter || atPos == SdfPath()) return {}; return {};
    */
}
}
//
// How to create one frame pass using Storm?
//
HVT_TEST(howTo, createOneFramePass)
{
    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
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

        // Add a ClearBufferTask to clear buffers to red before rendering.
        auto& taskManager         = sceneFramePass->GetTaskManager();
        auto renderBufferAccessor = sceneFramePass->GetRenderBufferAccessor();
        
        auto renderTask_additive = GetRenderTaskPathLeaf(pxr::TfToken("additive"));
        auto insertPos = taskManager->GetTaskPath(renderTask_additive);
        
        // Create clear buffer task with red color
        pxr::GfVec4f redColor(1.0f, 0.0f, 0.0f, 1.0f);
        
        CreateClearBufferTask(taskManager, renderBufferAccessor, TfToken("clearBuffer01") , redColor,
            1.0f, insertPos,
            hvt::TaskManager::InsertionOrder::insertAfter);

        auto renderTask_translucent = GetRenderTaskPathLeaf(pxr::TfToken("translucent"));
        
        CreateClearBufferTask(taskManager, renderBufferAccessor, TfToken("clearBuffer02"), redColor,
            1.0f,
            taskManager->GetTaskPath(renderTask_translucent),
            hvt::TaskManager::InsertionOrder::insertBefore);

        CreateClearBufferTask(taskManager, renderBufferAccessor, TfToken("clearBuffer03"), redColor,
            1.0f,
            taskManager->GetTaskPath(renderTask_translucent),
            hvt::TaskManager::InsertionOrder::insertAfter);

        
        CreateClearBufferTask(taskManager, renderBufferAccessor, TfToken("clearBuffer04"), redColor,
            1.0f, taskManager->GetTaskPath(TfToken("colorCorrectionTask")),
            hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        // Updates the main frame pass.

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

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, sceneFramePass.get());

    // Validates the rendering result.

    ASSERT_TRUE(context->validateImages(computedImageName, imageFile));
}
