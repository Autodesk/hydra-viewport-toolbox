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

// Include the appropriate test context declaration.
#include <RenderingFramework/TestContextCreator.h>

// Other include files.
#include <hvt/engine/framePass.h>
#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>

#include <hvt/tasks/blurTask.h>

#include <pxr/pxr.h>

#include <pxr/base/gf/vec4d.h>
#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/aovInputTask.h>
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/imaging/hdx/presentTask.h>
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hdx/simpleLightTask.h>
#include <pxr/usd/sdf/path.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
// Helper method to create a render index proxy for the Storm renderer.
hvt::RenderIndexProxyPtr _CreateStormRenderer(std::shared_ptr<TestHelpers::TestContext>& testContext)
{
    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::RendererDescriptor rendererDesc;
    rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
    rendererDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

    return pRenderIndexProxy;
}

// Even if the structure is close to the FramePass,keep it as-is. The goal is to keep the test code
// simple and readable and, only testing the TaskManager.
struct TaskManagerFixture
{
    std::shared_ptr<TestHelpers::TestContext> testContext;
    hvt::RenderIndexProxyPtr renderIndexProxy;
    HdRenderIndex* pRenderIndex = nullptr;
    std::unique_ptr<hvt::Engine> engine;
    HdRetainedSceneIndexRefPtr retainedSceneIndex;
    std::unique_ptr<hvt::TaskManager> taskManager;

    TaskManagerFixture()
    {
        testContext      = TestHelpers::CreateTestContext();
        renderIndexProxy = _CreateStormRenderer(testContext);
        pRenderIndex     = renderIndexProxy->RenderIndex();

        engine             = std::make_unique<hvt::Engine>();
        retainedSceneIndex = HdRetainedSceneIndex::New();
        pRenderIndex->InsertSceneIndex(retainedSceneIndex, SdfPath::AbsoluteRootPath());

        static const SdfPath uid("/TestTaskManager");
        taskManager = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, retainedSceneIndex);
    }

    ~TaskManagerFixture() { taskManager = nullptr; }
};
} // anonymous namespace

// ---------------------------------------------------------------------------
// Integration test: TaskManager + FramePass working together with rendering.
// ---------------------------------------------------------------------------

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestTaskManager, DISABLED_taskmgr_integration)
#else
HVT_TEST(TestTaskManager, taskmgr_integration)
#endif
{
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

    const SdfPath id { "/TestFramePass" };
    hvt::FramePassDescriptor desc { pRenderIndexProxy->RenderIndex(), id, {}, {} };
    hvt::FramePassPtr framePass = std::make_unique<hvt::FramePass>(desc.uid.GetText());
    framePass->Initialize(desc);

    hvt::TaskManagerPtr& taskManager = framePass->GetTaskManager();

    struct AppParams
    {
        PXR_NS::CameraUtilFraming framing;
        float blur { 3.25f };
    } app;
    app.framing = hvt::ViewParams::GetDefaultFraming(testContext->width(), testContext->height());

    auto renderBufferAccessor = framePass->GetRenderBufferAccessor();
    auto lightingAccessor     = framePass->GetLightingAccessor();

    const auto getLayerSettings = [&framePass]() -> hvt::BasicLayerParams const*
    { return &framePass->params(); };

    SdfPathVector taskIds, renderTaskIds;

    taskIds.push_back(hvt::CreateLightingTask(taskManager, lightingAccessor, getLayerSettings));

    static const TfToken kDefaultMaterialTag("defaultMaterialTag");
    renderTaskIds.push_back(hvt::CreateRenderTask(
        taskManager, renderBufferAccessor, getLayerSettings, kDefaultMaterialTag));

    ASSERT_FALSE(renderBufferAccessor.expired());

    if (renderBufferAccessor.lock()->IsAovSupported())
    {
        taskIds.push_back(hvt::CreateAovInputTask(taskManager, renderBufferAccessor));
        taskIds.push_back(
            hvt::CreatePresentTask(taskManager, renderBufferAccessor, getLayerSettings));
    }

    auto fnCommitBlur = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
    {
        const VtValue value        = fnGetValue(HdTokens->params);
        hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
        params.blurAmount          = app.blur;
        fnSetValue(HdTokens->params, VtValue(params));
    };

    const SdfPath insertBeforeTask = taskManager->GetTaskPath(HdxPrimitiveTokens->presentTask);
    ASSERT_TRUE(!insertBeforeTask.IsEmpty());

    taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), hvt::BlurTaskParams(),
        fnCommitBlur, insertBeforeTask, hvt::TaskManager::InsertionOrder::insertBefore);

    int frameCount = 10;
    auto render    = [&]()
    {
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

        framePass->Render();

        return --frameCount > 0;
    };

    testContext->run(render, framePass.get());

    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName, 1));
}

