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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/wboitRenderTask.h>
#include <hvt/tasks/wboitResolveTask.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hdx/oitRenderTask.h>
#include <pxr/imaging/hdx/oitResolveTask.h>
#include <pxr/imaging/hdx/renderTask.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ---------------------------------------------------------------------------
// Core tests
// ---------------------------------------------------------------------------

HVT_TEST(TestWboitTask, construction)
{
    // Verifies that the default TaskCreationOptions produces linked-list OIT (not WBOIT).

    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::RendererDescriptor rendererDesc;
    rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
    rendererDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

    HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
    pRenderIndexProxy->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

    {
        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestDefault");
        auto framePass       = hvt::ViewportEngine::CreateFramePass(passDesc);

        // CreateFramePass already calls CreatePresetTasks internally.
        auto& taskManager = framePass->GetTaskManager();
        ASSERT_TRUE(taskManager->HasTask(TfToken("oitResolveTask")));
        ASSERT_FALSE(taskManager->HasTask(TfToken("wboitResolveTask")));
    }

    {
        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex                    = pRenderIndexProxy->RenderIndex();
        passDesc.uid                            = SdfPath("/TestWbOit");
        passDesc.taskCreationOptions.useWbOit   = true;
        auto framePass = hvt::ViewportEngine::CreateFramePass(passDesc);

        // CreateFramePass already calls CreatePresetTasks internally.
        auto& taskManager = framePass->GetTaskManager();
        ASSERT_TRUE(taskManager->HasTask(TfToken("wboitResolveTask")));
        ASSERT_FALSE(taskManager->HasTask(TfToken("oitResolveTask")));
    }
}

HVT_TEST(TestWboitTask, resolveTaskParamsVtValue)
{
    // Validates that WbOitResolveTaskParams satisfies VtValue requirements.

    hvt::WbOitResolveTaskParams a;
    hvt::WbOitResolveTaskParams b;

    ASSERT_EQ(a, b);
    ASSERT_FALSE(a != b);

    std::ostringstream oss;
    oss << a;
    ASSERT_FALSE(oss.str().empty());
}
