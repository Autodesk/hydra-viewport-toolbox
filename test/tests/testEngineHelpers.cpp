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

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/basicLayerParams.h>
#include <hvt/engine/framePass.h>
#include <hvt/engine/renderBufferSettingsProvider.h>
#include <hvt/engine/syncDelegate.h>
#include <hvt/engine/taskManager.h>
#include <hvt/engine/taskUtils.h>
#include <hvt/engine/usdStageUtils.h>
#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdx/aovInputTask.h>
#include <pxr/imaging/hdx/renderSetupTask.h>

// ===========================================================================
// Tier 1 -- taskUtils.cpp pure utility functions (no GPU needed)
// ===========================================================================

// --- ToVec4i ---

TEST(TestEngine, ToVec4i_Positive)
{
    auto result = hvt::ToVec4i(GfVec4d(1.0, 2.0, 3.0, 4.0));
    EXPECT_EQ(result, GfVec4i(1, 2, 3, 4));
}

TEST(TestEngine, ToVec4i_Fractional)
{
    auto result = hvt::ToVec4i(GfVec4d(1.9, 2.7, 3.1, 4.99));
    EXPECT_EQ(result, GfVec4i(1, 2, 3, 4));
}

TEST(TestEngine, ToVec4i_Negative)
{
    auto result = hvt::ToVec4i(GfVec4d(-1.5, -2.5, 0.0, 100.0));
    EXPECT_EQ(result, GfVec4i(-1, -2, 0, 100));
}

TEST(TestEngine, ToVec4i_Zeros)
{
    auto result = hvt::ToVec4i(GfVec4d(0.0, 0.0, 0.0, 0.0));
    EXPECT_EQ(result, GfVec4i(0, 0, 0, 0));
}

// --- GetRenderTaskPathLeaf ---

TEST(TestEngine, GetRenderTaskPathLeaf_DefaultMaterialTag)
{
    auto leaf = hvt::GetRenderTaskPathLeaf(HdStMaterialTagTokens->defaultMaterialTag);
    std::string str = leaf.GetString();
    EXPECT_TRUE(str.find("renderTask_") == 0);
}

TEST(TestEngine, GetRenderTaskPathLeaf_Additive)
{
    auto leaf = hvt::GetRenderTaskPathLeaf(HdStMaterialTagTokens->additive);
    EXPECT_EQ(leaf.GetString(), "renderTask_additive");
}

TEST(TestEngine, GetRenderTaskPathLeaf_Translucent)
{
    auto leaf = hvt::GetRenderTaskPathLeaf(HdStMaterialTagTokens->translucent);
    EXPECT_EQ(leaf.GetString(), "renderTask_translucent");
}

TEST(TestEngine, GetRenderTaskPathLeaf_ColonReplacement)
{
    TfToken tagWithColon("my:tag");
    auto leaf = hvt::GetRenderTaskPathLeaf(tagWithColon);
    std::string str = leaf.GetString();
    EXPECT_EQ(str.find(':'), std::string::npos);
    EXPECT_EQ(str, "renderTask_my_tag");
}

// --- GetRenderTaskPath ---

TEST(TestEngine, GetRenderTaskPath_Structure)
{
    SdfPath controller("/myController");
    auto path = hvt::GetRenderTaskPath(controller, HdStMaterialTagTokens->additive);
    EXPECT_EQ(path.GetParentPath(), controller);
    EXPECT_EQ(path.GetNameToken(), TfToken("renderTask_additive"));
}

// --- GetAovPath ---

TEST(TestEngine, GetAovPath_Color)
{
    SdfPath parent("/pass");
    auto aovPath = hvt::GetAovPath(parent, HdAovTokens->color);
    EXPECT_EQ(aovPath.GetParentPath(), parent);
    EXPECT_TRUE(aovPath.GetNameToken().GetString().find("aov_") == 0);
}

TEST(TestEngine, GetAovPath_Depth)
{
    SdfPath parent("/pass");
    auto aovPath = hvt::GetAovPath(parent, HdAovTokens->depth);
    EXPECT_EQ(aovPath.GetParentPath(), parent);
    EXPECT_TRUE(aovPath.GetNameToken().GetString().find("aov_") == 0);
}

TEST(TestEngine, GetAovPath_DifferentAovsAreDifferent)
{
    SdfPath parent("/pass");
    auto colorPath = hvt::GetAovPath(parent, HdAovTokens->color);
    auto depthPath = hvt::GetAovPath(parent, HdAovTokens->depth);
    EXPECT_NE(colorPath, depthPath);
}