// ---------------------------------------------------------------------------
// Add and remove tasks by path.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, addRemoveByPath)
{
    TaskManagerFixture f;

    static const TfToken kDummy1("Dummy1");
    const SdfPath pathDummy1 = f.taskManager->AddTask<HdxAovInputTask>(kDummy1, nullptr, nullptr);

    static const TfToken kDummy2("Dummy2");
    const SdfPath pathDummy2 = f.taskManager->AddTask<HdxAovInputTask>(kDummy2, nullptr, nullptr);

    ASSERT_TRUE(f.taskManager->HasTask(pathDummy1));
    ASSERT_TRUE(f.taskManager->HasTask(pathDummy2));

    // Verify the prims exist in the retained scene index.
    ASSERT_TRUE(f.retainedSceneIndex->GetPrim(pathDummy1).dataSource != nullptr);
    ASSERT_TRUE(f.retainedSceneIndex->GetPrim(pathDummy2).dataSource != nullptr);

    f.taskManager->RemoveTask(pathDummy1);

    ASSERT_FALSE(f.taskManager->HasTask(pathDummy1));
    ASSERT_TRUE(f.taskManager->HasTask(pathDummy2));

    // Verify Dummy1 prim is gone from the scene index but Dummy2 is still there.
    ASSERT_TRUE(f.retainedSceneIndex->GetPrim(pathDummy1).dataSource == nullptr);
    ASSERT_TRUE(f.retainedSceneIndex->GetPrim(pathDummy2).dataSource != nullptr);

    f.taskManager->RemoveTask(pathDummy2);

    ASSERT_FALSE(f.taskManager->HasTask(pathDummy1));
    ASSERT_FALSE(f.taskManager->HasTask(pathDummy2));

    // Verify both prims are gone from the scene index.
    ASSERT_TRUE(f.retainedSceneIndex->GetPrim(pathDummy1).dataSource == nullptr);
    ASSERT_TRUE(f.retainedSceneIndex->GetPrim(pathDummy2).dataSource == nullptr);
}

// ---------------------------------------------------------------------------
// HasTask, GetTaskPath, RemoveTask, and EnableTask by TfToken name.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, lookupByName)
{
    TaskManagerFixture f;

    static const TfToken kTaskA("TaskA");
    static const TfToken kTaskB("TaskB");

    const SdfPath pathA = f.taskManager->AddTask<HdxAovInputTask>(kTaskA, nullptr, nullptr);
    f.taskManager->AddTask<HdxAovInputTask>(kTaskB, nullptr, nullptr);

    // HasTask by name.
    ASSERT_TRUE(f.taskManager->HasTask(kTaskA));
    ASSERT_TRUE(f.taskManager->HasTask(kTaskB));
    ASSERT_FALSE(f.taskManager->HasTask(TfToken("NonExistent")));

    // GetTaskPath by name.
    ASSERT_EQ(f.taskManager->GetTaskPath(kTaskA), pathA);
    ASSERT_TRUE(f.taskManager->GetTaskPath(TfToken("NonExistent")).IsEmpty());

    // EnableTask by name.
    bool committed = false;
    auto commitFn  = [&committed](hvt::TaskManager::GetTaskValueFn const&,
                        hvt::TaskManager::SetTaskValueFn const&) { committed = true; };

    static const TfToken kTaskC("TaskC");
    f.taskManager->AddTask<HdxAovInputTask>(kTaskC, nullptr, commitFn);

    f.taskManager->EnableTask(kTaskC, false);
    committed = false;
    f.taskManager->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    ASSERT_FALSE(committed);

    f.taskManager->EnableTask(kTaskC, true);
    committed = false;
    f.taskManager->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    ASSERT_TRUE(committed);

    // RemoveTask by name.
    f.taskManager->RemoveTask(kTaskA);
    ASSERT_FALSE(f.taskManager->HasTask(kTaskA));
    ASSERT_TRUE(f.taskManager->HasTask(kTaskB));
}

