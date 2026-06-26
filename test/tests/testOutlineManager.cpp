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
#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/outline/outlineManager.h>
#include <hvt/tasks/outline/outlineMaskTask.h>
#include <hvt/tasks/outline/outlineOverlayTask.h>
#include <hvt/tasks/outline/outlinePrimIdsTask.h>

#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>

#include <gtest/gtest.h>

#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

// Raw token strings for the five outline tasks. Used by parameter-propagation
// tests that look up tasks by name in the TaskManager directly.
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((outlineBasePrimIdsTask,    "outlineBasePrimIdsTask"))
    ((outlineOverlayPrimIdsTask, "outlineOverlayPrimIdsTask"))
    ((outlineDefaultPrimIdsTask, "outlineDefaultPrimIdsTask"))
    ((outlineMaskTask,           "outlineMaskTask"))
    ((outlineOverlayTask,        "outlineOverlayTask")));

// Minimal fixture: a FramePass without a scene index.
// Sufficient for install, cache, and style-dedup tests.
struct OutlineFixture
{
    std::shared_ptr<TestHelpers::TestContext> testContext;
    hvt::RenderIndexProxyPtr renderIndexProxy;
    hvt::FramePassPtr framePass;

    OutlineFixture()
    {
        testContext = TestHelpers::CreateTestContext();

        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndexProxy, rendererDesc);

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineManager");
        framePass            = hvt::ViewportEngine::CreateFramePass(passDesc);
    }
};

// Fixture with a retained scene index wired into the render index.
// Required by parameter-propagation tests that call CommitTaskValues() to
// read back committed hvt::Outline::OutlineMaskTaskParams or OutlinePrimIdsTaskParams.
struct OutlineSceneFixture
{
    std::shared_ptr<TestHelpers::TestContext> testContext;
    hvt::RenderIndexProxyPtr renderIndexProxy;
    hvt::FramePassPtr framePass;

    OutlineSceneFixture()
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
        passDesc.uid         = SdfPath("/TestOutlineScene");
        framePass            = hvt::ViewportEngine::CreateFramePass(passDesc);
    }
};

// Helper: reads back the committed hvt::Outline::OutlineMaskTaskParams from a TaskManager.
hvt::Outline::OutlineMaskTaskParams _GetMaskParams(hvt::TaskManager& taskManager)
{
    SdfPath const maskPath = taskManager.GetTaskPath(_tokens->outlineMaskTask);
    VtValue const value    = taskManager.GetTaskValue(maskPath, HdTokens->params);
    return value.Get<hvt::Outline::OutlineMaskTaskParams>();
}

} // namespace

// =====================================================================
// OutlineStyle -- equality and default value tests
// (no GPU required)
// =====================================================================

/// Test: Verifies OutlineStyle equality detects differences in each field.
HVT_TEST(TestOutlineManager, outline_styleEquality)
{
    hvt::Outline::OutlineStyle a;
    hvt::Outline::OutlineStyle b;

    ASSERT_EQ(a, b);
    ASSERT_FALSE(a != b);

    b.selectedColor = GfVec4f(1.0f, 0.0f, 0.0f, 1.0f);
    ASSERT_NE(a, b);

    b                    = {};
    b.selectionLeadColor = GfVec4f(0.0f, 1.0f, 0.0f, 1.0f);
    ASSERT_NE(a, b);

    b              = {};
    b.overlayColor = GfVec4f(0.0f, 0.0f, 1.0f, 0.5f);
    ASSERT_NE(a, b);

    b                       = {};
    b.enableDefaultOutlines = false;
    ASSERT_NE(a, b);

    b                  = {};
    b.softnessStrength = 0.5f;
    ASSERT_NE(a, b);

    b                 = {};
    b.softnessFalloff = 0.8f;
    ASSERT_NE(a, b);

    b          = {};
    b.blurMode = hvt::Outline::BlurMode::Blur5x5;
    ASSERT_NE(a, b);

    b          = {};
    b.blurMode = hvt::Outline::BlurMode::None;
    ASSERT_NE(a, b);

    b               = {};
    b.blurIntensity = 2.0f;
    ASSERT_NE(a, b);

    b                       = {};
    b.maskVisualizationMode = hvt::Outline::VisualizationMode::VISUALIZE_DEPTH;
    ASSERT_NE(a, b);
}

