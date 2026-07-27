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

#include <pxr/base/gf/matrix4d.h>
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
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((outlineBasePrimIdsTask,    "outlineBasePrimIdsTask"))
    ((outlineOverlayPrimIdsTask, "outlineOverlayPrimIdsTask"))
    ((outlineDefaultPrimIdsTask, "outlineDefaultPrimIdsTask"))
    ((outlineMaskTask,           "outlineMaskTask"))
    ((outlineOverlayTask,        "outlineOverlayTask"))
);

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

// Helper: reads back the committed hvt::Outline::OutlinePrimIdsTaskParams for a named
// prim-IDs task (Base / Overlay / Default) from a TaskManager.
hvt::Outline::OutlinePrimIdsTaskParams _GetPrimIdsParams(
    hvt::TaskManager& taskManager, TfToken const& token)
{
    SdfPath const path  = taskManager.GetTaskPath(token);
    VtValue const value = taskManager.GetTaskValue(path, HdTokens->params);
    return value.Get<hvt::Outline::OutlinePrimIdsTaskParams>();
}

// Helper: reads back the hvt::Outline::OutlineOverlayTaskParams from a TaskManager.
hvt::Outline::OutlineOverlayTaskParams _GetOverlayParams(hvt::TaskManager& taskManager)
{
    SdfPath const path  = taskManager.GetTaskPath(_tokens->outlineOverlayTask);
    VtValue const value = taskManager.GetTaskValue(path, HdTokens->params);
    return value.Get<hvt::Outline::OutlineOverlayTaskParams>();
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
    b.enableDefaultOutlines = true;
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
    ASSERT_FALSE(style.enableDefaultOutlines);
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

/// Test: Verifies that maxInputPathCount tracks the largest number of
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
    ASSERT_EQ(stats.maxInputPathCount, 3u);
}

/// Test: Verifies that changing overlayPaths, excludePaths, or isHoverSelected each
/// independently triggers a cache miss. Complements outline_cacheMissOnChangedInputs
/// (selectedPaths) and outline_cacheStatsAccumulate (leadPath) so every field the
/// SetInputs() dedup compares is exercised.
HVT_TEST(TestOutlineManager, outline_cacheMissOnEachRemainingField)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs inputs;
    outline.SetInputs(inputs); // identical to default state -> hit

    inputs.overlayPaths = { SdfPath("/Root/Gizmo") };
    outline.SetInputs(inputs); // miss -- overlayPaths changed

    inputs.excludePaths = { SdfPath("/Root/Transient") };
    outline.SetInputs(inputs); // miss -- excludePaths changed

    inputs.isHoverSelected = true;
    outline.SetInputs(inputs); // miss -- isHoverSelected changed

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries, 4u);
    ASSERT_EQ(stats.hits,         1u);
    ASSERT_EQ(stats.misses,       3u);
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

/// Test: Exercises both branches of SetStyle()'s equality guard through the observable
/// commit->readback contract. A repeated identical SetStyle() (guard fires, early return)
/// must leave the committed style intact; a SetStyle() with a changed field (guard falls
/// through, re-assigns) must propagate. The dedup early-return is a CPU optimization with no
/// directly observable effect, so this guards the behavior it must preserve, not the branch.
HVT_TEST(TestOutlineManager, outline_setStyleDedup)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    auto& taskManager = *f.framePass->GetTaskManager();

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs);

    // Default style, applied twice. The second (identical) call hits the dedup early return;
    // the committed params must still carry the default softness.
    hvt::Outline::OutlineStyle style;
    outline.SetStyle(style);
    outline.SetStyle(style);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_FLOAT_EQ(_GetMaskParams(taskManager).style.softnessStrength, 1.0f);

    // Changed field -- guard falls through, re-assigns, propagates on commit.
    hvt::Outline::OutlineStyle changed = style;
    changed.softnessStrength = 0.5f;
    outline.SetStyle(changed);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_FLOAT_EQ(_GetMaskParams(taskManager).style.softnessStrength, 0.5f);

    // Repeat the changed style (dedup again) -- committed value stays 0.5.
    outline.SetStyle(changed);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_FLOAT_EQ(_GetMaskParams(taskManager).style.softnessStrength, 0.5f);
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

