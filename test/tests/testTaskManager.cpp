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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

// Include the appropriate test context declaration.
#include <RenderingFramework/TestContextCreator.h>

// Other include files.
#include <hvt/engine/framePass.h>
#include <hvt/engine/syncDelegate.h>
#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>

#include <hvt/tasks/blurTask.h>

#include <gtest/gtest.h>

#include <pxr/base/gf/vec4d.h>
#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/aovInputTask.h>
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/imaging/hdx/presentTask.h>
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hdx/simpleLightTask.h>
#include <pxr/usd/sdf/path.h>

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestTaskManager)
#else
HVT_TEST(TestViewportToolbox, TestTaskManager)
#endif
{
    // The goal of the unit test is to validate the "TaskManager" and "FramePass" classes working
    // together.

    // Prepares a test context and loads the sample file.
    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    // Creates the render index.
    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::RendererDescriptor rendererDesc;
    rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
    rendererDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

    // Creates the scene index.
    HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
    pRenderIndexProxy->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

    // Creates the frame pass.
    const SdfPath id { "/TestFramePass" };
    hvt::FramePassDescriptor desc { pRenderIndexProxy->RenderIndex(), id, {} };
    hvt::FramePassPtr framePass = std::make_unique<hvt::FramePass>(desc.uid.GetText());
    framePass->Initialize(desc);

    hvt::TaskManagerPtr& taskManager = framePass->GetTaskManager();

    // Lets define the application parameters.
    struct AppParams
    {
        PXR_NS::CameraUtilFraming framing;
        float blur { 3.25f };
        //...
    } app;
    app.framing = hvt::ViewParams::GetDefaultFraming(testContext->width(), testContext->height());

    auto renderBufferAccessor = framePass->GetRenderBufferAccessor();
    auto lightingAccessor     = framePass->GetLightingAccessor();

    const auto getLayerSettings = [&framePass]() -> hvt::BasicLayerParams const* {
        return &framePass->params();
    };

    SdfPathVector taskIds, renderTaskIds;

    // Create a lighting task, using the TaskCreationHelper.
    taskIds.push_back(hvt::CreateLightingTask(taskManager, lightingAccessor, getLayerSettings));

    // Create a single render task, using the TaskCreationHelper.
    static const TfToken kDefaultMaterialTag("defaultMaterialTag");
    renderTaskIds.push_back(hvt::CreateRenderTask(
        taskManager, renderBufferAccessor, getLayerSettings, kDefaultMaterialTag));

    // The accessor should always be valid, for the entire life time of the frame pass.
    ASSERT_FALSE(renderBufferAccessor.expired());

    if (renderBufferAccessor.lock()->IsAovSupported())
    {
        // Create a AOV Input Task, using the TaskCreationHelper.
        taskIds.push_back(hvt::CreateAovInputTask(taskManager, renderBufferAccessor));

        // Create a Present Task, using the TaskCreationHelper.
        taskIds.push_back(
            hvt::CreatePresentTask(taskManager, renderBufferAccessor, getLayerSettings));
    }

    // Create a Blur Task, with a locally-defined parameter update callback (the commit function).
    auto fnCommitBlur = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
        // Gets the current parameters.
        const VtValue value        = fnGetValue(HdTokens->params);
        hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();

        // Here, we can transfer application-specific settings to the task parameters.
        // By defining this task-specific update function at task creation time, the task can
        // then be simply added to the Task Manager and be processed as any other task.
        params.blurAmount = app.blur;

        // Saves the new parameters.
        fnSetValue(HdTokens->params, VtValue(params));
    };

    // Finds the present task Id in the existing list of created tasks, so we can use this Id
    // as the insertion position of the blur task.
    const SdfPath insertBeforeTask = taskManager->GetTaskPath(HdxPrimitiveTokens->presentTask);
    ASSERT_TRUE(!insertBeforeTask.IsEmpty());

    // Adds the blur task, before the present task.
    taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), hvt::BlurTaskParams(),
        fnCommitBlur, insertBeforeTask, hvt::TaskManager::InsertionOrder::insertBefore);

    // Renders at most 10 times (i.e., arbitrary number to guarantee best result).

    int frameCount = 10;
    auto render    = [&]() {
        // Updates the frame pass parameters (in case of app resize for example).
        hvt::FramePassParams& params = framePass->params();

        params.viewInfo.framing = app.framing;
        params.renderBufferSize = GfVec2i(testContext->width(), testContext->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->disabled;
        params.backgroundColor = TestHelpers::ColorDarkGrey;

        params.enablePresentation = testContext->presentationEnabled();

        // Renders the frame pass.
        framePass->Render();

        // Checks for completion.
        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    testContext->run(render, framePass.get());

    // Validates the rendering result.

    const std::string computedFileName = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName, 1));
}