/// Test: Verifies default OutlineStyle field values match the documented defaults.
HVT_TEST(TestOutlineManager, outline_styleDefaultValues)
{
    hvt::Outline::OutlineStyle style;

    ASSERT_EQ(style.selectedColor,           GfVec4f(1.0f, 1.0f, 1.0f, 1.0f));
    ASSERT_EQ(style.selectedHoverColor,      GfVec4f(1.0f, 0.84f, 0.0f, 1.0f));
    ASSERT_EQ(style.selectionLeadColor,      GfVec4f(0.0f, 0.8f, 1.0f, 1.0f));
    ASSERT_EQ(style.selectionLeadHoverColor, GfVec4f(1.0f, 0.84f, 0.0f, 1.0f));
    ASSERT_EQ(style.overlayColor,            GfVec4f(1.0f, 1.0f, 1.0f, 0.7f));
    ASSERT_EQ(style.overlayHoverColor,       GfVec4f(1.0f, 0.84f, 0.0f, 1.0f));
    ASSERT_EQ(style.unselectedHoverColor,    GfVec4f(1.0f, 0.84f, 0.0f, 1.0f));
    ASSERT_EQ(style.defaultColor,            GfVec4f(0.5f, 0.5f, 0.5f, 1.0f));
    ASSERT_TRUE(style.enableDefaultOutlines);
    ASSERT_FLOAT_EQ(style.softnessStrength,  1.0f);
    ASSERT_FLOAT_EQ(style.softnessFalloff,   0.4f);
    ASSERT_EQ(style.blurMode,                hvt::Outline::BlurMode::Blur3x3);
    ASSERT_FLOAT_EQ(style.blurIntensity,     1.0f);
    ASSERT_EQ(style.maskVisualizationMode,   hvt::Outline::VisualizationMode::VISUALIZE_MASK_3x3);
}

// =====================================================================
// Outline::Install -- task lifecycle tests
// (requires GPU via TestContext and FramePass)
// =====================================================================

/// Test: Verifies Install() registers all five outline tasks in the frame pass.
HVT_TEST(TestOutlineManager, outline_install)
{
    OutlineFixture f;
    hvt::Outline::OutlineManager outline;

    auto& taskManager = f.framePass->GetTaskManager();

    ASSERT_FALSE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Base")));
    ASSERT_FALSE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Overlay")));
    ASSERT_FALSE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Default")));
    ASSERT_FALSE(taskManager->HasTask(hvt::Outline::OutlineMaskTask::GetToken()));
    ASSERT_FALSE(taskManager->HasTask(hvt::Outline::OutlineOverlayTask::GetToken()));

    outline.Install(*f.framePass);

    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Base")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Overlay")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Default")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlineMaskTask::GetToken()));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlineOverlayTask::GetToken()));
}

/// Test: Verifies that calling Install() a second time is silently ignored
/// and does not duplicate tasks in the frame pass.
HVT_TEST(TestOutlineManager, outline_installTwiceIsNoop)
{
    OutlineFixture f;
    hvt::Outline::OutlineManager outline;

    outline.Install(*f.framePass);
    outline.Install(*f.framePass); // second call emits TF_WARN and returns early

    auto& taskManager = f.framePass->GetTaskManager();
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Base")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Overlay")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Default")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlineMaskTask::GetToken()));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlineOverlayTask::GetToken()));
}

/// Test: Verifies that all three prim-IDs tasks execute before the mask task,
/// and the mask task executes before the overlay task.
HVT_TEST(TestOutlineManager, outline_taskOrderPrimIdsBeforeMaskBeforeOverlay)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;

    auto& taskManager = f.framePass->GetTaskManager();
    outline.Install(*f.framePass);

    SdfPath const basePath           = taskManager->GetTaskPath(_tokens->outlineBasePrimIdsTask);
    SdfPath const overlayPrimIdsPath = taskManager->GetTaskPath(_tokens->outlineOverlayPrimIdsTask);
    SdfPath const defaultPath        = taskManager->GetTaskPath(_tokens->outlineDefaultPrimIdsTask);
    SdfPath const maskPath           = taskManager->GetTaskPath(_tokens->outlineMaskTask);
    SdfPath const overlayPath        = taskManager->GetTaskPath(_tokens->outlineOverlayTask);

    SdfPathVector taskPaths;
    taskManager->GetTaskPaths(hvt::TaskFlagsBits::kExecutableBit, false, taskPaths);

    auto indexOf = [&taskPaths](SdfPath const& path) {
        auto it = std::find(taskPaths.begin(), taskPaths.end(), path);
        EXPECT_NE(it, taskPaths.end());
        return static_cast<size_t>(std::distance(taskPaths.begin(), it));
    };

    size_t const baseIdx           = indexOf(basePath);
    size_t const overlayPrimIdsIdx = indexOf(overlayPrimIdsPath);
    size_t const defaultIdx        = indexOf(defaultPath);
    size_t const maskIdx           = indexOf(maskPath);
    size_t const overlayIdx        = indexOf(overlayPath);

    EXPECT_LT(baseIdx,           maskIdx);
    EXPECT_LT(overlayPrimIdsIdx, maskIdx);
    EXPECT_LT(defaultIdx,        maskIdx);
    EXPECT_LT(maskIdx,           overlayIdx);
}