/// Test: The positive complement of the two fallback tests above. When overlayPaths is
/// non-empty AND enableDefaultOutlines is true, the mask task must reference the dedicated
/// overlay and default textures (not the base aliases) and set hasDistinctOverlay /
/// hasDistinctDefault so the mask shader performs both lookups.
HVT_TEST(TestOutlineManager, outline_maskTextureDistinctWhenOverlayAndDefaultPresent)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = true;
    outline.SetStyle(style);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    inputs.overlayPaths  = { SdfPath("/Root/Gizmo") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlineMaskTaskParams maskParams = _GetMaskParams(*f.framePass->GetTaskManager());

    EXPECT_EQ(maskParams.overlayPrimIdsTexture,   "outlineOverlayPrimIdsTexture");
    EXPECT_EQ(maskParams.overlayDepthTexture,     "outlineOverlayDepthTexture");
    EXPECT_EQ(maskParams.style.hasDistinctOverlay, 1);

    EXPECT_EQ(maskParams.defaultPrimIdsTexture,   "outlineDefaultPrimIdsTexture");
    EXPECT_EQ(maskParams.defaultDepthTexture,     "outlineDefaultDepthTexture");
    EXPECT_EQ(maskParams.style.hasDistinctDefault, 1);
}

/// Test: Verifies that every OutlineStyle field SetStyle() owns propagates into the
/// committed mask task parameters (colors, softness, visualization mode). SetStyle() is
/// the manager's sole path for theme changes, so a dropped field is a silent regression.
HVT_TEST(TestOutlineManager, outline_stylePropagatesToMaskParams)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.selectedColor           = GfVec4f(0.10f, 0.20f, 0.30f, 0.40f);
    style.selectedHoverColor      = GfVec4f(0.50f, 0.60f, 0.70f, 0.80f);
    style.selectionLeadColor      = GfVec4f(0.11f, 0.22f, 0.33f, 0.44f);
    style.selectionLeadHoverColor = GfVec4f(0.90f, 0.80f, 0.70f, 0.60f);
    style.overlayColor            = GfVec4f(0.15f, 0.25f, 0.35f, 0.45f);
    style.overlayHoverColor       = GfVec4f(0.55f, 0.65f, 0.75f, 0.85f);
    style.unselectedHoverColor    = GfVec4f(0.12f, 0.13f, 0.14f, 0.15f);
    style.defaultColor            = GfVec4f(0.21f, 0.22f, 0.23f, 0.24f);
    style.softnessStrength        = 0.33f;
    style.softnessFalloff         = 0.66f;
    style.maskVisualizationMode   = hvt::Outline::VisualizationMode::VISUALIZE_PRIM_IDS;
    outline.SetStyle(style);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlineMaskTaskParams maskParams = _GetMaskParams(*f.framePass->GetTaskManager());

    EXPECT_EQ(maskParams.style.selectedColor,           style.selectedColor);
    EXPECT_EQ(maskParams.style.selectedHoverColor,      style.selectedHoverColor);
    EXPECT_EQ(maskParams.style.selectionLeadColor,      style.selectionLeadColor);
    EXPECT_EQ(maskParams.style.selectionLeadHoverColor, style.selectionLeadHoverColor);
    EXPECT_EQ(maskParams.style.overlayColor,            style.overlayColor);
    EXPECT_EQ(maskParams.style.overlayHoverColor,       style.overlayHoverColor);
    EXPECT_EQ(maskParams.style.unselectedHoverColor,    style.unselectedHoverColor);
    EXPECT_EQ(maskParams.style.defaultColor,            style.defaultColor);
    EXPECT_FLOAT_EQ(maskParams.style.softnessStrength,  style.softnessStrength);
    EXPECT_FLOAT_EQ(maskParams.style.softnessFalloff,   style.softnessFalloff);
    EXPECT_EQ(maskParams.maskVisualizationMode,         style.maskVisualizationMode);
}