// --- CanUseMsaa ---

TEST(TestEngine, CanUseMsaa_DefaultMaterial)
{
    EXPECT_TRUE(hvt::CanUseMsaa(HdStMaterialTagTokens->defaultMaterialTag));
}

TEST(TestEngine, CanUseMsaa_Masked)
{
    EXPECT_TRUE(hvt::CanUseMsaa(HdStMaterialTagTokens->masked));
}

TEST(TestEngine, CanUseMsaa_Additive)
{
    EXPECT_TRUE(hvt::CanUseMsaa(HdStMaterialTagTokens->additive));
}

TEST(TestEngine, CanUseMsaa_Translucent)
{
    EXPECT_FALSE(hvt::CanUseMsaa(HdStMaterialTagTokens->translucent));
}

TEST(TestEngine, CanUseMsaa_Volume)
{
    EXPECT_FALSE(hvt::CanUseMsaa(HdStMaterialTagTokens->volume));
}

// --- SetBlendStateForMaterialTag ---

TEST(TestEngine, SetBlendState_DefaultMaterialTag)
{
    HdxRenderTaskParams params;
    hvt::SetBlendStateForMaterialTag(HdStMaterialTagTokens->defaultMaterialTag, params);
    EXPECT_FALSE(params.blendEnable);
    EXPECT_TRUE(params.depthMaskEnable);
    EXPECT_TRUE(params.enableAlphaToCoverage);
}

TEST(TestEngine, SetBlendState_Masked)
{
    HdxRenderTaskParams params;
    hvt::SetBlendStateForMaterialTag(HdStMaterialTagTokens->masked, params);
    EXPECT_FALSE(params.blendEnable);
    EXPECT_TRUE(params.depthMaskEnable);
    EXPECT_TRUE(params.enableAlphaToCoverage);
}

TEST(TestEngine, SetBlendState_Additive)
{
    HdxRenderTaskParams params;
    hvt::SetBlendStateForMaterialTag(HdStMaterialTagTokens->additive, params);
    EXPECT_TRUE(params.blendEnable);
    EXPECT_EQ(params.blendColorOp, HdBlendOpAdd);
    EXPECT_EQ(params.blendColorSrcFactor, HdBlendFactorOne);
    EXPECT_EQ(params.blendColorDstFactor, HdBlendFactorOne);
    EXPECT_FALSE(params.depthMaskEnable);
    EXPECT_FALSE(params.enableAlphaToCoverage);
}

TEST(TestEngine, SetBlendState_UnknownTag_NoChange)
{
    HdxRenderTaskParams before;
    HdxRenderTaskParams after;
    hvt::SetBlendStateForMaterialTag(TfToken("unknownTag"), after);
    EXPECT_EQ(before.blendEnable, after.blendEnable);
    EXPECT_EQ(before.depthMaskEnable, after.depthMaskEnable);
}

// ===========================================================================
// Tier 2 -- Default values for param structs (no GPU needed)
// ===========================================================================

// --- ViewParams::GetDefaultFraming ---

TEST(TestEngine, GetDefaultFraming_Simple)
{
    auto framing = hvt::ViewParams::GetDefaultFraming(800, 600);
    EXPECT_EQ(framing.dataWindow, GfRect2i(GfVec2i(0, 0), 800, 600));
}

TEST(TestEngine, GetDefaultFraming_WithOffset)
{
    auto framing = hvt::ViewParams::GetDefaultFraming(10, 20, 800, 600);
    EXPECT_EQ(framing.dataWindow, GfRect2i(GfVec2i(10, 20), 800, 600));
}

// --- BasicLayerParams defaults ---

TEST(TestEngine, BasicLayerParams_Defaults)
{
    hvt::BasicLayerParams params;
    EXPECT_EQ(params.colorspace, TfToken("sRGB"));
    EXPECT_TRUE(params.enablePresentation);
    EXPECT_EQ(params.depthCompare, HgiCompareFunctionLEqual);
    EXPECT_EQ(params.renderTags.size(), 4u);
    EXPECT_TRUE(params.enableSelection);
    EXPECT_FALSE(params.enableOutline);
}