namespace
{
hvt::RenderIndexProxyPtr CreateStormRenderer(std::shared_ptr<TestHelpers::TestContext>& testContext)
{
    // Creates a render delegate and render index.
    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::RendererDescriptor rendererDesc;
    rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
    rendererDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

    return pRenderIndexProxy;
}
} // namespace

HVT_TEST(TestViewportToolbox, TestTaskManagerAddRemove)
{
    // The goal of the unit test is to validate task insertion and removal with the TaskManager.

    auto testContext            = TestHelpers::CreateTestContext();
    auto renderIndexProxy       = CreateStormRenderer(testContext);
    HdRenderIndex* pRenderIndex = renderIndexProxy->RenderIndex();

    const SdfPath uid("/TestTaskManager");

    // Creates the HdEngine, SyncDelegate and TaskManager.
    auto engine       = std::make_unique<HdEngine>();
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    // Registers the first dummy task.
    static const TfToken kDummy1("Dummy1");
    const SdfPath pathDummy1 = taskManager->AddTask<HdxAovInputTask>(kDummy1, nullptr, nullptr);

    // Registers the second dummy task.
    static const TfToken kDummy2("Dummy2");
    const SdfPath pathDummy2 = taskManager->AddTask<HdxAovInputTask>(kDummy2, nullptr, nullptr);

    ASSERT_TRUE(taskManager->HasTask(pathDummy1));
    ASSERT_TRUE(taskManager->HasTask(pathDummy2));

    taskManager->RemoveTask(pathDummy1);

    ASSERT_FALSE(taskManager->HasTask(pathDummy1));
    ASSERT_TRUE(taskManager->HasTask(pathDummy2));

    taskManager->RemoveTask(pathDummy2);

    ASSERT_FALSE(taskManager->HasTask(pathDummy1));
    ASSERT_FALSE(taskManager->HasTask(pathDummy2));

    // Make sure the Task Manager is destroyed before the Render Index.
    taskManager = nullptr;
}