/// Test: Verifies that the SetInputs() path buckets and the isHoverSelected flag pass
/// through to the mask task parameters. The lead / hover / overlay ID counts are NOT
/// asserted here: the manager leaves them for OutlineMaskTask::_Sync() to resolve from the
/// render index (a path expands to a subtree of prim IDs), so they are only meaningful after
/// a render, not after a bare CommitTaskValues(). End-to-end count behavior is covered by the
/// render baseline tests and by the task's own tests in testOutlineTasks.cpp.
HVT_TEST(TestOutlineManager, outline_inputsPropagateToMaskParams)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths   = { SdfPath("/Root/Cube"), SdfPath("/Root/Sphere") };
    inputs.leadPath        = SdfPath("/Root/Cube");
    inputs.hoverPaths      = { SdfPath("/Root/Sphere") };
    inputs.overlayPaths    = { SdfPath("/Root/Gizmo"), SdfPath("/Root/Grid") };
    inputs.isHoverSelected = true;
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlineMaskTaskParams maskParams = _GetMaskParams(*f.framePass->GetTaskManager());

    EXPECT_EQ(maskParams.leadPath,     inputs.leadPath);
    EXPECT_EQ(maskParams.hoverPaths,   inputs.hoverPaths);
    EXPECT_EQ(maskParams.overlayPaths, inputs.overlayPaths);

    EXPECT_EQ(maskParams.style.isHoverSelected, 1);
}

/// Test: Verifies that the Base prim-IDs collection is built from the union of
/// selectedPaths and hoverPaths. leadPath is intentionally NOT added to the roots
/// (it is contractually a subset of selectedPaths) -- see OutlineManager.cpp.
HVT_TEST(TestOutlineManager, outline_baseCollectionUnionsSelectedAndHover)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    inputs.hoverPaths    = { SdfPath("/Root/Sphere") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlinePrimIdsTaskParams baseParams =
        _GetPrimIdsParams(*f.framePass->GetTaskManager(), _tokens->outlineBasePrimIdsTask);

    EXPECT_TRUE(baseParams.enabled);

    SdfPathVector roots = baseParams.collection.GetRootPaths();
    std::sort(roots.begin(), roots.end());
    SdfPathVector expected = { SdfPath("/Root/Cube"), SdfPath("/Root/Sphere") };
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(roots, expected);
}

/// Test: Verifies the per-bucket enabled logic. With only selectedPaths set and
/// enableDefaultOutlines disabled: Base is enabled (has selection), Overlay is
/// disabled (no overlayPaths), and Default is disabled (default outlines off).
HVT_TEST(TestOutlineManager, outline_perBucketEnabledFlags)
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

    auto& taskManager = *f.framePass->GetTaskManager();
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).enabled);
    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineOverlayPrimIdsTask).enabled);
    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask).enabled);
}

/// Test: Exercises the per-task derived-collection cache (keyed on inputsGeneration in
/// OutlineManager.cpp). This cache is the one piece of the manager's caching that host-side
/// caching (dirty flags, cached selection state) does NOT subsume: the task commit callbacks
/// run on every frame regardless of how the host gates SetInputs(), and the cache stops each
/// commit from rebuilding the HdRprimCollection when the inputs are unchanged.
///
/// A "rebuild happened" event is an internal CPU detail and is not directly observable
/// through the public API (a rebuilt collection is value-equal to a reused one). What IS
/// observable -- and what this test guards -- is the contract the generation cache must
/// uphold: the committed Base collection stays stable across repeated commits with unchanged
/// inputs (the cache is reused, never going stale or empty) AND is rebuilt to reflect the
/// new paths once SetInputs() bumps the generation. The main regression this catches is a
/// cache that never invalidates: it would leave the stale collection in the final step.
HVT_TEST(TestOutlineManager, outline_collectionCacheStableAcrossCommitsAndInvalidatesOnChange)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    auto& taskManager = *f.framePass->GetTaskManager();

    auto baseRoots = [&taskManager]() {
        SdfPathVector roots = _GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask)
                                  .collection.GetRootPaths();
        std::sort(roots.begin(), roots.end());
        return roots;
    };

    // First (non-empty) inputs -> miss. Commit, then commit again without touching inputs:
    // the second commit must reuse the cached collection and stay value-stable.
    hvt::Outline::OutlineInputs first;
    first.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(first);

    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_EQ(baseRoots(), SdfPathVector{ SdfPath("/Root/Cube") });

    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_EQ(baseRoots(), SdfPathVector{ SdfPath("/Root/Cube") });

    // A no-op SetInputs (identical) is a cache hit and must not disturb the committed roots.
    outline.SetInputs(first);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_EQ(baseRoots(), SdfPathVector{ SdfPath("/Root/Cube") });

    // Changed inputs -> miss -> generation bump. The next commit MUST rebuild the collection
    // so it reflects the new paths (proves the cache invalidates rather than going stale).
    hvt::Outline::OutlineInputs second;
    second.selectedPaths = { SdfPath("/Root/Sphere") };
    outline.SetInputs(second);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_EQ(baseRoots(), SdfPathVector{ SdfPath("/Root/Sphere") });

    // Sanity-check the stats reflect the exercised path: 3 queries, 1 hit (the repeat), 2 misses.
    auto stats = outline.GetCacheStats();
    EXPECT_EQ(stats.totalQueries, 3u);
    EXPECT_EQ(stats.hits,         1u);
    EXPECT_EQ(stats.misses,       2u);
}

