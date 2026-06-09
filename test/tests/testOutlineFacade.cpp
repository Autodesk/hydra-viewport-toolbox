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

#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/outline/outline.h>
#include <hvt/tasks/outline/outlineMaskTask.h>
#include <hvt/tasks/outline/outlineOverlayTask.h>
#include <hvt/tasks/outline/outlinePrimIdsTask.h>

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include <gtest/gtest.h>

#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((outlineBasePrimIdsTask, "outlineBasePrimIdsTask"))
    ((outlineOverlayPrimIdsTask, "outlineOverlayPrimIdsTask"))
    ((outlineDefaultPrimIdsTask, "outlineDefaultPrimIdsTask"))
    ((outlineMaskTask, "outlineMaskTask"))
    ((outlineOverlayTask, "outlineOverlayTask")));

struct OutlineFacadeFixture
{
    std::shared_ptr<TestHelpers::TestContext> testContext;
    hvt::RenderIndexProxyPtr renderIndexProxy;
    hvt::FramePassPtr framePass;

    OutlineFacadeFixture()
    {
        testContext = TestHelpers::CreateTestContext();

        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndexProxy, rendererDesc);

        HdRetainedSceneIndexRefPtr retainedSceneIndex = HdRetainedSceneIndex::New();
        renderIndexProxy->RenderIndex()->InsertSceneIndex(
            retainedSceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineFacade");
        framePass            = hvt::ViewportEngine::CreateFramePass(passDesc);
    }
};

OutlineMaskTaskParams GetMaskParams(hvt::TaskManager& taskManager)
{
    SdfPath const maskPath = taskManager.GetTaskPath(_tokens->outlineMaskTask);
    VtValue const value    = taskManager.GetTaskValue(maskPath, HdTokens->params);
    return value.Get<hvt::OutlineMaskTaskParams>();
}

} // namespace

HVT_TEST(TestOutlineFacade, outline_styleEquality)
{
    hvt::OutlineStyle a;
    hvt::OutlineStyle b;

    ASSERT_EQ(a, b);

    b.selectedColor = GfVec4f(1.0f, 0.0f, 0.0f, 1.0f);
    ASSERT_NE(a, b);

    b = a;
    b.enableDefaultOutlines = false;
    ASSERT_NE(a, b);
}

HVT_TEST(TestOutlineFacade, outline_installCreatesFiveTasks)
{
    OutlineFacadeFixture f;
    hvt::Outline outline;

    auto& taskManager = f.framePass->GetTaskManager();

    outline.Install(*f.framePass);

    ASSERT_TRUE(taskManager->HasTask(_tokens->outlineBasePrimIdsTask));
    ASSERT_TRUE(taskManager->HasTask(_tokens->outlineOverlayPrimIdsTask));
    ASSERT_TRUE(taskManager->HasTask(_tokens->outlineDefaultPrimIdsTask));
    ASSERT_TRUE(taskManager->HasTask(_tokens->outlineMaskTask));
    ASSERT_TRUE(taskManager->HasTask(_tokens->outlineOverlayTask));
}

HVT_TEST(TestOutlineFacade, outline_installIsIdempotent)
{
    OutlineFacadeFixture f;
    hvt::Outline outline;

    auto& taskManager = f.framePass->GetTaskManager();

    outline.Install(*f.framePass);

    SdfPathVector pathsAfterFirstInstall;
    taskManager->GetTaskPaths(
        hvt::TaskFlagsBits::kExecutableBit, false, pathsAfterFirstInstall);
    size_t const countAfterFirst = pathsAfterFirstInstall.size();

    outline.Install(*f.framePass);

    SdfPathVector pathsAfterSecondInstall;
    taskManager->GetTaskPaths(
        hvt::TaskFlagsBits::kExecutableBit, false, pathsAfterSecondInstall);
    EXPECT_EQ(countAfterFirst, pathsAfterSecondInstall.size());
}

HVT_TEST(TestOutlineFacade, outline_taskOrderPrimIdsBeforeMaskBeforeOverlay)
{
    OutlineFacadeFixture f;
    hvt::Outline outline;

    auto& taskManager = f.framePass->GetTaskManager();
    outline.Install(*f.framePass);

    SdfPath const basePath    = taskManager->GetTaskPath(_tokens->outlineBasePrimIdsTask);
    SdfPath const overlayPrimIdsPath =
        taskManager->GetTaskPath(_tokens->outlineOverlayPrimIdsTask);
    SdfPath const defaultPath = taskManager->GetTaskPath(_tokens->outlineDefaultPrimIdsTask);
    SdfPath const maskPath    = taskManager->GetTaskPath(_tokens->outlineMaskTask);
    SdfPath const overlayPath = taskManager->GetTaskPath(_tokens->outlineOverlayTask);

    SdfPathVector taskPaths;
    taskManager->GetTaskPaths(hvt::TaskFlagsBits::kExecutableBit, false, taskPaths);

    auto indexOf = [&taskPaths](SdfPath const& path) {
        auto it = std::find(taskPaths.begin(), taskPaths.end(), path);
        EXPECT_NE(it, taskPaths.end());
        return static_cast<size_t>(std::distance(taskPaths.begin(), it));
    };

    size_t const baseIdx          = indexOf(basePath);
    size_t const overlayPrimIdsIdx = indexOf(overlayPrimIdsPath);
    size_t const defaultIdx       = indexOf(defaultPath);
    size_t const maskIdx          = indexOf(maskPath);
    size_t const overlayIdx       = indexOf(overlayPath);

    EXPECT_LT(baseIdx, maskIdx);
    EXPECT_LT(overlayPrimIdsIdx, maskIdx);
    EXPECT_LT(defaultIdx, maskIdx);
    EXPECT_LT(maskIdx, overlayIdx);
}