// ---------------------------------------------------------------------------
// Commit function execution and override via SetTaskCommitFn.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, commitFunction)
{
    TaskManagerFixture f;

    struct AppParams
    {
        float blur { 0.75f };
    } app;

    auto fnCommitBlur = [&](hvt::TaskManager::GetTaskValueFn const&,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
    {
        hvt::BlurTaskParams params;
        params.blurAmount = app.blur;
        fnSetValue(HdTokens->params, VtValue(params));
    };
    const SdfPath pathBlur = f.taskManager->AddTask<hvt::BlurTask>(
        hvt::BlurTask::GetToken(), hvt::BlurTaskParams(), fnCommitBlur);

    f.taskManager->Execute(f.engine.get());

    VtValue value              = f.taskManager->GetTaskValue(pathBlur, HdTokens->params);
    hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, app.blur);

    app.blur = 12.0f;
    f.taskManager->Execute(f.engine.get());

    value  = f.taskManager->GetTaskValue(pathBlur, HdTokens->params);
    params = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, 12.0f);

    // Override the existing task commit function.
    constexpr float kNewBlurValue = 777.7f;
    f.taskManager->SetTaskCommitFn(pathBlur,
        [&](hvt::TaskManager::GetTaskValueFn const&,
            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            hvt::BlurTaskParams params;
            params.blurAmount = kNewBlurValue;
            fnSetValue(HdTokens->params, VtValue(params));
        });

    f.taskManager->Execute(f.engine.get());

    value  = f.taskManager->GetTaskValue(pathBlur, HdTokens->params);
    params = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, kNewBlurValue);
}

// ---------------------------------------------------------------------------
// GetTaskValue / SetTaskValue for params.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, getSetTaskValue)
{
    TaskManagerFixture f;

    auto fnCommitBlur = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
    {
        const VtValue value              = fnGetValue(HdTokens->params);
        const hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
        fnSetValue(HdTokens->params, VtValue(params));
    };

    hvt::BlurTaskParams params;
    params.blurAmount = 0.75f;

    const SdfPath& pathBlur =
        f.taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), params, fnCommitBlur);

    f.taskManager->Execute(f.engine.get());

    VtValue value = f.taskManager->GetTaskValue(pathBlur, HdTokens->params);
    params        = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, 0.75f);

    params.blurAmount = 0.05f;
    ASSERT_TRUE(f.taskManager->SetTaskValue(pathBlur, HdTokens->params, VtValue(params)));

    f.taskManager->Execute(f.engine.get());

    value  = f.taskManager->GetTaskValue(pathBlur, HdTokens->params);
    params = value.Get<hvt::BlurTaskParams>();
    ASSERT_EQ(params.blurAmount, 0.05f);
}

// ---------------------------------------------------------------------------
// SetTaskValue for collection and renderTags keys.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, setCollectionAndRenderTags)
{
    TaskManagerFixture f;

    static const TfToken kTask("testTask");
    const SdfPath taskPath = f.taskManager->AddTask<HdxAovInputTask>(kTask, nullptr, nullptr);

    // Set and verify a collection.
    HdRprimCollection collection(TfToken("myCollection"), HdReprSelector(HdReprTokens->hull));
    ASSERT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->collection, VtValue(collection)));

    VtValue readBack = f.taskManager->GetTaskValue(taskPath, HdTokens->collection);
    ASSERT_TRUE(readBack.IsHolding<HdRprimCollection>());
    ASSERT_EQ(readBack.Get<HdRprimCollection>().GetName(), TfToken("myCollection"));

    // Set and verify render tags.
    TfTokenVector renderTags = { HdRenderTagTokens->geometry, HdRenderTagTokens->guide };
    ASSERT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->renderTags, VtValue(renderTags)));

    readBack = f.taskManager->GetTaskValue(taskPath, HdTokens->renderTags);
    ASSERT_TRUE(readBack.IsHolding<TfTokenVector>());
    ASSERT_EQ(readBack.Get<TfTokenVector>().size(), 2u);
    ASSERT_EQ(readBack.Get<TfTokenVector>()[0], HdRenderTagTokens->geometry);
    ASSERT_EQ(readBack.Get<TfTokenVector>()[1], HdRenderTagTokens->guide);
}