HVT_TEST(TestViewportToolbox, TestTaskManagerCommitFn)
{
    // The goal of the unit test is to validate the "TaskManager" commit function execution, which
    // is responsible for updating HdTask parameters.

    auto testContext            = TestHelpers::CreateTestContext();
    auto renderIndexProxy       = CreateStormRenderer(testContext);
    HdRenderIndex* pRenderIndex = renderIndexProxy->RenderIndex();

    static const SdfPath uid("/TestTaskManager");

    // Creates the task manager.
    auto engine       = std::make_unique<HdEngine>();
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    // Lets define the application parameters (e.g., what could be changed by a UI interaction).
    struct AppParams
    {
        float blur { 0.75f };
        //...
    } app;

    // Registers the blur task.
    auto fnCommitBlur = [&](hvt::TaskManager::GetTaskValueFn const& /*fnGetValue*/,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
        // Sets all the parameters of the Blur task.
        hvt::BlurTaskParams params;
        params.blurAmount = app.blur;
        fnSetValue(HdTokens->params, VtValue(params));
    };
    const SdfPath pathBlur = taskManager->AddTask<hvt::BlurTask>(
        hvt::BlurTask::GetToken(), hvt::BlurTaskParams(), fnCommitBlur);

    // Executes.
    taskManager->Execute(engine.get());

    // Checks the blur value.
    VtValue value              = taskManager->GetTaskValue(pathBlur, HdTokens->params);
    hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, app.blur);

    // Changes the blur default value.
    app.blur = 12.0f;

    // Executes.
    taskManager->Execute(engine.get());

    // Checks the new blur value.
    value  = taskManager->GetTaskValue(pathBlur, HdTokens->params);
    params = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, 12.0f);

    // Override the existing task commit function with a new function.
    constexpr float kNewBlurValue = 777.7f;
    taskManager->SetTaskCommitFn(pathBlur,
        [&](hvt::TaskManager::GetTaskValueFn const& /*fnGetValue*/,
            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
    {
        // Sets all the parameters of the Blur task.
        hvt::BlurTaskParams params;
        params.blurAmount = kNewBlurValue;
        fnSetValue(HdTokens->params, VtValue(params));
    });

    // Executes.
    taskManager->Execute(engine.get());

    // Make sure the commit function was updated and properly applied.
    value  = taskManager->GetTaskValue(pathBlur, HdTokens->params);
    params = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, kNewBlurValue);

    // Make sure the Task Manager is destroyed before the Render Index.
    taskManager = nullptr;
}

HVT_TEST(TestViewportToolbox, TestTaskManagerSetTaskValue)
{
    // The goal of the unit test is to validate TaskManager::GetTaskValue and
    // TaskManager::SetTaskValue.

    auto testContext            = TestHelpers::CreateTestContext();
    auto renderIndexProxy       = CreateStormRenderer(testContext);
    HdRenderIndex* pRenderIndex = renderIndexProxy->RenderIndex();

    static const SdfPath uid("/TestTaskManager");

    // Creates the HdEngine, SyncDelegate and TaskManager.
    auto engine       = std::make_unique<HdEngine>();
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    // Registers the blur task.
    auto fnCommitBlur = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
        // TODO: Think about the approach.
        // This approach is much more flexible. It can work like the previous case
        // or like below because the "id" is now available.

        const VtValue value = fnGetValue(HdTokens->params);

        // In that case I can benefit from existing settings without having all of them
        // in memory somewhere else like the previous example.
        const hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();

        // Do some changes.

        // NOTE: Code can also change the blur value if needed.
        fnSetValue(HdTokens->params, VtValue(params));
    };

    // Updates the blur parameters.
    hvt::BlurTaskParams params;
    params.blurAmount = 0.75f;

    const SdfPath& pathBlur =
        taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), params, fnCommitBlur);

    // Executes.
    taskManager->Execute(engine.get());

    // Checks the blur value.
    VtValue value = taskManager->GetTaskValue(pathBlur, HdTokens->params);
    params        = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, 0.75f);

    // Perform a second change.

    // Updates the blur parameters.
    params.blurAmount = 0.05f;
    taskManager->SetTaskValue(pathBlur, HdTokens->params, VtValue(params));

    // Executes.
    taskManager->Execute(engine.get());

    // Checks the blur value.
    value  = taskManager->GetTaskValue(pathBlur, HdTokens->params);
    params = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, 0.05f);

    // Make sure the Task Manager is destroyed before the Render Index.
    taskManager = nullptr;
}

