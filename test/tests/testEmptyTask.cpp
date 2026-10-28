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

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/emptyTask.h>

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/usd/sdf/path.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

hvt::RenderIndexProxyPtr _CreateStormRenderer(
    std::shared_ptr<TestHelpers::TestContext>& testContext)
{
    hvt::RenderIndexProxyPtr renderIndexProxy;
    hvt::RendererDescriptor rendererDesc;
    rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
    rendererDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(renderIndexProxy, rendererDesc);
    return renderIndexProxy;
}

struct EmptyTaskFixture
{
    std::shared_ptr<TestHelpers::TestContext> testContext;
    hvt::RenderIndexProxyPtr renderIndexProxy;
    std::unique_ptr<hvt::TaskManager> taskManager;

    EmptyTaskFixture()
    {
        testContext      = TestHelpers::CreateTestContext();
        renderIndexProxy = _CreateStormRenderer(testContext);
        static SdfPath const uid { "/TestEmptyTask" };
        taskManager = std::make_unique<hvt::TaskManager>(uid, renderIndexProxy->RenderIndex());
    }
};

} // namespace

TEST(TestEmptyTask, EmptyTaskParams_Equality_Defaults)
{
    hvt::EmptyTaskParams a;
    hvt::EmptyTaskParams b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestEmptyTask, EmptyTaskToken)
{
    TfToken const& token = hvt::EmptyTask::GetToken();
    EXPECT_FALSE(token.IsEmpty());
    EXPECT_EQ(token.GetString(), "emptyTask");
}

TEST(TestEmptyTask, EmptyTaskConstruction)
{
    EmptyTaskFixture f;

    EXPECT_FALSE(f.taskManager->HasTask(hvt::EmptyTask::GetToken()));

    SdfPath const taskPath = f.taskManager->AddTask<hvt::EmptyTask>(
        hvt::EmptyTask::GetToken(), hvt::EmptyTaskParams(), nullptr);

    EXPECT_TRUE(f.taskManager->HasTask(hvt::EmptyTask::GetToken()));
    EXPECT_TRUE(f.taskManager->HasTask(taskPath));
    EXPECT_FALSE(taskPath.IsEmpty());

    f.taskManager->RemoveTask(taskPath);
    EXPECT_FALSE(f.taskManager->HasTask(hvt::EmptyTask::GetToken()));
}