// ---------------------------------------------------------------------------
// SetTaskValue with an unchanged value is a no-op (equality check).
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, setValueEqualitySkipsDirty)
{
    TaskManagerFixture f;

    hvt::BlurTaskParams params;
    params.blurAmount = 1.5f;

    const SdfPath taskPath =
        f.taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), params, nullptr);

    // Execute to sync and consume all initial dirty bits.
    f.taskManager->Execute(f.engine.get());

    HdChangeTracker& tracker = f.pRenderIndex->GetChangeTracker();

    // Setting the same value again should be a no-op (still returns true).
    ASSERT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->params, VtValue(params)));

    HdDirtyBits dirtyBits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_FALSE(dirtyBits & HdChangeTracker::DirtyParams)
        << "SetTaskValue with an unchanged value should not dirty the task.";

    // Setting a different value should dirty the task.
    params.blurAmount = 99.0f;
    ASSERT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->params, VtValue(params)));

    dirtyBits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_TRUE(dirtyBits & HdChangeTracker::DirtyParams)
        << "SetTaskValue with a changed value should dirty the task.";
}

// ---------------------------------------------------------------------------
// Equality skip for collection and renderTags keys.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, setCollectionAndRenderTagsEqualitySkipsDirty)
{
    // Verify that the equality-check optimization works for the collection and
    // renderTags keys, not just params. Setting the same collection or renderTags
    // value should NOT dirty the task; setting a different value should dirty it
    // with the correct dirty bit (DirtyCollection / DirtyRenderTags respectively).
    TaskManagerFixture f;

    static const TfToken kTask("dirtyCollTagTask");
    const SdfPath taskPath = f.taskManager->AddTask<HdxAovInputTask>(kTask, nullptr, nullptr);

    HdRprimCollection collection(TfToken("col1"), HdReprSelector(HdReprTokens->hull));
    f.taskManager->SetTaskValue(taskPath, HdTokens->collection, VtValue(collection));

    TfTokenVector renderTags = { HdRenderTagTokens->geometry };
    f.taskManager->SetTaskValue(taskPath, HdTokens->renderTags, VtValue(renderTags));

    f.taskManager->Execute(f.engine.get());

    HdChangeTracker& tracker = f.pRenderIndex->GetChangeTracker();

    // Same collection -- should not dirty.
    ASSERT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->collection, VtValue(collection)));
    HdDirtyBits bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_FALSE(bits & HdChangeTracker::DirtyCollection)
        << "Setting the same collection should not dirty the task.";

    // Different collection -- should dirty.
    HdRprimCollection collection2(TfToken("col2"), HdReprSelector(HdReprTokens->smoothHull));
    ASSERT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->collection, VtValue(collection2)));
    bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_TRUE(bits & HdChangeTracker::DirtyCollection)
        << "Setting a different collection should dirty the task.";

    // Consume dirty bits.
    f.taskManager->Execute(f.engine.get());

    // Same renderTags -- should not dirty.
    ASSERT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->renderTags, VtValue(renderTags)));
    bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_FALSE(bits & HdChangeTracker::DirtyRenderTags)
        << "Setting the same renderTags should not dirty the task.";

    // Different renderTags -- should dirty.
    TfTokenVector renderTags2 = { HdRenderTagTokens->geometry, HdRenderTagTokens->guide };
    ASSERT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->renderTags, VtValue(renderTags2)));
    bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_TRUE(bits & HdChangeTracker::DirtyRenderTags)
        << "Setting different renderTags should dirty the task.";
}