TEST(TestEngine, BasicLayerParams_SelectionColors)
{
    hvt::BasicLayerParams params;
    EXPECT_EQ(params.selectionColor, GfVec4f(1.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(params.locateColor, GfVec4f(1.0f, 1.0f, 0.0f, 1.0f));
}

// --- FramePassParams defaults ---

TEST(TestEngine, FramePassParams_Defaults)
{
    hvt::FramePassParams params;
    EXPECT_TRUE(params.enableColorCorrection);
    EXPECT_TRUE(params.clearBackgroundColor);
    EXPECT_EQ(params.backgroundColor, GfVec4f(0.025f, 0.025f, 0.025f, 1.0f));
    EXPECT_FALSE(params.clearBackgroundDepth);
    EXPECT_FLOAT_EQ(params.backgroundDepth, 1.0f);
    EXPECT_TRUE(params.enableMultisampling);
    EXPECT_EQ(params.msaaSampleCount, 4u);
    EXPECT_FALSE(params.enableNeyeRenderOutput);
}

// --- ModelParams defaults ---

TEST(TestEngine, ModelParams_Defaults)
{
    hvt::ModelParams params;
    EXPECT_FALSE(params.isZAxisUp);
}

// --- ViewParams defaults ---

TEST(TestEngine, ViewParams_Defaults)
{
    hvt::ViewParams params;
    EXPECT_FALSE(params.isOrtho);
    EXPECT_DOUBLE_EQ(params.cameraDistance, 0.0);
    EXPECT_DOUBLE_EQ(params.fov, 0.0);
    EXPECT_FALSE(params.initialized);
    EXPECT_TRUE(params.is3DCamera);
    EXPECT_EQ(params.ambient, GfVec4f(0.0f, 0.0f, 0.0f, 0.0f));
}

// ===========================================================================
// Tier 3 -- RenderBufferBinding operators (no GPU needed)
// ===========================================================================

TEST(TestEngine, RenderBufferBinding_Equality_Defaults)
{
    hvt::RenderBufferBinding a;
    hvt::RenderBufferBinding b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestEngine, RenderBufferBinding_Inequality_DifferentAovName)
{
    hvt::RenderBufferBinding a;
    hvt::RenderBufferBinding b;
    b.aovName = HdAovTokens->color;
    EXPECT_NE(a, b);
}

TEST(TestEngine, RenderBufferBinding_Inequality_DifferentRendererName)
{
    hvt::RenderBufferBinding a;
    hvt::RenderBufferBinding b;
    b.rendererName = "HdStorm";
    EXPECT_NE(a, b);
}

TEST(TestEngine, RenderBufferBinding_Equality_SameNonDefault)
{
    hvt::RenderBufferBinding a;
    hvt::RenderBufferBinding b;
    a.aovName = HdAovTokens->depth;
    a.rendererName = "HdStorm";
    b.aovName = HdAovTokens->depth;
    b.rendererName = "HdStorm";
    EXPECT_EQ(a, b);
}

// ===========================================================================
// Tier 2 (GPU) -- SyncDelegate get/set/has round-trips
// ===========================================================================

namespace
{

hvt::RenderIndexProxyPtr CreateStormRenderer(std::shared_ptr<TestHelpers::TestContext> const& ctx)
{
    hvt::RenderIndexProxyPtr proxy;
    hvt::RendererDescriptor desc;
    desc.hgiDriver    = &ctx->_backend->hgiDriver();
    desc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(proxy, desc);
    return proxy;
}

} // anonymous namespace

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestEngine, DISABLED_SyncDelegate_SetGetHas)
#else
TEST(TestEngine, SyncDelegate_SetGetHas)
#endif
{
    auto testContext      = TestHelpers::CreateTestContext();
    auto renderIndexProxy = CreateStormRenderer(testContext);
    auto* pRenderIndex    = renderIndexProxy->RenderIndex();

    const SdfPath uid("/TestSyncDelegate");
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);

    const SdfPath taskId = uid.AppendChild(TfToken("myTask"));
    const TfToken key("params");
    const VtValue val(42);

    EXPECT_FALSE(syncDelegate->HasValue(taskId, key));
    EXPECT_TRUE(syncDelegate->GetValue(taskId, key).IsEmpty());
    EXPECT_EQ(syncDelegate->GetValuePtr(taskId, key), nullptr);

    syncDelegate->SetValue(taskId, key, val);

    EXPECT_TRUE(syncDelegate->HasValue(taskId, key));
    EXPECT_EQ(syncDelegate->GetValue(taskId, key).Get<int>(), 42);
    EXPECT_NE(syncDelegate->GetValuePtr(taskId, key), nullptr);
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestEngine, DISABLED_SyncDelegate_OverwriteValue)
#else
TEST(TestEngine, SyncDelegate_OverwriteValue)
#endif
{
    auto testContext      = TestHelpers::CreateTestContext();
    auto renderIndexProxy = CreateStormRenderer(testContext);
    auto* pRenderIndex    = renderIndexProxy->RenderIndex();

    const SdfPath uid("/TestSyncDelegate2");
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);

    const SdfPath taskId = uid.AppendChild(TfToken("myTask"));
    const TfToken key("value");

    syncDelegate->SetValue(taskId, key, VtValue(10));
    EXPECT_EQ(syncDelegate->GetValue(taskId, key).Get<int>(), 10);

    syncDelegate->SetValue(taskId, key, VtValue(99));
    EXPECT_EQ(syncDelegate->GetValue(taskId, key).Get<int>(), 99);
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestEngine, DISABLED_SyncDelegate_MultipleKeys)
#else
TEST(TestEngine, SyncDelegate_MultipleKeys)
#endif
{
    auto testContext      = TestHelpers::CreateTestContext();
    auto renderIndexProxy = CreateStormRenderer(testContext);
    auto* pRenderIndex    = renderIndexProxy->RenderIndex();

    const SdfPath uid("/TestSyncDelegate3");
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);

    const SdfPath taskId = uid.AppendChild(TfToken("task"));
    const TfToken keyA("alpha");
    const TfToken keyB("beta");

    syncDelegate->SetValue(taskId, keyA, VtValue(1.0f));
    syncDelegate->SetValue(taskId, keyB, VtValue(std::string("hello")));

    EXPECT_TRUE(syncDelegate->HasValue(taskId, keyA));
    EXPECT_TRUE(syncDelegate->HasValue(taskId, keyB));
    EXPECT_FLOAT_EQ(syncDelegate->GetValue(taskId, keyA).Get<float>(), 1.0f);
    EXPECT_EQ(syncDelegate->GetValue(taskId, keyB).Get<std::string>(), "hello");
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestEngine, DISABLED_TaskManager_HasTaskByName)
#else
TEST(TestEngine, TaskManager_HasTaskByName)
#endif
{
    auto testContext      = TestHelpers::CreateTestContext();
    auto renderIndexProxy = CreateStormRenderer(testContext);
    auto* pRenderIndex    = renderIndexProxy->RenderIndex();

    const SdfPath uid("/TestTM");
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    static const TfToken kTask("TestTask");
    taskManager->AddTask<HdxAovInputTask>(kTask, nullptr, nullptr);

    EXPECT_TRUE(taskManager->HasTask(kTask));
    EXPECT_FALSE(taskManager->HasTask(TfToken("NonExistent")));

    taskManager = nullptr;
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestEngine, DISABLED_TaskManager_GetTaskPath_NonExistent)
#else
TEST(TestEngine, TaskManager_GetTaskPath_NonExistent)
#endif
{
    auto testContext      = TestHelpers::CreateTestContext();
    auto renderIndexProxy = CreateStormRenderer(testContext);
    auto* pRenderIndex    = renderIndexProxy->RenderIndex();

    const SdfPath uid("/TestTM2");
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    const auto& path = taskManager->GetTaskPath(TfToken("DoesNotExist"));
    EXPECT_EQ(path, SdfPath::EmptyPath());

    taskManager = nullptr;
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestEngine, DISABLED_TaskManager_BuildTaskPath)
#else
TEST(TestEngine, TaskManager_BuildTaskPath)
#endif
{
    auto testContext      = TestHelpers::CreateTestContext();
    auto renderIndexProxy = CreateStormRenderer(testContext);
    auto* pRenderIndex    = renderIndexProxy->RenderIndex();

    const SdfPath uid("/TestTM3");
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    auto builtPath = taskManager->BuildTaskPath(TfToken("myTask"));
    EXPECT_EQ(builtPath.GetParentPath(), uid);
    EXPECT_EQ(builtPath.GetNameToken(), TfToken("myTask"));

    taskManager = nullptr;
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestEngine, DISABLED_TaskManager_InsertionOrdering)
#else
TEST(TestEngine, TaskManager_InsertionOrdering)
#endif
{
    auto testContext      = TestHelpers::CreateTestContext();
    auto renderIndexProxy = CreateStormRenderer(testContext);
    auto* pRenderIndex    = renderIndexProxy->RenderIndex();

    const SdfPath uid("/TestTM4");
    auto syncDelegate = std::make_shared<hvt::SyncDelegate>(uid, pRenderIndex);
    auto taskManager  = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, syncDelegate);

    static const TfToken kFirst("First");
    static const TfToken kSecond("Second");
    static const TfToken kBefore("Before");

    auto pathFirst  = taskManager->AddTask<HdxAovInputTask>(kFirst, nullptr, nullptr);
    auto pathSecond = taskManager->AddTask<HdxAovInputTask>(kSecond, nullptr, nullptr);

    // Insert before First.
    taskManager->AddTask<HdxAovInputTask>(
        kBefore, nullptr, nullptr, pathFirst, hvt::TaskManager::InsertionOrder::insertBefore);

    SdfPathVector allPaths;
    taskManager->GetTaskPaths(hvt::TaskFlagsBits::kAllTaskBits, false, allPaths);

    ASSERT_EQ(allPaths.size(), 3u);
    EXPECT_EQ(allPaths[0].GetNameToken(), kBefore);
    EXPECT_EQ(allPaths[1].GetNameToken(), kFirst);
    EXPECT_EQ(allPaths[2].GetNameToken(), kSecond);

    taskManager = nullptr;
}