// =====================================================================
// Outline::Install -- atPos / order anchor placement
// (the atPos + order parameters are otherwise never exercised)
// =====================================================================

/// Test: Verifies that when Install() is given an explicit anchor (atPos) with
/// insertBefore, the whole outline group lands before that anchor task while the
/// three tasks keep their fixed internal order (prim-IDs -> mask -> overlay).
HVT_TEST(TestOutlineManager, outline_installAnchorInsertBefore)
{
    OutlineFixture f;
    hvt::Outline::OutlineManager outline;

    auto& taskManager = *f.framePass->GetTaskManager();

    // colorCorrectionTask is one of the default frame-pass tasks; use it as anchor.
    SdfPath const anchorPath = taskManager.GetTaskPath(HdxPrimitiveTokens->colorCorrectionTask);
    ASSERT_FALSE(anchorPath.IsEmpty());

    outline.Install(*f.framePass, anchorPath, hvt::TaskManager::InsertionOrder::insertBefore);

    SdfPathVector taskPaths;
    taskManager.GetTaskPaths(hvt::TaskFlagsBits::kExecutableBit, false, taskPaths);

    auto indexOf = [&taskPaths](SdfPath const& path) {
        auto it = std::find(taskPaths.begin(), taskPaths.end(), path);
        EXPECT_NE(it, taskPaths.end());
        return static_cast<size_t>(std::distance(taskPaths.begin(), it));
    };

    size_t const anchorIdx  = indexOf(anchorPath);
    size_t const baseIdx    = indexOf(taskManager.GetTaskPath(_tokens->outlineBasePrimIdsTask));
    size_t const overlayPIdx =
        indexOf(taskManager.GetTaskPath(_tokens->outlineOverlayPrimIdsTask));
    size_t const defaultIdx = indexOf(taskManager.GetTaskPath(_tokens->outlineDefaultPrimIdsTask));
    size_t const maskIdx    = indexOf(taskManager.GetTaskPath(_tokens->outlineMaskTask));
    size_t const overlayIdx = indexOf(taskManager.GetTaskPath(_tokens->outlineOverlayTask));

    // Whole group precedes the anchor.
    EXPECT_LT(baseIdx,     anchorIdx);
    EXPECT_LT(overlayPIdx, anchorIdx);
    EXPECT_LT(defaultIdx,  anchorIdx);
    EXPECT_LT(maskIdx,     anchorIdx);
    EXPECT_LT(overlayIdx,  anchorIdx);

    // Fixed internal order is preserved regardless of the anchor.
    EXPECT_LT(baseIdx,     maskIdx);
    EXPECT_LT(overlayPIdx, maskIdx);
    EXPECT_LT(defaultIdx,  maskIdx);
    EXPECT_LT(maskIdx,     overlayIdx);
}