// ---------------------------------------------------------------------------
// Commit function dirtiness: changed vs unchanged values.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, commitFunctionDirtiness)
{
    // Verify that the commit-function path (CommitTaskValues) correctly dirties
    // the task when the commit function writes a changed value, and does NOT
    // dirty it when the commit function writes back the same value. This is the
    // primary production path for updating task parameters.
    TaskManagerFixture f;

    hvt::BlurTaskParams params;
    params.blurAmount = 5.0f;

    float externalBlur = 5.0f;

    auto commitFn = [&externalBlur](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                        hvt::TaskManager::SetTaskValueFn const& fnSetValue)
    {
        hvt::BlurTaskParams p = fnGetValue(HdTokens->params).Get<hvt::BlurTaskParams>();
        p.blurAmount          = externalBlur;
        fnSetValue(HdTokens->params, VtValue(p));
    };

    const SdfPath taskPath =
        f.taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), params, commitFn);

    // Initial execute to sync and consume all initial dirty bits.
    f.taskManager->Execute(f.engine.get());

    HdChangeTracker& tracker = f.pRenderIndex->GetChangeTracker();

    // Commit with unchanged externalBlur (5.0f == 5.0f) -- should NOT dirty.
    f.taskManager->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    HdDirtyBits bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_FALSE(bits & HdChangeTracker::DirtyParams)
        << "CommitTaskValues writing back the same value should not dirty the task.";

    // Change the external value and commit -- should dirty.
    externalBlur = 42.0f;
    f.taskManager->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_TRUE(bits & HdChangeTracker::DirtyParams)
        << "CommitTaskValues writing a changed value should dirty the task.";

    // Verify the value was actually stored.
    VtValue value = f.taskManager->GetTaskValue(taskPath, HdTokens->params);
    ASSERT_EQ(value.Get<hvt::BlurTaskParams>().blurAmount, 42.0f);
}

// ---------------------------------------------------------------------------
// Dirty locator isolation: setting one key does not dirty unrelated keys.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, dirtyLocatorIsolation)
{
    // Verify that updating one task data key only dirties the corresponding
    // locator, not others. For example, changing params should set DirtyParams
    // but NOT DirtyCollection or DirtyRenderTags, and vice versa. This ensures
    // the per-field HdDataSourceLocator granularity works correctly.
    TaskManagerFixture f;

    hvt::BlurTaskParams params;
    params.blurAmount = 1.0f;

    const SdfPath taskPath =
        f.taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), params, nullptr);

    f.taskManager->Execute(f.engine.get());

    HdChangeTracker& tracker = f.pRenderIndex->GetChangeTracker();

    // Change only params.
    params.blurAmount = 2.0f;
    f.taskManager->SetTaskValue(taskPath, HdTokens->params, VtValue(params));

    HdDirtyBits bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_TRUE(bits & HdChangeTracker::DirtyParams)
        << "Changing params should set DirtyParams.";
    ASSERT_FALSE(bits & HdChangeTracker::DirtyCollection)
        << "Changing params should NOT set DirtyCollection.";
    ASSERT_FALSE(bits & HdChangeTracker::DirtyRenderTags)
        << "Changing params should NOT set DirtyRenderTags.";

    // Consume dirty bits.
    f.taskManager->Execute(f.engine.get());

    // Change only collection.
    HdRprimCollection collection(TfToken("isolated"), HdReprSelector(HdReprTokens->hull));
    f.taskManager->SetTaskValue(taskPath, HdTokens->collection, VtValue(collection));

    bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_TRUE(bits & HdChangeTracker::DirtyCollection)
        << "Changing collection should set DirtyCollection.";
    ASSERT_FALSE(bits & HdChangeTracker::DirtyParams)
        << "Changing collection should NOT set DirtyParams.";
    ASSERT_FALSE(bits & HdChangeTracker::DirtyRenderTags)
        << "Changing collection should NOT set DirtyRenderTags.";

    // Consume dirty bits.
    f.taskManager->Execute(f.engine.get());

    // Change only renderTags.
    TfTokenVector tags = { HdRenderTagTokens->guide };
    f.taskManager->SetTaskValue(taskPath, HdTokens->renderTags, VtValue(tags));

    bits = tracker.GetTaskDirtyBits(taskPath);
    ASSERT_TRUE(bits & HdChangeTracker::DirtyRenderTags)
        << "Changing renderTags should set DirtyRenderTags.";
    ASSERT_FALSE(bits & HdChangeTracker::DirtyParams)
        << "Changing renderTags should NOT set DirtyParams.";
    ASSERT_FALSE(bits & HdChangeTracker::DirtyCollection)
        << "Changing renderTags should NOT set DirtyCollection.";
}