// ===========================================================================
// Tier 3 (Stage) -- CreateStage, PrepareSelection, usdStageUtils
// ===========================================================================

TEST(TestEngine, CreateStage_ReturnsNonNull)
{
    auto stage = hvt::ViewportEngine::CreateStage("testStage");
    ASSERT_TRUE(stage);
}

TEST(TestEngine, CreateStage_EmptyName)
{
    auto stage = hvt::ViewportEngine::CreateStage("");
    ASSERT_TRUE(stage);
}

TEST(TestEngine, PrepareSelection_EmptyPaths)
{
    SdfPathSet empty;
    auto selection = hvt::ViewportEngine::PrepareSelection(empty);
    ASSERT_TRUE(selection);
}

TEST(TestEngine, PrepareSelection_WithPaths)
{
    SdfPathSet paths;
    paths.insert(SdfPath("/prim1"));
    paths.insert(SdfPath("/prim2"));
    auto selection = hvt::ViewportEngine::PrepareSelection(paths);
    ASSERT_TRUE(selection);

    auto selectedPaths =
        selection->GetSelectedPrimPaths(HdSelection::HighlightModeSelect);
    EXPECT_EQ(selectedPaths.size(), 2u);
}

TEST(TestEngine, PrepareSelection_AppendToExisting)
{
    SdfPathSet paths1;
    paths1.insert(SdfPath("/prim1"));
    auto sel1 = hvt::ViewportEngine::PrepareSelection(paths1);

    SdfPathSet paths2;
    paths2.insert(SdfPath("/prim2"));
    auto sel2 = hvt::ViewportEngine::PrepareSelection(paths2, HdSelection::HighlightModeSelect, sel1);

    auto selectedPaths = sel2->GetSelectedPrimPaths(HdSelection::HighlightModeSelect);
    EXPECT_EQ(selectedPaths.size(), 2u);
}