/// Test: Verifies that Install() with an anchor and insertAfter places the whole
/// outline group after that anchor task, internal order still preserved.
HVT_TEST(TestOutlineManager, outline_installAnchorInsertAfter)
{
    OutlineFixture f;
    hvt::Outline::OutlineManager outline;

    auto& taskManager = *f.framePass->GetTaskManager();

    SdfPath const anchorPath = taskManager.GetTaskPath(HdxPrimitiveTokens->colorCorrectionTask);
    ASSERT_FALSE(anchorPath.IsEmpty());

    outline.Install(*f.framePass, anchorPath, hvt::TaskManager::InsertionOrder::insertAfter);

    SdfPathVector taskPaths;
    taskManager.GetTaskPaths(hvt::TaskFlagsBits::kExecutableBit, false, taskPaths);

    auto indexOf = [&taskPaths](SdfPath const& path) {
        auto it = std::find(taskPaths.begin(), taskPaths.end(), path);
        EXPECT_NE(it, taskPaths.end());
        return static_cast<size_t>(std::distance(taskPaths.begin(), it));
    };

    size_t const anchorIdx  = indexOf(anchorPath);
    size_t const baseIdx    = indexOf(taskManager.GetTaskPath(_tokens->outlineBasePrimIdsTask));
    size_t const maskIdx    = indexOf(taskManager.GetTaskPath(_tokens->outlineMaskTask));
    size_t const overlayIdx = indexOf(taskManager.GetTaskPath(_tokens->outlineOverlayTask));

    EXPECT_GT(baseIdx,    anchorIdx);
    EXPECT_GT(maskIdx,    anchorIdx);
    EXPECT_GT(overlayIdx, anchorIdx);

    EXPECT_LT(baseIdx, maskIdx);
    EXPECT_LT(maskIdx, overlayIdx);
}

/// Test: With an empty atPos, insertBefore / insertAfter degrade to insertAtEnd (the fallback
/// documented on Install(): with no anchor there is nothing to sit before/after). The five
/// tasks must still install and keep their fixed internal order (prim-IDs -> mask -> overlay).
/// This guards the documented fallback against a future change in how the TaskManager handles
/// an empty anchor.
HVT_TEST(TestOutlineManager, outline_installEmptyAnchorFallsBackToEnd)
{
    hvt::TaskManager::InsertionOrder const orders[] = {
        hvt::TaskManager::InsertionOrder::insertBefore,
        hvt::TaskManager::InsertionOrder::insertAfter,
    };

    for (auto order : orders)
    {
        OutlineFixture f;
        hvt::Outline::OutlineManager outline;

        outline.Install(*f.framePass, SdfPath(), order); // empty anchor

        auto& taskManager = *f.framePass->GetTaskManager();

        EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Base")));
        EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Overlay")));
        EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Default")));
        EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineMaskTask::GetToken()));
        EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineOverlayTask::GetToken()));

        SdfPathVector taskPaths;
        taskManager.GetTaskPaths(hvt::TaskFlagsBits::kExecutableBit, false, taskPaths);

        auto indexOf = [&taskPaths](SdfPath const& path) {
            auto it = std::find(taskPaths.begin(), taskPaths.end(), path);
            EXPECT_NE(it, taskPaths.end());
            return static_cast<size_t>(std::distance(taskPaths.begin(), it));
        };

        size_t const baseIdx    = indexOf(taskManager.GetTaskPath(_tokens->outlineBasePrimIdsTask));
        size_t const overlayPIdx =
            indexOf(taskManager.GetTaskPath(_tokens->outlineOverlayPrimIdsTask));
        size_t const defaultIdx = indexOf(taskManager.GetTaskPath(_tokens->outlineDefaultPrimIdsTask));
        size_t const maskIdx    = indexOf(taskManager.GetTaskPath(_tokens->outlineMaskTask));
        size_t const overlayIdx = indexOf(taskManager.GetTaskPath(_tokens->outlineOverlayTask));

        EXPECT_LT(baseIdx,     maskIdx);
        EXPECT_LT(overlayPIdx, maskIdx);
        EXPECT_LT(defaultIdx,  maskIdx);
        EXPECT_LT(maskIdx,     overlayIdx);
    }
}

// =====================================================================
// Outline lifetime -- destroy-then-commit safety
// (the shared_ptr / weak_ptr no-op contract documented on the header)
// =====================================================================