// ---------------------------------------------------------------------------
// SetTaskValue returns false for invalid arguments.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, setTaskValueReturnsFalseOnError)
{
    TaskManagerFixture f;

    hvt::BlurTaskParams params;
    params.blurAmount = 1.0f;

    const SdfPath taskPath =
        f.taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), params, nullptr);

    // Empty uid.
    EXPECT_FALSE(f.taskManager->SetTaskValue(SdfPath(), HdTokens->params, VtValue(params)));

    // Non-existent task path.
    EXPECT_FALSE(f.taskManager->SetTaskValue(SdfPath("/bogus"), HdTokens->params, VtValue(params)));

    // Unsupported key on an existing task.
    EXPECT_FALSE(f.taskManager->SetTaskValue(taskPath, TfToken("unsupportedKey"), VtValue(params)));

    // Valid call should still return true after the failed ones.
    params.blurAmount = 2.0f;
    EXPECT_TRUE(f.taskManager->SetTaskValue(taskPath, HdTokens->params, VtValue(params)));
}

// ---------------------------------------------------------------------------
// GetTaskValue returns an empty VtValue for invalid arguments.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, getTaskValueReturnsEmptyOnError)
{
    TaskManagerFixture f;

    hvt::BlurTaskParams params;
    params.blurAmount = 1.0f;

    const SdfPath taskPath =
        f.taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), params, nullptr);

    // Empty uid.
    EXPECT_TRUE(f.taskManager->GetTaskValue(SdfPath(), HdTokens->params).IsEmpty());

    // Non-existent task path.
    EXPECT_TRUE(f.taskManager->GetTaskValue(SdfPath("/bogus"), HdTokens->params).IsEmpty());

    // Unsupported key on an existing task.
    EXPECT_TRUE(f.taskManager->GetTaskValue(taskPath, TfToken("unsupportedKey")).IsEmpty());

    // Valid call should still return a non-empty value after the failed ones.
    VtValue value = f.taskManager->GetTaskValue(taskPath, HdTokens->params);
    EXPECT_FALSE(value.IsEmpty());
    EXPECT_EQ(value.Get<hvt::BlurTaskParams>().blurAmount, 1.0f);
}