TEST(TestEngine, NoSelectionFilterFn_PassThrough)
{
    SdfPath path("/test/prim");
    auto result = hvt::ViewportEngine::noSelectionFilterFn(path);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], path);
}

// --- usdStageUtils: SetVisibleSelectBox / UpdateSelectBox ---

TEST(TestEngine, SetVisibleSelectBox_NoPrim_NoCrash)
{
    auto stage = hvt::ViewportEngine::CreateStage("empty");
    ASSERT_TRUE(stage);
    // Should not crash when the select box prim doesn't exist.
    hvt::SetVisibleSelectBox(stage, true);
    hvt::SetVisibleSelectBox(stage, false);
}

TEST(TestEngine, UpdateSelectBox_NoPrim_NoCrash)
{
    auto stage = hvt::ViewportEngine::CreateStage("empty");
    ASSERT_TRUE(stage);
    hvt::UpdateSelectBox(stage, 10, 20, 100, 200, 800.0, 600.0);
}

TEST(TestEngine, SelectBox_WithPrim)
{
    auto stage = hvt::ViewportEngine::CreateStage("selectBoxTest");
    ASSERT_TRUE(stage);

    hvt::ViewportEngine::CreateSelectBox(stage, SdfPath("/frozen/selectBoxGizmo"), false);

    auto prim = stage->GetPrimAtPath(SdfPath("/frozen/selectBoxGizmo"));
    ASSERT_TRUE(prim.IsValid());

    hvt::SetVisibleSelectBox(stage, true);
    hvt::SetVisibleSelectBox(stage, false);
    hvt::UpdateSelectBox(stage, 10, 20, 100, 200, 800.0, 600.0);
}