HVT_TEST(TestOutlineFacade, outline_setInputsDedup)
{
    OutlineFacadeFixture f;
    hvt::Outline outline;
    outline.Install(*f.framePass);

    hvt::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };

    outline.SetInputs(inputs);
    hvt::Outline::CacheStats afterFirst = outline.GetCacheStats();
    EXPECT_EQ(afterFirst.totalQueries, 1u);
    EXPECT_EQ(afterFirst.misses, 1u);
    EXPECT_EQ(afterFirst.hits, 0u);

    outline.SetInputs(inputs);
    hvt::Outline::CacheStats afterSecond = outline.GetCacheStats();
    EXPECT_EQ(afterSecond.totalQueries, 2u);
    EXPECT_EQ(afterSecond.misses, 1u);
    EXPECT_EQ(afterSecond.hits, 1u);

    inputs.hoverPaths = { SdfPath("/Root/Sphere") };
    outline.SetInputs(inputs);
    hvt::Outline::CacheStats afterThird = outline.GetCacheStats();
    EXPECT_EQ(afterThird.totalQueries, 3u);
    EXPECT_EQ(afterThird.misses, 2u);
    EXPECT_EQ(afterThird.hits, 1u);
}

HVT_TEST(TestOutlineFacade, outline_setStyleDedup)
{
    OutlineFacadeFixture f;
    hvt::Outline outline;
    outline.Install(*f.framePass);

    hvt::OutlineStyle style;
    outline.SetStyle(style);
    outline.SetStyle(style);

    hvt::OutlineStyle changed = style;
    changed.softnessStrength = 0.5f;
    outline.SetStyle(changed);
    outline.SetStyle(changed);
}

HVT_TEST(TestOutlineFacade, outline_maskTextureFallbackWhenDefaultDisabled)
{
    OutlineFacadeFixture f;
    hvt::Outline outline;
    outline.Install(*f.framePass);

    hvt::OutlineStyle style;
    style.enableDefaultOutlines = false;
    outline.SetStyle(style);

    hvt::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::OutlineMaskTaskParams maskParams = GetMaskParams(*f.framePass->GetTaskManager());

    EXPECT_EQ(maskParams.defaultPrimIdsTexture, "outlineBasePrimIdsTexture");
    EXPECT_EQ(maskParams.defaultDepthTexture, "outlineBaseDepthTexture");
    EXPECT_EQ(maskParams.style.hasDistinctDefault, 0);
}

HVT_TEST(TestOutlineFacade, outline_maskTextureFallbackWhenOverlayEmpty)
{
    OutlineFacadeFixture f;
    hvt::Outline outline;
    outline.Install(*f.framePass);

    hvt::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::OutlineMaskTaskParams maskParams = GetMaskParams(*f.framePass->GetTaskManager());

    EXPECT_EQ(maskParams.overlayPrimIdsTexture, "outlineBasePrimIdsTexture");
    EXPECT_EQ(maskParams.overlayDepthTexture, "outlineBaseDepthTexture");
    EXPECT_EQ(maskParams.style.hasDistinctOverlay, 0);
}

HVT_TEST(TestOutlineFacade, outline_excludePathsAppliedToDefaultCollection)
{
    OutlineFacadeFixture f;
    hvt::Outline outline;
    outline.Install(*f.framePass);

    hvt::OutlineStyle style;
    style.enableDefaultOutlines = true;
    outline.SetStyle(style);

    hvt::OutlineInputs inputs;
    inputs.excludePaths = { SdfPath("/Root/Transient") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    SdfPath const defaultPath = f.framePass->GetTaskManager()->GetTaskPath(
        _tokens->outlineDefaultPrimIdsTask);
    VtValue const value = f.framePass->GetTaskManager()->GetTaskValue(
        defaultPath, HdTokens->params);
    hvt::OutlinePrimIdsTaskParams primIdsParams =
        value.Get<hvt::OutlinePrimIdsTaskParams>();

    EXPECT_TRUE(primIdsParams.enabled);
    EXPECT_EQ(primIdsParams.collection.GetExcludePaths(),
        SdfPathVector{ SdfPath("/Root/Transient") });
}