// ---------------------------------------------------------------------------
// Task flags: CommitTaskValues and GetTasks filtering.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, taskFlagsFiltering)
{
    TaskManagerFixture f;

    const auto kInsertOrder = hvt::TaskManager::InsertionOrder::insertAtEnd;

    static const TfToken kSimpleLightTask("simpleLightTask");
    static const TfToken kRenderTaskToken("renderTask");
    static const TfToken kPickTaskToken("pickTask");
    std::vector<bool> commitFunctionsCalled = { false, false, false };

    auto lightingCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                                hvt::TaskManager::SetTaskValueFn const&)
    { commitFunctionsCalled[0] = true; };
    const SdfPath kLightingTaskPath = f.taskManager->AddTask<HdxSimpleLightTask>(kSimpleLightTask,
        nullptr, lightingCommitFn, {}, kInsertOrder, hvt::TaskFlagsBits::kExecutableBit);

    auto renderCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                              hvt::TaskManager::SetTaskValueFn const&)
    { commitFunctionsCalled[1] = true; };
    const SdfPath kRenderTaskPath =
        f.taskManager->AddTask<HdxRenderTask>(kRenderTaskToken, nullptr, renderCommitFn, {},
            kInsertOrder, hvt::TaskFlagsBits::kExecutableBit | hvt::TaskFlagsBits::kRenderTaskBit);

    auto pickCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                            hvt::TaskManager::SetTaskValueFn const&)
    { commitFunctionsCalled[2] = true; };
    const SdfPath kPickTaskPath = f.taskManager->AddTask<HdxPickTask>(kPickTaskToken, nullptr,
        pickCommitFn, {}, kInsertOrder, hvt::TaskFlagsBits::kPickingTaskBit);

    // Validate CommitTaskValues filtering.

    commitFunctionsCalled = { false, false, false };
    f.taskManager->CommitTaskValues(hvt::TaskFlagsBits::kRenderTaskBit);
    ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ false, true, false }));

    commitFunctionsCalled = { false, false, false };
    f.taskManager->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ true, true, false }));

    commitFunctionsCalled = { false, false, false };
    f.taskManager->CommitTaskValues(hvt::TaskFlagsBits::kPickingTaskBit);
    ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ false, false, true }));

    commitFunctionsCalled = { false, false, false };
    f.taskManager->CommitTaskValues(
        hvt::TaskFlagsBits::kExecutableBit | hvt::TaskFlagsBits::kPickingTaskBit);
    ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ true, true, true }));

    // Validate GetTasks filtering.

    auto getTasks = [&f](std::vector<SdfPath> const& taskPaths)
    {
        HdTaskSharedPtrVector tasks;
        for (SdfPath const& taskPath : taskPaths)
        {
            tasks.push_back(f.pRenderIndex->GetTask(taskPath));
        }
        return tasks;
    };

    auto filteredTasks = f.taskManager->GetTasks(hvt::TaskFlagsBits::kRenderTaskBit);
    ASSERT_TRUE(filteredTasks == getTasks({ kRenderTaskPath }));

    filteredTasks = f.taskManager->GetTasks(hvt::TaskFlagsBits::kExecutableBit);
    ASSERT_TRUE(filteredTasks == getTasks({ kLightingTaskPath, kRenderTaskPath }));

    filteredTasks = f.taskManager->GetTasks(hvt::TaskFlagsBits::kPickingTaskBit);
    ASSERT_TRUE(filteredTasks == getTasks({ kPickTaskPath }));

    filteredTasks = f.taskManager->GetTasks(
        hvt::TaskFlagsBits::kRenderTaskBit | hvt::TaskFlagsBits::kPickingTaskBit);
    ASSERT_TRUE(filteredTasks == getTasks({ kRenderTaskPath, kPickTaskPath }));
}

// ---------------------------------------------------------------------------
// Enable/disable tasks affects CommitTaskValues and GetTasks.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, enableDisableTask)
{
    TaskManagerFixture f;

    const auto kInsertOrder = hvt::TaskManager::InsertionOrder::insertAtEnd;

    static const TfToken kSimpleLightTask("simpleLightTask");
    static const TfToken kRenderTaskToken("renderTask");
    static const TfToken kPickTaskToken("pickTask");
    std::vector<bool> commitFunctionsCalled = { false, false, false };

    auto lightingCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                                hvt::TaskManager::SetTaskValueFn const&)
    { commitFunctionsCalled[0] = true; };
    const SdfPath kLightingTaskPath = f.taskManager->AddTask<HdxSimpleLightTask>(kSimpleLightTask,
        nullptr, lightingCommitFn, {}, kInsertOrder, hvt::TaskFlagsBits::kExecutableBit);

    auto renderCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                              hvt::TaskManager::SetTaskValueFn const&)
    { commitFunctionsCalled[1] = true; };
    const SdfPath kRenderTaskPath =
        f.taskManager->AddTask<HdxRenderTask>(kRenderTaskToken, nullptr, renderCommitFn, {},
            kInsertOrder, hvt::TaskFlagsBits::kExecutableBit | hvt::TaskFlagsBits::kRenderTaskBit);

    auto pickCommitFn = [&commitFunctionsCalled](hvt::TaskManager::GetTaskValueFn const&,
                            hvt::TaskManager::SetTaskValueFn const&)
    { commitFunctionsCalled[2] = true; };
    const SdfPath kPickTaskPath = f.taskManager->AddTask<HdxPickTask>(kPickTaskToken, nullptr,
        pickCommitFn, {}, kInsertOrder, hvt::TaskFlagsBits::kPickingTaskBit);

    const hvt::TaskFlags kAllTasks = hvt::TaskFlagsBits::kExecutableBit |
        hvt::TaskFlagsBits::kPickingTaskBit | hvt::TaskFlagsBits::kRenderTaskBit;

    {
        commitFunctionsCalled = { false, false, false };

        f.taskManager->EnableTask(kLightingTaskPath, true);
        f.taskManager->EnableTask(kRenderTaskPath, false);
        f.taskManager->EnableTask(kPickTaskPath, true);

        f.taskManager->CommitTaskValues(kAllTasks);

        ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ true, false, true }));
    }

    {
        commitFunctionsCalled = { false, false, false };

        f.taskManager->EnableTask(kLightingTaskPath, false);
        f.taskManager->EnableTask(kRenderTaskPath, true);
        f.taskManager->EnableTask(kPickTaskPath, false);

        f.taskManager->CommitTaskValues(kAllTasks);

        ASSERT_TRUE(commitFunctionsCalled == std::vector<bool>({ false, true, false }));
    }
}