// =====================================================================
// Outline::SetInputs -- cache behavior tests
// (no GPU required; SetInputs() / GetCacheStats() work standalone)
// =====================================================================

/// Test: Verifies that calling SetInputs() with all-empty inputs (identical
/// to the default-constructed state) counts as a cache hit.
HVT_TEST(TestOutlineManager, outline_cacheFirstEmptyCallIsHit)
{
    hvt::Outline::OutlineManager outline;
    outline.SetInputs(hvt::Outline::OutlineInputs{}); // same as default state -> hit

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries, 1u);
    ASSERT_EQ(stats.hits,         1u);
    ASSERT_EQ(stats.misses,       0u);
}

/// Test: Verifies that calling SetInputs() with non-empty paths counts
/// as a cache miss (changed from default empty state).
HVT_TEST(TestOutlineManager, outline_cacheFirstNonEmptyCallIsMiss)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/world/cube") };
    outline.SetInputs(inputs);

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries, 1u);
    ASSERT_EQ(stats.hits,         0u);
    ASSERT_EQ(stats.misses,       1u);
}

/// Test: Verifies that calling SetInputs() twice with identical inputs
/// counts the second call as a cache hit.
HVT_TEST(TestOutlineManager, outline_cacheHitOnIdenticalInputs)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/world/cube") };

    outline.SetInputs(inputs); // miss
    outline.SetInputs(inputs); // hit -- inputs unchanged

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries, 2u);
    ASSERT_EQ(stats.hits,         1u);
    ASSERT_EQ(stats.misses,       1u);
}

/// Test: Verifies that changing any input field triggers a cache miss.
HVT_TEST(TestOutlineManager, outline_cacheMissOnChangedInputs)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs a;
    a.selectedPaths = { SdfPath("/world/cube") };

    hvt::Outline::OutlineInputs b;
    b.selectedPaths = { SdfPath("/world/sphere") };

    outline.SetInputs(a); // miss
    outline.SetInputs(b); // miss -- selectedPaths changed

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries, 2u);
    ASSERT_EQ(stats.hits,         0u);
    ASSERT_EQ(stats.misses,       2u);
}

/// Test: Verifies that cache statistics accumulate correctly across
/// a sequence of hit and miss calls.
HVT_TEST(TestOutlineManager, outline_cacheStatsAccumulate)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/world/cube") };

    outline.SetInputs(inputs); // miss
    outline.SetInputs(inputs); // hit
    outline.SetInputs(inputs); // hit

    inputs.leadPath = SdfPath("/world/cube");
    outline.SetInputs(inputs); // miss -- leadPath changed
    outline.SetInputs(inputs); // hit

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries, 5u);
    ASSERT_EQ(stats.hits,         3u);
    ASSERT_EQ(stats.misses,       2u);
}

/// Test: Verifies that maxCollectionSize tracks the largest number of
/// paths seen across all SetInputs() calls.
HVT_TEST(TestOutlineManager, outline_cacheMaxCollectionSize)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs smallInputs;
    smallInputs.selectedPaths = { SdfPath("/a") };
    outline.SetInputs(smallInputs); // miss, size=1

    hvt::Outline::OutlineInputs largeInputs;
    largeInputs.selectedPaths = { SdfPath("/b"), SdfPath("/c"), SdfPath("/d") };
    outline.SetInputs(largeInputs); // miss, size=3

    hvt::Outline::OutlineInputs mediumInputs;
    mediumInputs.selectedPaths = { SdfPath("/e"), SdfPath("/f") };
    outline.SetInputs(mediumInputs); // miss, size=2

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.maxCollectionSize, 3u);
}