HVT_TEST(TestViewportToolbox, TestTaskManagerTaskFlags)
{
    // The goal of the unit test is to validate the task flags that are used by the Task Manager
    // to classify the tasks into categories upon creation.

    auto testContext            = TestHelpers::CreateTestContext();
    auto renderIndexProxy       = CreateStormRenderer(testContext);
    HdRenderIndex* pRenderIndex = renderIndexProxy->RenderIndex();

    static const SdfPath uid("/TestTaskManager");

    // Creates the HdEngine, SyncDelegate and TaskManager.
    auto engine       = std::make_unique<HdEngine>();
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    const auto kInsertOrder = hvt::TaskManager::InsertionOrder::insertAtEnd;

    static const TfToken kSimpleLightTask("simpleLightTask");
    static const TfToken kRenderTaskToken("renderTask");
    static const TfToken kPickTaskToken("pickTask");
    std::vector<bool> commitFunctionsCalled = { false, false, false };

    // Create a dummy LightingTask commit function setting the associated flag when run.
    auto lightingCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                                hvt::TaskManager::SetTaskValueFn const&) {
        commitFunctionsCalled[0] = true;
    };
    const SdfPath kLightingTaskPath = taskManager->AddTask<HdxSimpleLightTask>(kSimpleLightTask,
        nullptr, lightingCommitFn, {}, kInsertOrder, hvt::TaskFlagsBits::kExecutableBit);

    // Create a dummy RenderTask commit function setting the associated flag when run.
    auto renderCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                              hvt::TaskManager::SetTaskValueFn const&) {
        commitFunctionsCalled[1] = true;
    };
    const SdfPath kRenderTaskPath =
        taskManager->AddTask<HdxRenderTask>(kRenderTaskToken, nullptr, renderCommitFn, {},
            kInsertOrder, hvt::TaskFlagsBits::kExecutableBit | hvt::TaskFlagsBits::kRenderTaskBit);

    // Create a dummy PickTask commit function setting the associated flag when run.
    auto pickCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                            hvt::TaskManager::SetTaskValueFn const&) {
        commitFunctionsCalled[2] = true;
    };
    const SdfPath kPickTaskPath = taskManager->AddTask<HdxPickTask>(kPickTaskToken, nullptr,
        pickCommitFn, {}, kInsertOrder, hvt::TaskFlagsBits::kPickingTaskBit);

    commitFunctionsCalled = { false, false, false }; // Reset values for the current test.
    taskManager->CommitTaskValues(hvt::TaskFlagsBits::kRenderTaskBit);
    ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ false, true, false }));

    // The following section validates TaskManager::CommitTaskValues is calling the commit functions
    // associated with the proper task flags.

    commitFunctionsCalled = { false, false, false }; // Reset values for the current test.
    taskManager->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ true, true, false }));

    commitFunctionsCalled = { false, false, false }; // Reset values for the current test.
    taskManager->CommitTaskValues(hvt::TaskFlagsBits::kPickingTaskBit);
    ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ false, false, true }));

    commitFunctionsCalled = { false, false, false }; // Reset values for the current test.
    taskManager->CommitTaskValues(
        hvt::TaskFlagsBits::kExecutableBit | hvt::TaskFlagsBits::kPickingTaskBit);
    ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ true, true, true }));

    // The following section validates TaskManager::GetTasks returns the expected list of HdTasks
    // according to their task flags.

    auto getTasks = [pRenderIndex](std::vector<SdfPath> const& taskPaths) {
        HdTaskSharedPtrVector tasks;
        for (SdfPath const& taskPath : taskPaths)
        {
            tasks.push_back(pRenderIndex->GetTask(taskPath));
        }
        return tasks;
    };

    auto filteredTasks = taskManager->GetTasks(hvt::TaskFlagsBits::kRenderTaskBit);
    ASSERT_TRUE(filteredTasks == getTasks({ kRenderTaskPath }));

    filteredTasks = taskManager->GetTasks(hvt::TaskFlagsBits::kExecutableBit);
    ASSERT_TRUE(filteredTasks == getTasks({ kLightingTaskPath, kRenderTaskPath }));

    filteredTasks = taskManager->GetTasks(hvt::TaskFlagsBits::kPickingTaskBit);
    ASSERT_TRUE(filteredTasks == getTasks({ kPickTaskPath }));

    filteredTasks = taskManager->GetTasks(
        hvt::TaskFlagsBits::kRenderTaskBit | hvt::TaskFlagsBits::kPickingTaskBit);
    ASSERT_TRUE(filteredTasks == getTasks({ kRenderTaskPath, kPickTaskPath }));

    // Make sure the Task Manager is destroyed before the Render Index.
    taskManager = nullptr;
}