/// Test: Verifies the manager's core lifetime contract: if the OutlineManager is
/// destroyed while its tasks are still installed in the frame pass, the next commit
/// must safely no-op (the commit callbacks lock() a weak_ptr that now fails) rather
/// than dereference freed state. The installed tasks outlive the manager because they
/// are owned by the frame pass's TaskManager, not by the manager.
HVT_TEST(TestOutlineManager, outline_destroyManagerBeforeCommitIsSafe)
{
    OutlineSceneFixture f;
    auto& taskManager = *f.framePass->GetTaskManager();

    {
        hvt::Outline::OutlineManager outline;
        outline.Install(*f.framePass);

        hvt::Outline::OutlineInputs inputs;
        inputs.selectedPaths = { SdfPath("/Root/Cube") };
        outline.SetInputs(inputs);

        taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    } // manager destroyed here; its tasks remain installed in the frame pass

    // The next commit must run the callbacks' weak_ptr lock()-fail path and no-op
    // without crashing.
    EXPECT_NO_THROW(taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit));

    // Tasks are still present (frame pass owns them, not the destroyed manager).
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Base")));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineMaskTask::GetToken()));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineOverlayTask::GetToken()));
}

// =====================================================================
// Outline internals -- overlay task parameters, enabled logic, edge inputs
// =====================================================================

/// Test: Verifies that the overlay task's params reflect the enabled state and that
/// blurMode / blurIntensity (consumed ONLY by the overlay task) propagate from SetStyle().
/// No other test reads OutlineOverlayTaskParams back, so blurIntensity propagation is
/// otherwise unverified at the parameter level.
HVT_TEST(TestOutlineManager, outline_overlayParamsEnabledAndBlurPropagate)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.blurMode      = hvt::Outline::BlurMode::Blur5x5;
    style.blurIntensity = 2.0f;
    outline.SetStyle(style);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs);

    f.framePass->GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlineOverlayTaskParams overlayParams =
        _GetOverlayParams(*f.framePass->GetTaskManager());

    EXPECT_TRUE(overlayParams.enabled); // has a selection
    EXPECT_EQ(overlayParams.blurMode, hvt::Outline::BlurMode::Blur5x5);
    EXPECT_FLOAT_EQ(overlayParams.blurIntensity, 2.0f);
}

/// Test: The "nothing to draw" state. With no path inputs AND enableDefaultOutlines
/// disabled, every prim-IDs task, the mask task, and the overlay task must all report
/// enabled == false. This is the only state in which the whole outline group is inert.
HVT_TEST(TestOutlineManager, outline_nothingEnabledWhenAllInputsEmpty)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = false;
    outline.SetStyle(style);
    outline.SetInputs(hvt::Outline::OutlineInputs{}); // all buckets empty

    auto& taskManager = *f.framePass->GetTaskManager();
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).enabled);
    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineOverlayPrimIdsTask).enabled);
    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask).enabled);
    EXPECT_FALSE(_GetMaskParams(taskManager).enabled);
    EXPECT_FALSE(_GetOverlayParams(taskManager).enabled);
}

/// Test: The Base prim-IDs task is enabled when ONLY overlayPaths is set (one of the
/// four OR-conditions in its enabledFn), even though the base collection roots are empty.
/// Overlay prim-IDs is enabled too; Default stays off with default outlines disabled.
HVT_TEST(TestOutlineManager, outline_baseEnabledFromOverlayPathsAlone)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = false;
    outline.SetStyle(style);

    hvt::Outline::OutlineInputs inputs;
    inputs.overlayPaths = { SdfPath("/Root/Gizmo") }; // no selection / hover
    outline.SetInputs(inputs);

    auto& taskManager = *f.framePass->GetTaskManager();
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).enabled);
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineOverlayPrimIdsTask).enabled);
    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask).enabled);
}

/// Test: The Base prim-IDs task (and Default) are enabled purely because
/// enableDefaultOutlines is true, with no path inputs at all. Overlay stays off.
HVT_TEST(TestOutlineManager, outline_baseEnabledFromDefaultOutlinesAlone)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = true; // opt-in (defaults to false)
    outline.SetStyle(style);
    outline.SetInputs(hvt::Outline::OutlineInputs{}); // no paths

    auto& taskManager = *f.framePass->GetTaskManager();
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).enabled);
    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineOverlayPrimIdsTask).enabled);
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask).enabled);
}