// ---------------------------------------------------------------------------
// Task insertion ordering: insertBefore, insertAfter, insertAtEnd.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, insertionOrdering)
{
    TaskManagerFixture f;

    static const TfToken kTaskA("TaskA");
    static const TfToken kTaskB("TaskB");
    static const TfToken kTaskC("TaskC");
    static const TfToken kTaskD("TaskD");

    // Insert A at end, then B at end. Order: [A, B].
    const SdfPath pathA = f.taskManager->AddTask<HdxAovInputTask>(kTaskA, nullptr, nullptr);
    const SdfPath pathB = f.taskManager->AddTask<HdxAovInputTask>(kTaskB, nullptr, nullptr);

    // Insert C before B. Order: [A, C, B].
    const SdfPath pathC = f.taskManager->AddTask<HdxAovInputTask>(
        kTaskC, nullptr, nullptr, pathB, hvt::TaskManager::InsertionOrder::insertBefore);

    // Insert D after A. Order: [A, D, C, B].
    const SdfPath pathD = f.taskManager->AddTask<HdxAovInputTask>(
        kTaskD, nullptr, nullptr, pathA, hvt::TaskManager::InsertionOrder::insertAfter);

    // Verify the order via GetTasks.
    auto tasks = f.taskManager->GetTasks(hvt::TaskFlagsBits::kExecutableBit);
    ASSERT_EQ(tasks.size(), 4u);
    ASSERT_EQ(tasks[0].get(), f.pRenderIndex->GetTask(pathA).get());
    ASSERT_EQ(tasks[1].get(), f.pRenderIndex->GetTask(pathD).get());
    ASSERT_EQ(tasks[2].get(), f.pRenderIndex->GetTask(pathC).get());
    ASSERT_EQ(tasks[3].get(), f.pRenderIndex->GetTask(pathB).get());
}

// ---------------------------------------------------------------------------
// AddRenderTask convenience sets the correct task flags.
// ---------------------------------------------------------------------------

HVT_TEST(TestTaskManager, addRenderTaskFlags)
{
    TaskManagerFixture f;

    static const TfToken kRenderTask("myRenderTask");
    const SdfPath taskPath =
        f.taskManager->AddRenderTask<HdxRenderTask>(kRenderTask, HdxRenderTaskParams(), nullptr);

    // AddRenderTask should set both kExecutableBit and kRenderTaskBit.
    SdfPathVector renderTaskPaths;
    f.taskManager->GetTaskPaths(hvt::TaskFlagsBits::kRenderTaskBit, false, renderTaskPaths);
    ASSERT_EQ(renderTaskPaths.size(), 1u);
    ASSERT_EQ(renderTaskPaths[0], taskPath);

    SdfPathVector executablePaths;
    f.taskManager->GetTaskPaths(hvt::TaskFlagsBits::kExecutableBit, false, executablePaths);
    ASSERT_EQ(executablePaths.size(), 1u);
    ASSERT_EQ(executablePaths[0], taskPath);

    // Should not show up as a picking task.
    SdfPathVector pickingPaths;
    f.taskManager->GetTaskPaths(hvt::TaskFlagsBits::kPickingTaskBit, false, pickingPaths);
    ASSERT_TRUE(pickingPaths.empty());
}