HVT_TEST(TestViewportToolbox, TestTaskManagerEnableTask)
{
    // The goal of the unit test is to validate TaskManager::EnableTask, which is used to activate
    // and deactivate existing tasks, after they are created. Note: a disable task is considered
    // dormant, and the TaskManager will stop calling the associated CommitTaskFn callback as well
    // as stop executing the task.

    auto testContext            = TestHelpers::CreateTestContext();
    auto renderIndexProxy       = CreateStormRenderer(testContext);
    HdRenderIndex* pRenderIndex = renderIndexProxy->RenderIndex();

    static const SdfPath uid("/TestTaskManager");

    // Creates the HdEngine, SyncDelegate and TaskManager.
    auto engine       = std::make_unique<HdEngine>();
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    const auto kInsertOrder = hvt::TaskManager::InsertionOrder::insertAtEnd;

    static const TfToken kSimpleLightTask("simpleLightTask");
    static const TfToken kRenderTaskToken("renderTask");
    static const TfToken kPickTaskToken("pickTask");
    std::vector<bool> commitFunctionsCalled = { false, false, false };

    // Create a dummy LightingTask commit function setting the associated flag when run.
    auto lightingCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                                hvt::TaskManager::SetTaskValueFn const&) {
        commitFunctionsCalled[0] = true;
    };
    const SdfPath kLightingTaskPath = taskManager->AddTask<HdxSimpleLightTask>(kSimpleLightTask,
        nullptr, lightingCommitFn, {}, kInsertOrder, hvt::TaskFlagsBits::kExecutableBit);

    // Create a dummy RenderTask commit function setting the associated flag when run.
    auto renderCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                              hvt::TaskManager::SetTaskValueFn const&) {
        commitFunctionsCalled[1] = true;
    };
    const SdfPath kRenderTaskPath =
        taskManager->AddTask<HdxRenderTask>(kRenderTaskToken, nullptr, renderCommitFn, {},
            kInsertOrder, hvt::TaskFlagsBits::kExecutableBit | hvt::TaskFlagsBits::kRenderTaskBit);

    // Create a dummy PickTask commit function setting the associated flag when run.
    auto pickCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                            hvt::TaskManager::SetTaskValueFn const&) {
        commitFunctionsCalled[2] = true;
    };
    const SdfPath kPickTaskPath = taskManager->AddTask<HdxPickTask>(kPickTaskToken, nullptr,
        pickCommitFn, {}, kInsertOrder, hvt::TaskFlagsBits::kPickingTaskBit);

    const hvt::TaskFlags kAllTasks = hvt::TaskFlagsBits::kExecutableBit |
        hvt::TaskFlagsBits::kPickingTaskBit | hvt::TaskFlagsBits::kRenderTaskBit;

    {
        // Reset values for the current test.
        commitFunctionsCalled = { false, false, false };

        // Update tasks enabled/disabled state.
        taskManager->EnableTask(kLightingTaskPath, true);
        taskManager->EnableTask(kRenderTaskPath, false);
        taskManager->EnableTask(kPickTaskPath, true);

        // Execute the commit function for the enabled tasks.
        taskManager->CommitTaskValues(kAllTasks);

        // Validate the the commit function was called only for the enabled tasks.
        ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ true, false, true }));
    }

    {
        // Reset values for the current test.
        commitFunctionsCalled = { false, false, false };

        // Update tasks enabled/disabled state.
        taskManager->EnableTask(kLightingTaskPath, false);
        taskManager->EnableTask(kRenderTaskPath, true);
        taskManager->EnableTask(kPickTaskPath, false);

        // Execute the commit function for the enabled tasks.
        taskManager->CommitTaskValues(kAllTasks);

        // Validate the the commit function was called only for the enabled tasks.
        ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ false, true, false }));
    }

    // Make sure the Task Manager is destroyed before the Render Index.
    taskManager = nullptr;
}