/// Test: Guards the documented leadPath contract (OutlineInputs::leadPath). A leadPath
/// that is NOT one of selectedPaths / hoverPaths is intentionally NOT added to the Base
/// collection roots (so it would silently not draw), yet it still passes through to the
/// mask params. (leadIdsCount is resolved from the render index by OutlineMaskTask::_Sync(),
/// not by the manager, so it is not asserted after a bare commit.)
HVT_TEST(TestOutlineManager, outline_leadPathNotAddedToBaseRoots)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    inputs.leadPath      = SdfPath("/Root/Lead"); // deliberately not in selectedPaths
    outline.SetInputs(inputs);

    auto& taskManager = *f.framePass->GetTaskManager();
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    SdfPathVector roots =
        _GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).collection.GetRootPaths();
    EXPECT_EQ(roots, SdfPathVector{ SdfPath("/Root/Cube") }); // lead is absent

    hvt::Outline::OutlineMaskTaskParams maskParams = _GetMaskParams(taskManager);
    EXPECT_EQ(maskParams.leadPath, SdfPath("/Root/Lead"));
}

/// Test: With a selection but no leadPath, the leadPath passes through to the mask params
/// empty (complements outline_inputsPropagateToMaskParams, which sets one).
HVT_TEST(TestOutlineManager, outline_leadPathEmptyWhenNoLead)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") }; // no leadPath
    outline.SetInputs(inputs);

    auto& taskManager = *f.framePass->GetTaskManager();
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlineMaskTaskParams maskParams = _GetMaskParams(taskManager);
    EXPECT_TRUE(maskParams.leadPath.IsEmpty());
}

/// Test: Verifies that the default (whole-scene) prim-IDs collection is rooted at the
/// absolute root and carries no exclude paths when excludePaths is empty.
HVT_TEST(TestOutlineManager, outline_defaultCollectionRootIsAbsoluteRootNoExclude)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = true;
    outline.SetStyle(style);
    outline.SetInputs(hvt::Outline::OutlineInputs{}); // no excludePaths

    auto& taskManager = *f.framePass->GetTaskManager();
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlinePrimIdsTaskParams defaultParams =
        _GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask);

    EXPECT_EQ(defaultParams.collection.GetRootPaths(),
        SdfPathVector{ SdfPath::AbsoluteRootPath() });
    EXPECT_TRUE(defaultParams.collection.GetExcludePaths().empty());
}

/// Test: Verifies every VisualizationMode value round-trips through SetStyle() into the
/// committed mask params (outline_stylePropagatesToMaskParams only covers one mode).
HVT_TEST(TestOutlineManager, outline_styleVisualizationModePropagatesAllModes)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs);

    auto& taskManager = *f.framePass->GetTaskManager();

    hvt::Outline::VisualizationMode const modes[] = {
        hvt::Outline::VisualizationMode::VISUALIZE_MASK_3x3,
        hvt::Outline::VisualizationMode::VISUALIZE_MASK_5x5,
        hvt::Outline::VisualizationMode::VISUALIZE_PRIM_IDS,
        hvt::Outline::VisualizationMode::VISUALIZE_DEPTH,
    };

    for (auto mode : modes)
    {
        hvt::Outline::OutlineStyle style;
        style.maskVisualizationMode = mode;
        outline.SetStyle(style);

        taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

        EXPECT_EQ(_GetMaskParams(taskManager).maskVisualizationMode, mode);
    }
}

// =====================================================================
// Outline::SetInputs -- cache statistics (avg / multi-bucket size)
// =====================================================================

/// Test: Verifies avgInputPathCount (never asserted elsewhere) tracks the mean per-call
/// total path count across EVERY query -- hits included, not just misses -- alongside
/// maxInputPathCount. The final repeat of `c` is a cache hit whose size (2) still
/// contributes to the average, guarding against the average being computed over misses only.
/// Sizes 1, 3, 2, 2 across four queries (three misses + one hit) -> mean (1+3+2+2)/4 = 2, max 3.
HVT_TEST(TestOutlineManager, outline_cacheAvgCollectionSize)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs a;
    a.selectedPaths = { SdfPath("/a") }; // size 1
    outline.SetInputs(a); // miss

    hvt::Outline::OutlineInputs b;
    b.selectedPaths = { SdfPath("/b"), SdfPath("/c"), SdfPath("/d") }; // size 3
    outline.SetInputs(b); // miss

    hvt::Outline::OutlineInputs c;
    c.selectedPaths = { SdfPath("/e"), SdfPath("/f") }; // size 2
    outline.SetInputs(c); // miss
    outline.SetInputs(c); // hit -- size 2 still counts toward the average

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries,      4u);
    ASSERT_EQ(stats.misses,            3u);
    ASSERT_EQ(stats.hits,              1u);
    EXPECT_EQ(stats.maxInputPathCount, 3u);
    EXPECT_EQ(stats.avgInputPathCount, 2u); // (1 + 3 + 2 + 2) / 4
}