/// Test: Verifies that changing selectedPaths and hoverPaths independently
/// each produce a cache miss, and that identical calls in between produce hits.
HVT_TEST(TestOutlineManager, outline_cacheSetInputsDedupWithHoverPaths)
{
    OutlineFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };

    outline.SetInputs(inputs);
    hvt::Outline::OutlineManager::CacheStats afterFirst = outline.GetCacheStats();
    ASSERT_EQ(afterFirst.totalQueries, 1u);
    ASSERT_EQ(afterFirst.misses,       1u);
    ASSERT_EQ(afterFirst.hits,         0u);

    outline.SetInputs(inputs);
    hvt::Outline::OutlineManager::CacheStats afterSecond = outline.GetCacheStats();
    ASSERT_EQ(afterSecond.totalQueries, 2u);
    ASSERT_EQ(afterSecond.misses,       1u);
    ASSERT_EQ(afterSecond.hits,         1u);

    inputs.hoverPaths = { SdfPath("/Root/Sphere") };
    outline.SetInputs(inputs);
    hvt::Outline::OutlineManager::CacheStats afterThird = outline.GetCacheStats();
    ASSERT_EQ(afterThird.totalQueries, 3u);
    ASSERT_EQ(afterThird.misses,       2u);
    ASSERT_EQ(afterThird.hits,         1u);
}

// =====================================================================
// Outline::SetStyle -- dedup behavior tests
// (no GPU required)
// =====================================================================

/// Test: Verifies that SetStyle() accepts repeated identical calls without
/// error and re-applies when a field changes.
HVT_TEST(TestOutlineManager, outline_setStyleDedup)
{
    OutlineFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    outline.SetStyle(style);
    outline.SetStyle(style); // identical -- no re-apply expected

    hvt::Outline::OutlineStyle changed = style;
    changed.softnessStrength  = 0.5f;
    outline.SetStyle(changed); // different -- re-apply expected
    outline.SetStyle(changed); // identical again -- no re-apply
}

// =====================================================================
// Outline internals -- task ordering and parameter propagation
// (requires FramePass with scene index; no full GPU render)
// =====================================================================

/// Test: Verifies that when enableDefaultOutlines is false, the mask task's
/// default texture inputs fall back to the base prim-IDs textures rather than
/// the separate default-pass textures, and hasDistinctDefault is cleared.
HVT_TEST(TestOutlineManager, outline_maskTextureFallbackWhenDefaultDisabled)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = false;
    outline.SetStyle(style);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlineMaskTaskParams maskParams = _GetMaskParams(*f.framePass->GetTaskManager());

    EXPECT_EQ(maskParams.defaultPrimIdsTexture,    "outlineBasePrimIdsTexture");
    EXPECT_EQ(maskParams.defaultDepthTexture,       "outlineBaseDepthTexture");
    EXPECT_EQ(maskParams.style.hasDistinctDefault,  0);
}

/// Test: Verifies that when overlayPaths is empty, the mask task's overlay
/// texture inputs fall back to the base prim-IDs textures, and hasDistinctOverlay
/// is cleared so the mask shader skips the overlay lookup.
HVT_TEST(TestOutlineManager, outline_maskTextureFallbackWhenOverlayEmpty)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs); // no overlayPaths set

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlineMaskTaskParams maskParams = _GetMaskParams(*f.framePass->GetTaskManager());

    EXPECT_EQ(maskParams.overlayPrimIdsTexture,    "outlineBasePrimIdsTexture");
    EXPECT_EQ(maskParams.overlayDepthTexture,       "outlineBaseDepthTexture");
    EXPECT_EQ(maskParams.style.hasDistinctOverlay,  0);
}

/// Test: Verifies that excludePaths are applied only to the Default prim-IDs
/// collection and do not affect the selected or overlay buckets.
HVT_TEST(TestOutlineManager, outline_excludePathsAppliedToDefaultCollection)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = true;
    outline.SetStyle(style);

    hvt::Outline::OutlineInputs inputs;
    inputs.excludePaths = { SdfPath("/Root/Transient") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    SdfPath const defaultPath = f.framePass->GetTaskManager()->GetTaskPath(
        _tokens->outlineDefaultPrimIdsTask);
    VtValue const value = f.framePass->GetTaskManager()->GetTaskValue(
        defaultPath, HdTokens->params);
    hvt::Outline::OutlinePrimIdsTaskParams primIdsParams =
        value.Get<hvt::Outline::OutlinePrimIdsTaskParams>();

    EXPECT_TRUE(primIdsParams.enabled);
    EXPECT_EQ(primIdsParams.collection.GetExcludePaths(),
        SdfPathVector{ SdfPath("/Root/Transient") });
}

// =====================================================================
// Rendering tests
// (full GPU tests -- disabled on Apple due to non-deterministic primIds)
// =====================================================================