/// Test: Verifies maxInputPathCount sums the selected + hover + overlay + lead buckets
/// (the outline_cacheMaxCollectionSize test only ever drives it via selectedPaths), and
/// that excludePaths is deliberately excluded from that total.
HVT_TEST(TestOutlineManager, outline_maxInputPathCountCountsAllBucketsExceptExclude)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/s0"), SdfPath("/s1") };        // 2
    inputs.hoverPaths    = { SdfPath("/h0") };                       // 1
    inputs.overlayPaths  = { SdfPath("/o0"), SdfPath("/o1") };       // 2
    inputs.leadPath      = SdfPath("/s0");                           // 1
    inputs.excludePaths  = { SdfPath("/x0"), SdfPath("/x1"), SdfPath("/x2") }; // not counted
    outline.SetInputs(inputs);

    auto stats = outline.GetCacheStats();
    EXPECT_EQ(stats.maxInputPathCount, 6u); // 2 + 1 + 2 + 1, excludePaths ignored
}

/// Test: A freshly constructed manager, before any SetInputs(), reports all-zero stats.
HVT_TEST(TestOutlineManager, outline_freshManagerCacheStatsAreZero)
{
    hvt::Outline::OutlineManager outline;

    auto stats = outline.GetCacheStats();
    EXPECT_EQ(stats.totalQueries,      0u);
    EXPECT_EQ(stats.hits,              0u);
    EXPECT_EQ(stats.misses,            0u);
    EXPECT_EQ(stats.maxInputPathCount, 0u);
    EXPECT_EQ(stats.avgInputPathCount, 0u);
}

// =====================================================================
// Outline ordering -- SetStyle / SetInputs called before Install
// =====================================================================

/// Test: Verifies state pushed BEFORE Install() is honored. The manager stores style and
/// inputs from construction, so Install()'s initial overlay params snapshot the pre-set
/// blurMode (before any commit), and the first commit propagates the full pre-set style
/// and inputs into the mask params.
HVT_TEST(TestOutlineManager, outline_setStyleAndInputsBeforeInstall)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineStyle style;
    style.blurMode      = hvt::Outline::BlurMode::Blur5x5;
    style.selectedColor = GfVec4f(0.10f, 0.20f, 0.30f, 0.40f);
    outline.SetStyle(style);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    inputs.overlayPaths  = { SdfPath("/Root/Gizmo") };
    outline.SetInputs(inputs);

    outline.Install(*f.framePass); // install AFTER state was pushed

    auto& taskManager = *f.framePass->GetTaskManager();

    // Initial overlay params (captured at Install time, before any commit) reflect the
    // pre-set blur mode.
    EXPECT_EQ(_GetOverlayParams(taskManager).blurMode, hvt::Outline::BlurMode::Blur5x5);

    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    hvt::Outline::OutlineMaskTaskParams maskParams = _GetMaskParams(taskManager);
    EXPECT_EQ(maskParams.style.selectedColor,      style.selectedColor);
    EXPECT_EQ(maskParams.overlayPaths,             inputs.overlayPaths);
    EXPECT_EQ(maskParams.style.hasDistinctOverlay, 1);
    EXPECT_EQ(_GetOverlayParams(taskManager).blurMode, hvt::Outline::BlurMode::Blur5x5);
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

            params.viewInfo.viewMatrix = stage.viewMatrix();

            // Zoom the camera 2x (scale clip-space x/y about the screen centre) so the box
            // outline occupies enough pixels for the None / 3x3 / 5x5 blur differences to be
            // visible in the baseline images -- at 1x the outline is too thin to distinguish.
            GfMatrix4d zoom(1.0);
            zoom[0][0] = 2.0;
            zoom[1][1] = 2.0;
            params.viewInfo.projectionMatrix = stage.projectionMatrix() * zoom;
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