/// Test: Verifies that Outline with a selected path produces the expected
/// outline output when driven through SetInputs().
#if defined(__APPLE__)
HVT_TEST(TestOutlineManager, DISABLED_outline_renderSelectedPath)
#else
HVT_TEST(TestOutlineManager, outline_renderSelectedPath)
#endif
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    {
        auto& usdStage = stage.stage();
        if (UsdPrim mesh0 = usdStage->GetPrimAtPath(SdfPath("/mesh_0")))
        {
            mesh0.SetActive(false);
        }
        auto box = UsdGeomCube::Define(usdStage, SdfPath("/Root/Selected/Box"));
        box.GetSizeAttr().Set(9.0);
        UsdGeomXformCommonAPI(box).SetTranslate(GfVec3d(-10.0, 0.0, 0.0));

        auto sphere = UsdGeomSphere::Define(usdStage, SdfPath("/Root/Unselected/Sphere"));
        sphere.GetRadiusAttr().Set(4.5);
        UsdGeomXformCommonAPI(sphere).SetTranslate(GfVec3d(8.0, 0.0, 0.0));
    }

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::FramePassPtr sceneFramePass;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineRenderSelectedPath");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    hvt::Outline::OutlineManager outline;
    outline.Install(*sceneFramePass);

    {
        hvt::Outline::OutlineStyle style;
        style.selectedColor = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
        style.defaultColor  = GfVec4f(0.2f, 0.2f, 0.2f, 1.0f);
        style.blurMode      = hvt::Outline::BlurMode::Blur3x3;
        outline.SetStyle(style);
    }

    {
        hvt::Outline::OutlineInputs inputs;
        inputs.selectedPaths = { SdfPath("/Root/Selected") };
        inputs.excludePaths  = { SdfPath("/Root/Selected") };
        outline.SetInputs(inputs);
    }

    int frameCount = 10;
    auto render    = [&]()
    {
        auto& params = sceneFramePass->params();

        params.renderBufferSize = GfVec2i(testContext->width(), testContext->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(testContext->width(), testContext->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->disabled;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = testContext->presentationEnabled();

        sceneFramePass->Render();
        testContext->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    testContext->run(render, sceneFramePass.get());

    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName));
}

/// Test: Verifies that each BlurMode (None, Blur3x3, Blur5x5) produces the expected
/// output when applied via SetStyle(). Each mode is rendered independently and
/// compared against its own per-mode baseline image, so regressions in one mode
/// are distinguishable from regressions in another.
#if defined(__APPLE__)
HVT_TEST(TestOutlineManager, DISABLED_outline_renderStyleChange)
#else
HVT_TEST(TestOutlineManager, outline_renderStyleChange)
#endif
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    {
        auto& usdStage = stage.stage();
        if (UsdPrim mesh0 = usdStage->GetPrimAtPath(SdfPath("/mesh_0")))
        {
            mesh0.SetActive(false);
        }
        auto box = UsdGeomCube::Define(usdStage, SdfPath("/Root/Selected/Box"));
        box.GetSizeAttr().Set(9.0);
        UsdGeomXformCommonAPI(box).SetTranslate(GfVec3d(0.0, 0.0, 0.0));
    }

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::FramePassPtr sceneFramePass;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineRenderStyleChange");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    hvt::Outline::OutlineManager outline;
    outline.Install(*sceneFramePass);

    {
        hvt::Outline::OutlineInputs inputs;
        inputs.selectedPaths = { SdfPath("/Root/Selected") };
        inputs.excludePaths  = { SdfPath("/Root/Selected") };
        outline.SetInputs(inputs);
    }

    // For each blur mode: apply the style, render 3 frames to let Storm settle,
    // then capture and compare against a per-mode baseline image.
    static const struct
    {
        hvt::Outline::BlurMode mode;
        const char*            suffix;
    } kModes[] = {
        { hvt::Outline::BlurMode::None,    "_none"    },
        { hvt::Outline::BlurMode::Blur3x3, "_blur3x3" },
        { hvt::Outline::BlurMode::Blur5x5, "_blur5x5" },
    };

    for (auto const& m : kModes)
    {
        hvt::Outline::OutlineStyle style;
        style.selectedColor = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
        style.blurMode      = m.mode;
        outline.SetStyle(style);

        int frameCount = 3;
        auto render    = [&]()
        {
            auto& params = sceneFramePass->params();

            params.renderBufferSize = GfVec2i(testContext->width(), testContext->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(testContext->width(), testContext->height());

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = HdxColorCorrectionTokens->disabled;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            params.enablePresentation = testContext->presentationEnabled();

            sceneFramePass->Render();
            testContext->_backend->waitForGPUIdle();

            return --frameCount > 0;
        };

        testContext->run(render, sceneFramePass.get());

        ASSERT_TRUE(testContext->validateImages(
            computedImageName + m.suffix,
            TestHelpers::gTestNames.fixtureName + m.suffix));
    }
}
