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

#include <pxr/pxr.h>

#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <exception>
#include <functional>
#include <string>
#include <vector>

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

// Helper: commits the task values and reads back the Base collection roots, sorted so the
// comparison is order-independent.
SdfPathVector _GetSortedBaseRoots(hvt::FramePass& framePass)
{
    framePass.GetTaskManager()->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    SdfPathVector roots =
        _GetPrimIdsParams(*framePass.GetTaskManager(), _tokens->outlineBasePrimIdsTask)
            .collection.GetRootPaths();
    std::sort(roots.begin(), roots.end());
    return roots;
}

// Records the warnings posted while in scope, and keeps them out of the test output, so that a test
// that triggers a warning on purpose can check it.
class ScopedWarningCapture : public TfDiagnosticMgr::Delegate
{
public:
    ScopedWarningCapture()
    {
        TfDiagnosticMgr::GetInstance().AddDelegate(this);
        TfDiagnosticMgr::GetInstance().SetQuiet(true);
    }
    ~ScopedWarningCapture() override
    {
        TfDiagnosticMgr::GetInstance().SetQuiet(false);
        TfDiagnosticMgr::GetInstance().RemoveDelegate(this);
    }
    ScopedWarningCapture(ScopedWarningCapture const&)            = delete;
    ScopedWarningCapture& operator=(ScopedWarningCapture const&) = delete;

    std::vector<std::string> const& GetWarnings() const { return _warnings; }

    void IssueError(TfError const&) override {}
    void IssueFatalError(TfCallContext const&, std::string const&) override {}
    void IssueStatus(TfStatus const&) override {}
    void IssueWarning(TfWarning const& warning) override
    {
        _warnings.push_back(warning.GetCommentary());
    }

private:
    std::vector<std::string> _warnings;
};

// The image of three touching instances with instances 0 and 1 selected, which several render
// tests below share as their baseline.
constexpr char kTouchingInstanceTargetsBaseline[] = "outline_renderTouchingInstanceTargets";

// Colors told apart in the per-instance render tests below.
hvt::Outline::OutlineStyle _GetInstanceTestStyle()
{
    hvt::Outline::OutlineStyle style;
    style.selectedColor        = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
    style.selectionLeadColor   = GfVec4f(0.2f, 1.0f, 0.2f, 1.0f);
    style.selectedHoverColor   = GfVec4f(1.0f, 0.5f, 0.0f, 1.0f);
    style.unselectedHoverColor = GfVec4f(1.0f, 0.2f, 1.0f, 1.0f);
    style.blurMode             = hvt::Outline::BlurMode::Blur3x3;
    return style;
}

// One step of _RenderTouchingInstanceSteps: an optional stage edit, the inputs pushed before the
// step renders, and the baseline its image is compared with (none when empty).
struct InstanceRenderStep
{
    hvt::Outline::OutlineInputs inputs;
    std::string baseline;
    std::function<void(UsdStageRefPtr const&)> edit;
};

// Renders three touching cubes of size 6, drawn by the point instancer /Root/PI from one prototype
// rprim (instances 0, 1, 2 at x = -6, 0, 6), once per step, on one frame pass and one
// OutlineManager. Every step runs even after a mismatch, so that one run writes every computed
// image. Returns true when every compared image matches its baseline.
bool _RenderTouchingInstanceSteps(std::vector<InstanceRenderStep> const& steps,
    std::string const& computedImageName, SdfPath const& framePassUid)
{
    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    if (!stage.open(testContext->_sceneFilepath))
    {
        ADD_FAILURE() << "Cannot open " << testContext->_sceneFilepath;
        return false;
    }

    {
        auto& usdStage = stage.stage();
        if (UsdPrim mesh0 = usdStage->GetPrimAtPath(SdfPath("/mesh_0")))
        {
            mesh0.SetActive(false);
        }

        auto instancer = UsdGeomPointInstancer::Define(usdStage, SdfPath("/Root/PI"));
        auto cube      = UsdGeomCube::Define(usdStage, SdfPath("/Root/PI/Protos/Cube"));
        cube.GetSizeAttr().Set(6.0);
        instancer.CreatePrototypesRel().AddTarget(cube.GetPath());
        instancer.CreateProtoIndicesAttr().Set(VtIntArray { 0, 0, 0 });
        instancer.CreatePositionsAttr().Set(VtVec3fArray {
            GfVec3f(-6.0f, 0.0f, 0.0f), GfVec3f(0.0f, 0.0f, 0.0f), GfVec3f(6.0f, 0.0f, 0.0f) });
    }

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::FramePassPtr sceneFramePass;
    UsdImagingStageSceneIndexRefPtr stageSceneIndex;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        // Keep the stage scene index: a stage edit reaches the render index only through
        // UpdateUSDSceneIndex(), which needs it.
        UsdImagingSceneIndices const sceneIndices =
            hvt::ViewportEngine::CreateUSDSceneIndices(stage.stage());
        stageSceneIndex = sceneIndices.stageSceneIndex;
        if (!stageSceneIndex)
        {
            ADD_FAILURE() << "No stage scene index";
            return false;
        }
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(
            sceneIndices.finalSceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = framePassUid;
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    hvt::Outline::OutlineManager outline;
    outline.Install(*sceneFramePass);
    outline.SetStyle(_GetInstanceTestStyle());

    bool allMatch = true;
    for (size_t i = 0; i < steps.size(); ++i)
    {
        InstanceRenderStep const& step = steps[i];
        if (step.edit)
        {
            step.edit(stage.stage());
            hvt::ViewportEngine::UpdateUSDSceneIndex(stageSceneIndex, UsdTimeCode::EarliestTime());
        }
        outline.SetInputs(step.inputs);

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

        // A single step keeps the test's own image name.
        std::string const computed =
            steps.size() == 1 ? computedImageName : computedImageName + "_" + std::to_string(i);
        if (step.baseline.empty())
        {
            allMatch = testContext->_backend->saveImage(computed) && allMatch;
        }
        else
        {
            // validateImages() throws on a mismatch or a missing baseline.
            try
            {
                allMatch = testContext->validateImages(computed, step.baseline) && allMatch;
            }
            catch (std::exception const& e)
            {
                ADD_FAILURE() << "Step " << i << ": " << e.what();
                allMatch = false;
            }
        }
    }

    return allMatch;
}

// Renders the touching cubes outlined with the given inputs, and compares the image with the test
// baseline.
bool _RenderTouchingInstances(hvt::Outline::OutlineInputs const& inputs,
    std::string const& computedImageName, SdfPath const& framePassUid)
{
    return _RenderTouchingInstanceSteps(
        { { inputs, TestHelpers::gTestNames.fixtureName, {} } }, computedImageName, framePassUid);
}

// The first instancer of a prim's instancedBy, or the empty path.
SdfPath _GetFirstInstancedBy(HdSceneIndexBaseRefPtr const& sceneIndex, SdfPath const& path)
{
    HdPathArrayDataSourceHandle const pathsDs =
        HdInstancedBySchema::GetFromParent(sceneIndex->GetPrim(path).dataSource).GetPaths();
    if (!pathsDs)
    {
        return {};
    }
    VtArray<SdfPath> const paths = pathsDs->GetTypedValue(0.0f);
    return paths.empty() ? SdfPath() : paths[0];
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

/// Test: Verifies that calling Install() a second time is ignored with a warning
/// and does not duplicate tasks in the frame pass.
HVT_TEST(TestOutlineManager, outline_installTwiceIsNoop)
{
    OutlineFixture f;
    hvt::Outline::OutlineManager outline;

    outline.Install(*f.framePass);
    {
        ScopedWarningCapture warnings;
        outline.Install(*f.framePass);
        EXPECT_EQ(warnings.GetWarnings().size(), 1u);
    }

    auto& taskManager = f.framePass->GetTaskManager();
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Base")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Overlay")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlinePrimIdsTask ::GetToken("Default")));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlineMaskTask::GetToken()));
    ASSERT_TRUE(taskManager->HasTask(hvt::Outline::OutlineOverlayTask::GetToken()));
}

/// Test: Verifies that a second OutlineManager installing into a pass that already has outline
/// tasks is refused and leaves the pass untouched. The task names carry no per-instance suffix, so
/// an accepted second install would fail every AddTask yet still record itself as installed, and
/// its SetInputs() / SetStyle() would silently go nowhere.
HVT_TEST(TestOutlineManager, outline_installSecondManagerOnSamePassIsRefused)
{
    OutlineSceneFixture f;
    auto& taskManager = *f.framePass->GetTaskManager();

    hvt::Outline::OutlineManager first;
    first.Install(*f.framePass);

    {
        hvt::Outline::OutlineManager second;

        // Install() must refuse before reaching AddTask, which would post a TF_CODING_ERROR per
        // task. The harness only prints those, so assert on the error list instead: the mark is
        // clean only if nothing was posted while it was in scope.
        {
            TfErrorMark mark;
            ScopedWarningCapture warnings;
            second.Install(*f.framePass);
            EXPECT_TRUE(mark.IsClean());
            EXPECT_EQ(warnings.GetWarnings().size(), 1u);
            mark.Clear(); // on failure, keep the errors from surfacing again at teardown
        }

        hvt::Outline::OutlineInputs ignored;
        ignored.overlayPaths = { SdfPath("/Root/Other") };
        second.SetInputs(ignored);
    }

    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineMaskTask::GetToken()));

    // Only the first manager's inputs reach the tasks.
    hvt::Outline::OutlineInputs inputs;
    inputs.overlayPaths = { SdfPath("/Root/Cube") };
    first.SetInputs(inputs);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_EQ(_GetMaskParams(taskManager).overlayPaths, inputs.overlayPaths);
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
/// the separate default-pass textures. OutlineMaskTask::Execute() derives
/// hasDistinctDefault from these names, so they are the committed contract.
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

    EXPECT_EQ(maskParams.defaultPrimIdsTexture, "outlineBasePrimIdsTexture");
    EXPECT_EQ(maskParams.defaultDepthTexture, "outlineBaseDepthTexture");
    EXPECT_EQ(maskParams.defaultPrimIdsTexture, maskParams.basePrimIdsTexture);
    EXPECT_EQ(maskParams.defaultDepthTexture, maskParams.baseDepthTexture);
}

/// Test: Verifies that when overlayPaths is empty, the mask task's overlay
/// texture inputs fall back to the base prim-IDs textures. That aliasing is what makes
/// OutlineMaskTask::Execute() clear hasDistinctOverlay, so the shader skips the overlay lookup.
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

    EXPECT_EQ(maskParams.overlayPrimIdsTexture, "outlineBasePrimIdsTexture");
    EXPECT_EQ(maskParams.overlayDepthTexture, "outlineBaseDepthTexture");
    EXPECT_EQ(maskParams.overlayPrimIdsTexture, maskParams.basePrimIdsTexture);
    EXPECT_EQ(maskParams.overlayDepthTexture, maskParams.baseDepthTexture);
}

/// Test: The mask looks up the Base instance IDs under the name the Base pass publishes them as.
/// The texture is optional (published only while instance isolation is active, see
/// OutlinePrimIdsTaskParams::targets), so the name is committed with or without isolation, and
/// OutlineMaskTask::Execute() derives hasBaseInstanceIds from its presence in the task context.
HVT_TEST(TestOutlineManager, outline_maskBaseInstanceIdsTextureName)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    auto& taskManager = *f.framePass->GetTaskManager();

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    outline.SetInputs(inputs);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_EQ(_GetMaskParams(taskManager).baseInstanceIdsTexture, "outlineBaseInstanceIdsTexture");

    inputs.selectedTargets = { { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 0, 1 } } } } };
    outline.SetInputs(inputs);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_EQ(_GetMaskParams(taskManager).baseInstanceIdsTexture, "outlineBaseInstanceIdsTexture");
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
/// overlay and default textures rather than the base aliases, which is what makes
/// OutlineMaskTask::Execute() set hasDistinctOverlay / hasDistinctDefault for both lookups.
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

    EXPECT_EQ(maskParams.overlayPrimIdsTexture, "outlineOverlayPrimIdsTexture");
    EXPECT_EQ(maskParams.overlayDepthTexture, "outlineOverlayDepthTexture");
    EXPECT_NE(maskParams.overlayPrimIdsTexture, maskParams.basePrimIdsTexture);
    EXPECT_NE(maskParams.overlayDepthTexture, maskParams.baseDepthTexture);

    EXPECT_EQ(maskParams.defaultPrimIdsTexture, "outlineDefaultPrimIdsTexture");
    EXPECT_EQ(maskParams.defaultDepthTexture, "outlineDefaultDepthTexture");
    EXPECT_NE(maskParams.defaultPrimIdsTexture, maskParams.basePrimIdsTexture);
    EXPECT_NE(maskParams.defaultDepthTexture, maskParams.baseDepthTexture);
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
/// (it only recolors prim IDs already rasterized there) -- see OutlineManager.cpp.
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

/// Test: A hovered path that is already selected (the isHoverSelected state) appears in both
/// buckets but collapses to a single Base collection root, so the roots vector stays stable.
HVT_TEST(TestOutlineManager, outline_baseCollectionPrunesDuplicateHoverRoot)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths   = { SdfPath("/Root/Cube") };
    inputs.hoverPaths      = { SdfPath("/Root/Cube") }; // same path in both buckets
    inputs.isHoverSelected = true;
    outline.SetInputs(inputs);

    EXPECT_EQ(_GetSortedBaseRoots(*f.framePass), SdfPathVector { SdfPath("/Root/Cube") });
}

/// Test: A hovered path nested under a selected root is pruned from the Base collection roots --
/// the selected ancestor already selects that subtree, so hovering within a selection leaves the
/// roots vector unchanged rather than dirtying the collection.
HVT_TEST(TestOutlineManager, outline_baseCollectionPrunesNestedHoverRoot)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    inputs.hoverPaths    = { SdfPath("/Root/Cube/Child") }; // nested under the selected root
    outline.SetInputs(inputs);

    EXPECT_EQ(_GetSortedBaseRoots(*f.framePass), SdfPathVector { SdfPath("/Root/Cube") });
}

/// Test: Pruning is path-prefix aware, not string-prefix aware. "/Root/CubeExtra" shares a
/// string prefix with "/Root/Cube" but is a sibling, not a descendant, so both roots survive.
HVT_TEST(TestOutlineManager, outline_baseCollectionKeepsStringPrefixSiblingRoot)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    inputs.hoverPaths    = { SdfPath("/Root/CubeExtra") };
    outline.SetInputs(inputs);

    SdfPathVector expected = { SdfPath("/Root/Cube"), SdfPath("/Root/CubeExtra") };
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(_GetSortedBaseRoots(*f.framePass), expected);
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

/// Test: Verifies the manager's core lifetime contract: destroying the OutlineManager while its
/// tasks are still installed must leave the next commit a safe no-op (the callbacks lock() a
/// weak_ptr that now fails) rather than a dereference of freed state. The tasks outlive the manager
/// because the frame pass's TaskManager owns them, as it does every other task -- and they keep the
/// parameters last committed to them, which is the documented reason a host has to clear the outline
/// itself rather than rely on destruction.
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

    EXPECT_NO_THROW(taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit));

    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Base")));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Overlay")));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Default")));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineMaskTask::GetToken()));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineOverlayTask::GetToken()));

    // Destruction clears nothing: the mask stays enabled and the base pass still selects the
    // subtree that was pushed, which is what a host would still be rendering.
    EXPECT_TRUE(_GetMaskParams(taskManager).enabled);
    SdfPathVector const baseRoots =
        _GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).collection.GetRootPaths();
    EXPECT_EQ(baseRoots, SdfPathVector { SdfPath("/Root/Cube") });
}

/// Test: Verifies the documented route to reusing a still-live frame pass. Install() refuses a pass
/// whose task names are taken, so a replacement manager requires removing the previous one's tasks
/// through the TaskManager that owns them.
HVT_TEST(TestOutlineManager, outline_reinstallAfterRemovingTasksOnSameFramePass)
{
    OutlineSceneFixture f;
    auto& taskManager = *f.framePass->GetTaskManager();

    {
        hvt::Outline::OutlineManager first;
        first.Install(*f.framePass);
    }

    for (TfToken const& taskName :
        { hvt::Outline::OutlinePrimIdsTask::GetToken("Base"),
            hvt::Outline::OutlinePrimIdsTask::GetToken("Overlay"),
            hvt::Outline::OutlinePrimIdsTask::GetToken("Default"),
            hvt::Outline::OutlineMaskTask::GetToken(),
            hvt::Outline::OutlineOverlayTask::GetToken() })
    {
        taskManager.RemoveTask(taskName);
    }

    hvt::Outline::OutlineManager second;
    second.Install(*f.framePass);

    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Base")));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Overlay")));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlinePrimIdsTask::GetToken("Default")));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineMaskTask::GetToken()));
    EXPECT_TRUE(taskManager.HasTask(hvt::Outline::OutlineOverlayTask::GetToken()));

    // The replacement drives the tasks it installed.
    hvt::Outline::OutlineInputs inputs;
    inputs.overlayPaths = { SdfPath("/Root/Cube") };
    second.SetInputs(inputs);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_EQ(_GetMaskParams(taskManager).overlayPaths, inputs.overlayPaths);
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
///
/// Both pushes below take their dedup early return on a freshly installed manager -- the default
/// style and default inputs compare equal to the manager's own -- so this case asserts the initial
/// configuration, not a transition into it. Reaching the same state from an enabled one is
/// outline_clearedInputsAndStyleDisableEveryTask below.
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

/// Test: A target with no instance levels is the same as its path in selectedPaths: the committed
/// Base, Overlay, Default, mask and overlay-composite params are equal, whole structs compared.
/// This pins the opt-in contract of selectedTargets: a host that moves whole-prim selections to
/// targets, or leaves targets empty, gets exactly the outline it got from selectedPaths.
HVT_TEST(TestOutlineManager, outline_levelLessTargetMatchesSelectedPath)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    auto& taskManager = *f.framePass->GetTaskManager();

    struct Committed
    {
        hvt::Outline::OutlinePrimIdsTaskParams base;
        hvt::Outline::OutlinePrimIdsTaskParams overlay;
        hvt::Outline::OutlinePrimIdsTaskParams def;
        hvt::Outline::OutlineMaskTaskParams mask;
        hvt::Outline::OutlineOverlayTaskParams composite;
    };
    auto commit = [&]()
    {
        taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
        return Committed { _GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask),
            _GetPrimIdsParams(taskManager, _tokens->outlineOverlayPrimIdsTask),
            _GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask),
            _GetMaskParams(taskManager), _GetOverlayParams(taskManager) };
    };

    hvt::Outline::OutlineInputs byPath;
    byPath.selectedPaths = { SdfPath("/Root/Cube") };
    byPath.leadPath      = SdfPath("/Root/Cube");
    outline.SetInputs(byPath);
    Committed const fromPath = commit();
    ASSERT_TRUE(fromPath.base.enabled);

    hvt::Outline::OutlineInputs byTarget;
    byTarget.selectedTargets = { { SdfPath("/Root/Cube"), {} } };
    byTarget.leadPath        = SdfPath("/Root/Cube");
    outline.SetInputs(byTarget);
    Committed const fromTarget = commit();

    EXPECT_TRUE(fromTarget.base == fromPath.base);
    EXPECT_TRUE(fromTarget.overlay == fromPath.overlay);
    EXPECT_TRUE(fromTarget.def == fromPath.def);
    EXPECT_TRUE(fromTarget.mask == fromPath.mask);
    EXPECT_TRUE(fromTarget.composite == fromPath.composite);
}

/// Test: selectedTargets alone enable the highlight tasks, as selectedPaths does, and every
/// target adds its whole path to the Base collection roots, instance levels or not: the pass draws
/// the whole subtree, and instance isolation happens in the shader. The roots are pruned across
/// selectedPaths and selectedTargets like any other overlap.
HVT_TEST(TestOutlineManager, outline_selectedTargetsEnableAndJoinBaseRoots)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = false;
    outline.SetStyle(style);

    hvt::Outline::OutlineTarget const instance3 { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 3 } } } };

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedTargets = { instance3 };
    outline.SetInputs(inputs);

    auto& taskManager = *f.framePass->GetTaskManager();
    EXPECT_EQ(_GetSortedBaseRoots(*f.framePass), SdfPathVector { SdfPath("/Root/PI") });
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).enabled);
    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineOverlayPrimIdsTask).enabled);
    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask).enabled);
    EXPECT_TRUE(_GetMaskParams(taskManager).enabled);
    EXPECT_TRUE(_GetOverlayParams(taskManager).enabled);

    // A target nested under a selected path, and one duplicating it, are pruned; a sibling stays.
    inputs.selectedPaths   = { SdfPath("/Root/Cube") };
    inputs.selectedTargets = { instance3, { SdfPath("/Root/Cube/Child"), {} },
        { SdfPath("/Root/Cube"), {} } };
    outline.SetInputs(inputs);

    SdfPathVector expected = { SdfPath("/Root/Cube"), SdfPath("/Root/PI") };
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(_GetSortedBaseRoots(*f.framePass), expected);
}

/// Test: The Base pass receives targets only once some target is restricted to instances, so hosts
/// selecting whole prims keep the plain primId shader. Each bucket then lists its whole-prim paths
/// as level-less targets around its targets: selectedPaths with selectedTargets, leadPath with
/// leadTargets, hoverPaths with hoverTargets, so that a restricted rprim also selected, lead or
/// hovered whole is classified as such. Only the level-less entries that can cover a restricted
/// rprim are listed: those at, above or under the path of a target with instance levels
/// (/Root/PI here). /Root/PI/Protos/A (under), /Root/PI (same) and /Root (above) are kept, one per
/// bucket; /Root/Cube, /Root/Sphere, /Root/Hovered and /Root/HoveredToo are left out. The Overlay
/// and Default passes never receive targets.
HVT_TEST(TestOutlineManager, outline_baseTargetsOnlyWithInstanceLevels)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = true; // so the Default pass commits too
    outline.SetStyle(style);

    auto& taskManager = *f.framePass->GetTaskManager();
    auto commitBase   = [&]()
    {
        taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
        return _GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask);
    };

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths   = { SdfPath("/Root/Cube"), SdfPath("/Root/PI/Protos/A") };
    inputs.selectedTargets = { { SdfPath("/Root/Sphere"), {} } };
    inputs.hoverPaths      = { SdfPath("/Root/Hovered"), SdfPath("/Root") };
    inputs.overlayPaths    = { SdfPath("/Root/Gizmo") };
    outline.SetInputs(inputs);
    inputs.leadPath        = SdfPath("/Root/Cube");
    inputs.leadTargets     = { { SdfPath("/Root/PI"), {} } };
    inputs.hoverTargets    = { { SdfPath("/Root/HoveredToo"), {} } };
    outline.SetInputs(inputs);
    {
        auto const base = commitBase(); // level-less only: no isolation
        EXPECT_TRUE(base.targets.empty());
        EXPECT_TRUE(base.leadTargets.empty());
        EXPECT_TRUE(base.hoverTargets.empty());
    }

    // Instance levels in any one bucket turn isolation on for the three.
    hvt::Outline::OutlineTarget const instance3 { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 3 } } } };
    for (int bucket = 0; bucket < 3; ++bucket)
    {
        hvt::Outline::OutlineInputs restricted = inputs;
        hvt::Outline::OutlineTargets* const inputBuckets[] = { &restricted.selectedTargets,
            &restricted.leadTargets, &restricted.hoverTargets };
        inputBuckets[bucket]->push_back(instance3);
        outline.SetInputs(restricted);

        auto const base = commitBase();
        hvt::Outline::OutlineTargets expectedSelected = { { SdfPath("/Root/PI/Protos/A"), {} } };
        hvt::Outline::OutlineTargets expectedLead     = { { SdfPath("/Root/PI"), {} } };
        hvt::Outline::OutlineTargets expectedHover    = { { SdfPath("/Root"), {} } };
        hvt::Outline::OutlineTargets* const expectedBuckets[] = { &expectedSelected,
            &expectedLead, &expectedHover };
        expectedBuckets[bucket]->push_back(instance3);
        EXPECT_EQ(base.targets, expectedSelected) << "bucket " << bucket;
        EXPECT_EQ(base.leadTargets, expectedLead) << "bucket " << bucket;
        EXPECT_EQ(base.hoverTargets, expectedHover) << "bucket " << bucket;

        for (TfToken const& other :
            { _tokens->outlineOverlayPrimIdsTask, _tokens->outlineDefaultPrimIdsTask })
        {
            auto const params = _GetPrimIdsParams(taskManager, other);
            EXPECT_TRUE(params.targets.empty());
            EXPECT_TRUE(params.leadTargets.empty());
            EXPECT_TRUE(params.hoverTargets.empty());
        }
    }
}

/// Test: The Base pass receives the targets of each bucket merged, as a host that selects instances
/// one by one sends one target per instance. In the selected bucket:
///   - three targets on /Root/PI restricting /Root/PI become one, with the sorted union of their
///     indices [0, 1, 3], at the position of the first;
///   - the duplicate level-less /Root/PI is dropped;
///   - a target on /Root/PI restricting another instancer, and one on another path, stay apart;
///   - a target with two levels is passed through, even when repeated: a union of intersections
///     is not the intersection of the unions.
/// The lead bucket is merged on its own: its target on /Root/PI does not join the selected one.
HVT_TEST(TestOutlineManager, outline_baseTargetsMergeSingleLevelTargets)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    SdfPath const pi("/Root/PI");
    SdfPath const outer("/Root/Outer");
    SdfPath const inner("/Root/Outer/Protos/Inner");
    auto singleLevel = [](SdfPath const& path, SdfPath const& instancer, VtIntArray const& indices)
    { return hvt::Outline::OutlineTarget { path, { { instancer, indices } } }; };
    hvt::Outline::OutlineTarget const twoLevels { outer,
        { { inner, VtIntArray { 0 } }, { outer, VtIntArray { 1 } } } };

    hvt::Outline::OutlineTarget const otherInstancer =
        singleLevel(pi, SdfPath("/Root/Other"), VtIntArray { 2 });
    hvt::Outline::OutlineTarget const otherPath =
        singleLevel(SdfPath("/Root/PI2"), pi, VtIntArray { 0 });

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedTargets = {
        singleLevel(pi, pi, VtIntArray { 3 }),
        { pi, {} },
        singleLevel(pi, pi, VtIntArray { 1, 3 }),
        otherInstancer,
        { pi, {} },
        otherPath,
        twoLevels,
        twoLevels,
        singleLevel(pi, pi, VtIntArray { 0 }),
    };
    inputs.leadTargets = { singleLevel(pi, pi, VtIntArray { 3 }) };
    outline.SetInputs(inputs);

    auto& taskManager = *f.framePass->GetTaskManager();
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    auto const base = _GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask);

    hvt::Outline::OutlineTargets const expectedSelected = {
        singleLevel(pi, pi, VtIntArray { 0, 1, 3 }),
        { pi, {} },
        otherInstancer,
        otherPath,
        twoLevels,
        twoLevels,
    };
    EXPECT_EQ(base.targets, expectedSelected);
    EXPECT_EQ(base.leadTargets, inputs.leadTargets);
    EXPECT_TRUE(base.hoverTargets.empty());
}

/// Test: Lead and hover targets with no instance levels are plain paths of their bucket: the mask
/// gets them in leadPaths and hoverPaths. Targets with instance levels are colored from the Base
/// pass bucket bits instead, so they stay out of the mask lists. Hover targets join the Base roots,
/// like hoverPaths; lead targets do not, like leadPath.
HVT_TEST(TestOutlineManager, outline_leadAndHoverTargetsReachMaskAndBaseRoots)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    hvt::Outline::OutlineTarget const leadInstance { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 1 } } } };
    hvt::Outline::OutlineTarget const hoverInstance { SdfPath("/Root/PI2"),
        { { SdfPath("/Root/PI2"), VtIntArray { 2 } } } };

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    inputs.leadPath      = SdfPath("/Root/Cube");
    inputs.leadTargets   = { { SdfPath("/Root/LeadWhole"), {} }, leadInstance };
    inputs.hoverPaths    = { SdfPath("/Root/Hovered") };
    inputs.hoverTargets  = { { SdfPath("/Root/HoveredWhole"), {} }, hoverInstance };
    outline.SetInputs(inputs);

    auto& taskManager = *f.framePass->GetTaskManager();
    SdfPathVector expectedRoots = { SdfPath("/Root/Cube"), SdfPath("/Root/Hovered"),
        SdfPath("/Root/HoveredWhole"), SdfPath("/Root/PI2") };
    std::sort(expectedRoots.begin(), expectedRoots.end());
    EXPECT_EQ(_GetSortedBaseRoots(*f.framePass), expectedRoots);

    hvt::Outline::OutlineMaskTaskParams const mask = _GetMaskParams(taskManager);
    EXPECT_EQ(mask.leadPath, SdfPath("/Root/Cube"));
    EXPECT_EQ(mask.leadPaths, SdfPathVector { SdfPath("/Root/LeadWhole") });
    EXPECT_EQ(mask.hoverPaths,
        (SdfPathVector { SdfPath("/Root/Hovered"), SdfPath("/Root/HoveredWhole") }));

    // Hover targets alone enable the highlight tasks, as hoverPaths does.
    hvt::Outline::OutlineInputs hoverOnly;
    hoverOnly.hoverTargets = { hoverInstance };
    outline.SetInputs(hoverOnly);
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).enabled);
    EXPECT_TRUE(_GetMaskParams(taskManager).enabled);
    EXPECT_TRUE(_GetOverlayParams(taskManager).enabled);
}

/// Test: The documented teardown route -- push cleared inputs AND a style with
/// enableDefaultOutlines disabled, then let one commit run -- takes every task from enabled to
/// disabled. This is the only case that observes an enabled -> disabled transition: the other
/// enablement cases commit once from the default state, so none of them would notice if a commit
/// stopped recomputing enablement downward (gating it on inputsGeneration, say, as the per-task
/// collection cache legitimately does).
///
/// The middle phase is the point of the test as much as the last one: it pins the reason the style
/// push is part of the recipe rather than redundant. Clearing the inputs alone leaves the Base and
/// Default prim-IDs passes, the mask and the overlay composite enabled, because every one of those
/// enablement expressions ORs in enableDefaultOutlines, which is still set.
HVT_TEST(TestOutlineManager, outline_clearedInputsAndStyleDisableEveryTask)
{
    OutlineSceneFixture f;
    hvt::Outline::OutlineManager outline;
    outline.Install(*f.framePass);

    auto& taskManager = *f.framePass->GetTaskManager();

    // Phase 1 -- every task enabled: a selection drives Base and the mask, overlayPaths drives the
    // Overlay pass, and enableDefaultOutlines drives the Default pass.
    hvt::Outline::OutlineStyle style;
    style.enableDefaultOutlines = true;
    outline.SetStyle(style);

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Cube") };
    inputs.overlayPaths  = { SdfPath("/Root/Sphere") };
    outline.SetInputs(inputs);

    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).enabled);
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineOverlayPrimIdsTask).enabled);
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask).enabled);
    EXPECT_TRUE(_GetMaskParams(taskManager).enabled);
    EXPECT_TRUE(_GetOverlayParams(taskManager).enabled);

    // Phase 2 -- cleared inputs alone are NOT enough. Only the Overlay prim-IDs pass, whose
    // enablement is overlayPaths and nothing else, goes quiet; the other four stay enabled because
    // enableDefaultOutlines is still set.
    outline.SetInputs(hvt::Outline::OutlineInputs {});
    taskManager.CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

    EXPECT_FALSE(_GetPrimIdsParams(taskManager, _tokens->outlineOverlayPrimIdsTask).enabled);
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineBasePrimIdsTask).enabled);
    EXPECT_TRUE(_GetPrimIdsParams(taskManager, _tokens->outlineDefaultPrimIdsTask).enabled);
    EXPECT_TRUE(_GetMaskParams(taskManager).enabled);
    EXPECT_TRUE(_GetOverlayParams(taskManager).enabled);

    // Phase 3 -- the style half completes the teardown, and the commit is what carries it to the
    // tasks.
    style.enableDefaultOutlines = false;
    outline.SetStyle(style);
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

/// Test: Guards the documented leadPath contract (OutlineInputs::leadPath). leadPath is never
/// added to the Base collection roots -- it only recolors prim IDs already rasterized there --
/// so a leadPath disjoint from those buckets has no visible effect, yet it still passes through
/// to the mask params. (leadIdsCount is resolved by OutlineMaskTask::_Sync(), not by the
/// manager, so it is not asserted after a bare commit.)
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

/// Test: OutlineInstanceLevel and OutlineTarget equality detects a difference in each field,
/// including the order of the instance indices (the arrays are compared as given).
HVT_TEST(TestOutlineManager, outline_targetEquality)
{
    using hvt::Outline::OutlineInstanceLevel;
    using hvt::Outline::OutlineTarget;

    OutlineInstanceLevel const level { SdfPath("/Root/PI"), VtIntArray { 1, 3 } };
    EXPECT_EQ(level, (OutlineInstanceLevel { SdfPath("/Root/PI"), VtIntArray { 1, 3 } }));
    EXPECT_NE(level, (OutlineInstanceLevel { SdfPath("/Root/Other"), VtIntArray { 1, 3 } }));
    EXPECT_NE(level, (OutlineInstanceLevel { SdfPath("/Root/PI"), VtIntArray { 3, 1 } }));
    EXPECT_NE(level, (OutlineInstanceLevel { SdfPath("/Root/PI"), VtIntArray {} }));

    OutlineTarget const target { SdfPath("/Root/PI"), { level } };
    EXPECT_EQ(target, (OutlineTarget { SdfPath("/Root/PI"), { level } }));
    EXPECT_NE(target, (OutlineTarget { SdfPath("/Root/Other"), { level } }));
    EXPECT_NE(target, (OutlineTarget { SdfPath("/Root/PI"), {} }));
    EXPECT_NE(target, (OutlineTarget { SdfPath("/Root/PI"), { level, level } }));
}

/// Test: selectedTargets takes part in the SetInputs() dedup and in the input path count. An
/// unchanged target is a hit; a change in its instance indices alone is a miss. Each target counts
/// as one input path, whatever its instance levels.
HVT_TEST(TestOutlineManager, outline_cacheMissOnSelectedTargets)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineInputs inputs;
    outline.SetInputs(inputs); // empty targets, identical to the default state -> hit

    hvt::Outline::OutlineTarget const instance3 { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 3 } } } };
    inputs.selectedTargets = { instance3, { SdfPath("/Root/Cube"), {} } };
    outline.SetInputs(inputs); // miss -- targets added
    outline.SetInputs(inputs); // hit -- unchanged

    inputs.selectedTargets[0].instanceLevels[0].instanceIndices = VtIntArray { 4 };
    outline.SetInputs(inputs); // miss -- only an instance index changed

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries, 4u);
    ASSERT_EQ(stats.hits,         2u);
    ASSERT_EQ(stats.misses,       2u);
    EXPECT_EQ(stats.maxInputPathCount, 2u); // one per target
}

/// Test: leadTargets and hoverTargets take part in the SetInputs() dedup and in the input path
/// count, like selectedTargets.
HVT_TEST(TestOutlineManager, outline_cacheMissOnLeadAndHoverTargets)
{
    hvt::Outline::OutlineManager outline;

    hvt::Outline::OutlineTarget const instance3 { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 3 } } } };

    hvt::Outline::OutlineInputs inputs;
    inputs.leadTargets = { instance3 };
    outline.SetInputs(inputs); // miss -- lead target added
    outline.SetInputs(inputs); // hit -- unchanged

    inputs.hoverTargets = { instance3, { SdfPath("/Root/Cube"), {} } };
    outline.SetInputs(inputs); // miss -- hover targets added

    inputs.leadTargets[0].instanceLevels[0].instanceIndices = VtIntArray { 4 };
    outline.SetInputs(inputs); // miss -- only a lead instance index changed

    auto stats = outline.GetCacheStats();
    ASSERT_EQ(stats.totalQueries, 4u);
    ASSERT_EQ(stats.hits,         1u);
    ASSERT_EQ(stats.misses,       3u);
    EXPECT_EQ(stats.maxInputPathCount, 3u); // one per target
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
    EXPECT_EQ(maskParams.style.selectedColor, style.selectedColor);
    EXPECT_EQ(maskParams.overlayPaths, inputs.overlayPaths);
    EXPECT_EQ(maskParams.overlayPrimIdsTexture, "outlineOverlayPrimIdsTexture");
    EXPECT_EQ(_GetOverlayParams(taskManager).blurMode, hvt::Outline::BlurMode::Blur5x5);
}

// =====================================================================
// Rendering tests
// (full GPU tests -- disabled on Apple due to non-deterministic primIds)
// Baselines are RGBA and compared on all four channels, so they also cover
// the overlay composite's destination alpha.
// =====================================================================

/// Test: Verifies that Outline with a selected path produces the expected
/// outline output when driven through SetInputs().
HVT_TEST(TestOutlineManager, outline_renderSelectedPath)
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

/// Test: Instance isolation end to end. A point instancer draws three cubes from one prototype
/// rprim, so the three instances share one prim ID. A target restricted to instance 0 must outline
/// the cube at x = -10 only, on the right of the image (the test camera mirrors x); without
/// isolation, all three would be outlined. An end cube rather than the middle one, so that an index
/// counted from the wrong end outlines the other end.
HVT_TEST(TestOutlineManager, outline_renderInstanceTarget)
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

        // Prototypes under the instancer are drawn only through it.
        auto instancer = UsdGeomPointInstancer::Define(usdStage, SdfPath("/Root/PI"));
        auto cube      = UsdGeomCube::Define(usdStage, SdfPath("/Root/PI/Protos/Cube"));
        cube.GetSizeAttr().Set(6.0);
        instancer.CreatePrototypesRel().AddTarget(cube.GetPath());
        instancer.CreateProtoIndicesAttr().Set(VtIntArray { 0, 0, 0 });
        instancer.CreatePositionsAttr().Set(VtVec3fArray {
            GfVec3f(-10.0f, 0.0f, 0.0f), GfVec3f(0.0f, 0.0f, 0.0f), GfVec3f(10.0f, 0.0f, 0.0f) });
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
        passDesc.uid         = SdfPath("/TestOutlineRenderInstanceTarget");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    hvt::Outline::OutlineManager outline;
    outline.Install(*sceneFramePass);

    {
        hvt::Outline::OutlineStyle style;
        style.selectedColor = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
        style.blurMode      = hvt::Outline::BlurMode::Blur3x3;
        outline.SetStyle(style);
    }

    {
        hvt::Outline::OutlineInputs inputs;
        inputs.selectedTargets = { { SdfPath("/Root/PI"),
            { { SdfPath("/Root/PI"), VtIntArray { 0 } } } } };
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

/// Test: Edges between touching kept instances. Of three touching point instances sharing one prim
/// ID, a target restricted to instances 0 and 1 outlines those two one by one, with an edge where
/// they touch, on the right of the image (the test camera mirrors x). The indices are unsorted and
/// repeated, so the encoding must sort them for the shader's binary search.
HVT_TEST(TestOutlineManager, outline_renderTouchingInstanceTargets)
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedTargets = { { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 1, 1, 0 } } } } };

    ASSERT_TRUE(_RenderTouchingInstances(
        inputs, computedImageName, SdfPath("/TestOutlineRenderTouchingInstanceTargets")));
}

/// Test: The lead among instances of one rprim. Of three touching point instances sharing one prim
/// ID, instances 0 and 1 are selected and instance 1 is the lead: instance 0 gets the selected
/// color, instance 1 the lead color, with an edge between them, and instance 2 is not outlined.
/// With a lead per rprim (leadPath), both would get the lead color.
HVT_TEST(TestOutlineManager, outline_renderLeadInstanceTarget)
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedTargets = { { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 0, 1 } } } } };
    inputs.leadTargets     = { { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 1 } } } } };

    ASSERT_TRUE(_RenderTouchingInstances(
        inputs, computedImageName, SdfPath("/TestOutlineRenderLeadInstanceTarget")));
}

/// Test: Hover per instance. Of three touching point instances sharing one prim ID, instance 0 is
/// selected and instances 0 and 2 are hovered: instance 0 gets the selected hover color, instance 2
/// the unselected hover color, and instance 1, neither selected nor hovered, is not outlined.
/// isHoverSelected is left false: the instances are hovered as selected from the selected targets.
HVT_TEST(TestOutlineManager, outline_renderHoverInstanceTarget)
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    hvt::Outline::OutlineInputs inputs;
    inputs.selectedTargets = { { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 0 } } } } };
    inputs.hoverTargets    = { { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 0, 2 } } } } };

    ASSERT_TRUE(_RenderTouchingInstances(
        inputs, computedImageName, SdfPath("/TestOutlineRenderHoverInstanceTarget")));
}

/// Test: Instance isolation turned on and off at runtime, on one frame pass. Steps, on three
/// touching point instances sharing one prim ID:
///   0. /Root/PI selected whole: one outline around the three cubes, isolation off;
///   1. instances 0 and 1 as a target: isolation turns on, so the task adds the instanceId AOV and
///      binds the targets. Same image as outline_renderTouchingInstanceTargets;
///   2. instances 1 and 2: only the indices change, so the targets are encoded again, with no AOV
///      change;
///   3. /Root/PI selected whole again: isolation turns off, so the task unbinds the instanceId AOV
///      (its buffer is kept) and erases its texture from the task context. Same image as step 0;
///   4. instances 0 and 1 again: isolation turns on again and the kept buffer is bound again,
///      with no reallocation. Same image as step 1.
/// Isolation not turning on loses the edge in step 1, a stale encoding makes step 2 repeat step 1,
/// an instanceId texture left in the task context keeps the edge of step 2 in step 3, and a kept
/// buffer that is not bound again loses the edge in step 4.
HVT_TEST(TestOutlineManager, outline_renderInstanceIsolationToggle)
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    std::string const& name = TestHelpers::gTestNames.fixtureName;

    hvt::Outline::OutlineInputs whole;
    whole.selectedPaths = { SdfPath("/Root/PI") };

    hvt::Outline::OutlineInputs instances01;
    instances01.selectedTargets = { { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 0, 1 } } } } };

    hvt::Outline::OutlineInputs instances12;
    instances12.selectedTargets = { { SdfPath("/Root/PI"),
        { { SdfPath("/Root/PI"), VtIntArray { 1, 2 } } } } };

    ASSERT_TRUE(_RenderTouchingInstanceSteps(
        { { whole, name + "_whole", {} },
            { instances01, kTouchingInstanceTargetsBaseline, {} },
            { instances12, name + "_instances12", {} },
            { whole, name + "_whole", {} },
            { instances01, kTouchingInstanceTargetsBaseline, {} } },
        computedImageName, SdfPath("/TestOutlineRenderInstanceIsolationToggle")));
}

/// Test: Edge cases of instance targets, on one frame pass, with no error posted. Steps, on three
/// touching point instances sharing one prim ID:
///   0. instances 0, 1 and 99: the out-of-range index keeps nothing. Same image as
///      outline_renderTouchingInstanceTargets;
///   1. an empty index list: keeps no instance, so nothing is outlined;
///   2. a level on a path that draws no rprim: the target covers no instance of the cubes, so
///      nothing is outlined;
///   3. the target path on the prototype prim instead of the instancer, instances 0 and 1: the
///      rprim drawn under it is restricted the same way. Same image as step 0;
///   4. /Root/PI deactivated while targeted: its rprims leave the render index, so the targets
///      resolve to nothing. Not compared: nothing is left to outline;
///   5. /Root/PI active again, same inputs: the rprims come back with new prim IDs and the targets
///      are resolved again. Same image as step 0.
HVT_TEST(TestOutlineManager, outline_renderInstanceTargetEdgeCases)
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    std::string const& name = TestHelpers::gTestNames.fixtureName;
    SdfPath const pi("/Root/PI");

    // One selected target on path, restricted to the given instances of instancer.
    auto makeInputs = [](SdfPath const& path, SdfPath const& instancer, VtIntArray const& indices)
    {
        hvt::Outline::OutlineInputs inputs;
        inputs.selectedTargets = { { path, { { instancer, indices } } } };
        return inputs;
    };
    hvt::Outline::OutlineInputs const onPrototype =
        makeInputs(SdfPath("/Root/PI/Protos/Cube"), pi, VtIntArray { 0, 1 });

    auto setInstancerActive = [](bool active)
    {
        return [active](UsdStageRefPtr const& usdStage)
        { usdStage->GetPrimAtPath(SdfPath("/Root/PI")).SetActive(active); };
    };

    TfErrorMark mark;
    ASSERT_TRUE(_RenderTouchingInstanceSteps(
        { { makeInputs(pi, pi, VtIntArray { 0, 1, 99 }), kTouchingInstanceTargetsBaseline, {} },
            { makeInputs(pi, pi, VtIntArray {}), name + "_none", {} },
            { makeInputs(pi, SdfPath("/Root/NoInstancer"), VtIntArray { 0 }), name + "_none", {} },
            { onPrototype, kTouchingInstanceTargetsBaseline, {} },
            { onPrototype, {}, setInstancerActive(false) },
            { onPrototype, kTouchingInstanceTargetsBaseline, setInstancerActive(true) } },
        computedImageName, SdfPath("/TestOutlineRenderInstanceTargetEdgeCases")));
    EXPECT_TRUE(mark.IsClean());
    mark.Clear(); // on failure, keep the errors from surfacing again at teardown
}

/// Test: Instance targets split the way a host that picks instances one by one sends them outline
/// the same instances as one target listing them all. Steps, on three touching point instances
/// sharing one prim ID, on one frame pass:
///   0. instances 1, 0 and 1 again, one target each on /Root/PI: OutlineManager merges them into
///      one target. Same image as outline_renderTouchingInstanceTargets;
///   1. instance 0 on /Root/PI and instance 1 on the prototype prim /Root/PI/Protos/Cube: the
///      manager keeps the two paths apart, and the Base pass merges their blocks for the rprim.
///      Same image as step 0;
///   2. instances 0 and 1 selected, and lead instance 1 from two targets, one on each path. Same
///      image as outline_renderLeadInstanceTarget.
/// A merge that loses or adds indices changes which cubes are outlined, or the edge between them.
HVT_TEST(TestOutlineManager, outline_renderSplitInstanceTargets)
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    SdfPath const pi("/Root/PI");
    SdfPath const prototype("/Root/PI/Protos/Cube");
    auto instance = [&pi](SdfPath const& path, int index)
    { return hvt::Outline::OutlineTarget { path, { { pi, VtIntArray { index } } } }; };

    hvt::Outline::OutlineInputs onePath;
    onePath.selectedTargets = { instance(pi, 1), instance(pi, 0), instance(pi, 1) };

    hvt::Outline::OutlineInputs twoPaths;
    twoPaths.selectedTargets = { instance(pi, 0), instance(prototype, 1) };

    hvt::Outline::OutlineInputs splitLead;
    splitLead.selectedTargets = { instance(pi, 0), instance(pi, 1) };
    splitLead.leadTargets     = { instance(prototype, 1), instance(pi, 1) };

    ASSERT_TRUE(_RenderTouchingInstanceSteps(
        { { onePath, kTouchingInstanceTargetsBaseline, {} },
            { twoPaths, kTouchingInstanceTargetsBaseline, {} },
            { splitLead, "outline_renderLeadInstanceTarget", {} } },
        computedImageName, SdfPath("/TestOutlineRenderSplitInstanceTargets")));
}

/// Test: Nested instancers.
///
/// Scene: the inner point instancer has three instances of a cube (inner 0, 1 and 2), side by side.
/// The outer point instancer has two instances of the inner instancer (outer 0 and 1), one above
/// the other, so six cubes are drawn, in two rows of three:
///
///     outer 0 (top row):     inner 0   inner 1   inner 2
///     outer 1 (bottom row):  inner 0   inner 1   inner 2
///
/// All six cubes are drawn by one rprim, so they share one prim ID. Each cube is told apart by its
/// instance index at two levels: level 0 is the cube's own instancer (inner), level 1 the outer
/// one.
///
/// Two selected targets:
///   - inner 0 and 2 within outer 1. The levels are listed inner first, which is not the order of
///     the instancer chain, to check that the order does not matter;
///   - outer 0, with a single level on the outer instancer (level 1 of the chain).
/// Expected: all three cubes of the top row, and the two end cubes of the bottom row. Combining the
/// levels of a target as a union would also outline the middle cube of the bottom row; a wrong
/// level lookup would outline other cubes, or none.
HVT_TEST(TestOutlineManager, outline_renderNestedInstanceTarget)
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    SdfPath const outerPath("/Root/Outer");
    {
        auto& usdStage = stage.stage();
        if (UsdPrim mesh0 = usdStage->GetPrimAtPath(SdfPath("/mesh_0")))
        {
            mesh0.SetActive(false);
        }

        // Outer instances one above the other (instance 0 on top), inner cubes side by side, none
        // touching. Shifted in x to center the grid in the image.
        SdfPath const innerUsdPath = outerPath.AppendPath(SdfPath("Protos/Inner"));
        SdfPath const cubePath     = innerUsdPath.AppendPath(SdfPath("Protos/Cube"));
        auto outer                 = UsdGeomPointInstancer::Define(usdStage, outerPath);
        auto inner                 = UsdGeomPointInstancer::Define(usdStage, innerUsdPath);
        auto cube                  = UsdGeomCube::Define(usdStage, cubePath);
        cube.GetSizeAttr().Set(6.0);

        // Create the three instances of the cube.
        inner.CreatePrototypesRel().AddTarget(cube.GetPath());
        inner.CreateProtoIndicesAttr().Set(VtIntArray { 0, 0, 0 });
        inner.CreatePositionsAttr().Set(VtVec3fArray {
            GfVec3f(-9.0f, 0.0f, 0.0f), GfVec3f(0.0f, 0.0f, 0.0f), GfVec3f(9.0f, 0.0f, 0.0f) });

        // Create the two instances of the inner instancer.
        outer.CreatePrototypesRel().AddTarget(inner.GetPath());
        outer.CreateProtoIndicesAttr().Set(VtIntArray { 0, 0 });
        outer.CreatePositionsAttr().Set(
            VtVec3fArray { GfVec3f(6.0f, 5.0f, 0.0f), GfVec3f(6.0f, -5.0f, 0.0f) });
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
        passDesc.uid         = SdfPath("/TestOutlineRenderNestedInstanceTarget");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // OutlineInstanceLevel::instancer is a render index path. Prototype propagation re-roots the
    // inner instancer, so its render index path is not its USD path: find it from the instancedBy
    // chain of the cube rprim (inner instancer, then the outer one).
    SdfPath innerPath;
    {
        HdRenderIndex* renderIndex            = pRenderIndexProxy->RenderIndex();
        HdSceneIndexBaseRefPtr const terminal = renderIndex->GetTerminalSceneIndex();
        ASSERT_TRUE(terminal);

        SdfPathVector innerPaths;
        for (SdfPath const& rprimPath : renderIndex->GetRprimIds())
        {
            SdfPath const instancer = _GetFirstInstancedBy(terminal, rprimPath);
            if (!instancer.IsEmpty() && _GetFirstInstancedBy(terminal, instancer) == outerPath
                && std::find(innerPaths.begin(), innerPaths.end(), instancer) == innerPaths.end())
            {
                innerPaths.push_back(instancer);
            }
        }
        ASSERT_EQ(innerPaths.size(), 1u);
        innerPath = innerPaths[0];
    }

    hvt::Outline::OutlineManager outline;
    outline.Install(*sceneFramePass);

    {
        hvt::Outline::OutlineStyle style;
        style.selectedColor = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
        style.blurMode      = hvt::Outline::BlurMode::Blur3x3;
        outline.SetStyle(style);
    }

    {
        hvt::Outline::OutlineInputs inputs;
        inputs.selectedTargets = {
            { outerPath, { { innerPath, VtIntArray { 0, 2 } }, { outerPath, VtIntArray { 1 } } } },
            { outerPath, { { outerPath, VtIntArray { 0 } } } }
        };
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
HVT_TEST(TestOutlineManager, outline_renderStyleChange)
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

/// Test: Verifies that each VisualizationMode selects the matching mask shader when applied via
/// SetStyle(). outline_styleVisualizationModePropagatesAllModes already covers the mode reaching
/// the mask task's params; this covers the remaining leg -- the params value selecting a compute
/// program -- which is only observable in the rendered image. Each mode is rendered independently
/// against its own baseline so a regression in one mode stays distinguishable from another.
HVT_TEST(TestOutlineManager, outline_renderVisualizationModes)
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
        // Two selected objects, so VISUALIZE_PRIM_IDS shows two distinct hashed colors and
        // VISUALIZE_DEPTH two different depth ranges -- a single object cannot show that the
        // debug views are per-prim. Both sit under /Root/Selected, so one SetInputs() covers
        // them. The box is the same size and position as outline_renderStyleChange's, so it
        // frames the same way under the 2x zoom, and the sphere sits beside it with a gap, so
        // the two outlines stay separable.
        //
        // The sphere is also pulled toward the camera (which looks down +z from -z, see
        // TestView::updateCameraAndLights), so the two prims sit at clearly different depths:
        // VISUALIZE_DEPTH shows two grays instead of one, and the mask's nearest-prim
        // comparison has a real winner where the two outlines meet.
        auto box = UsdGeomCube::Define(usdStage, SdfPath("/Root/Selected/Box"));
        box.GetSizeAttr().Set(9.0);
        UsdGeomXformCommonAPI(box).SetTranslate(GfVec3d(0.0, 0.0, 0.0));

        auto sphere = UsdGeomSphere::Define(usdStage, SdfPath("/Root/Selected/Sphere"));
        sphere.GetRadiusAttr().Set(4.0);
        UsdGeomXformCommonAPI(sphere).SetTranslate(GfVec3d(10.0, 0.0, -6.0));
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
        passDesc.uid         = SdfPath("/TestOutlineRenderVisualizationModes");
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

    // For each visualization mode: apply the style, render 3 frames to let Storm settle, then
    // capture and compare against a per-mode baseline image. VISUALIZE_MASK_3x3 / _5x5 differ
    // only in outline thickness; VISUALIZE_PRIM_IDS and VISUALIZE_DEPTH replace the outline with
    // a debug view of the sampled buffers.
    static const struct
    {
        hvt::Outline::VisualizationMode mode;
        const char*                     suffix;
    } kModes[] = {
        { hvt::Outline::VisualizationMode::VISUALIZE_MASK_3x3, "_mask3x3" },
        { hvt::Outline::VisualizationMode::VISUALIZE_MASK_5x5, "_mask5x5" },
        { hvt::Outline::VisualizationMode::VISUALIZE_PRIM_IDS, "_primIds" },
        { hvt::Outline::VisualizationMode::VISUALIZE_DEPTH,    "_depth"   },
    };

    for (auto const& m : kModes)
    {
        hvt::Outline::OutlineStyle style;
        style.selectedColor         = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
        style.maskVisualizationMode = m.mode;
        outline.SetStyle(style);

        int frameCount = 3;
        auto render    = [&]()
        {
            auto& params = sceneFramePass->params();

            params.renderBufferSize = GfVec2i(testContext->width(), testContext->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(testContext->width(), testContext->height());

            params.viewInfo.viewMatrix = stage.viewMatrix();

            // A projection that brackets the scene, rather than stage.projectionMatrix(): the
            // harness camera uses near = diameter/100 and far = diameter*10 (TestHelpers.cpp,
            // TestView::updateCameraAndLights), a far/near ratio of 1000 that compresses every
            // surface here to a depth around 0.996. VISUALIZE_DEPTH renders 1.0 - depth, so that
            // reads as black. Bracketing the eye distance is what a viewport's dynamic near/far
            // fitting achieves, and it is also what the mask's nearest-prim depth comparison
            // assumes. Only the z mapping changes -- same 45 degree FOV, same framing.
            //
            // The aspect ratio is 1.0 to match the harness: its own SetPerspective() call divides
            // two ints, so it passes 1 rather than width/height. Matching it keeps this test
            // framed like the other outline baselines.
            GfVec3d const eye  = stage.viewMatrix().GetInverse().ExtractTranslation();
            double const dist  = eye.GetLength();
            GfFrustum frustum;
            frustum.SetPerspective(45.0, 1.0, dist * 0.5, dist * 1.5);

            // Zoom the camera 2x (scale clip-space x/y about the screen centre), as
            // outline_renderStyleChange does, so the two outlines occupy enough pixels to
            // compare the 3x3 and 5x5 mask kernels by eye in the baseline images. The scene
            // above is sized for this zoom.
            GfMatrix4d zoom(1.0);
            zoom[0][0] = 2.0;
            zoom[1][1] = 2.0;
            params.viewInfo.projectionMatrix = frustum.ComputeProjectionMatrix() * zoom;
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

/// Test: Verifies that an rprim inserted into the render index reaches the mask task's resolved
/// primitive IDs with the host pushing no new inputs, guarding the rprim-version gate in
/// OutlineMaskTask::Prepare().
///
/// Uses leadPath because only the lead / hover / overlay buckets are resolved into the mask's integer
/// ID arrays. selectedPaths reaches the shader as the base collection instead, and the prim-IDs pass
/// rasterizes its primIds every frame, so a prim added under a selected root is outlined either way.
/// An inserted rprim missing from the lead ID array keeps selectedColor instead of selectionLeadColor,
/// which is visible.
///
/// Asserts twice: the reference render matches a baseline (so lead colouring is verified to happen
/// at all), then the render that only saw the insertion matches that reference. Comparing against
/// the pre-insertion image instead would prove nothing, since the new prim's shaded geometry appears
/// regardless.
///
/// Does not cover the gate skipping quiet frames (not observable from outside the task), nor the
/// case Prepare() protects against, which needs an HdRenderIndex::SyncAll that skips clean tasks.
HVT_TEST(TestOutlineManager, outline_renderLeadPicksUpInsertedPrim)
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
    UsdImagingStageSceneIndexRefPtr stageSceneIndex;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        // Keep the stage scene index, unlike the other outline tests: a mid-test stage edit reaches
        // the render index only through UpdateUSDSceneIndex() -> ApplyPendingUpdates(), and the
        // single-argument CreateUSDSceneIndex() overload discards the handle that needs.
        UsdImagingSceneIndices const sceneIndices =
            hvt::ViewportEngine::CreateUSDSceneIndices(stage.stage());
        stageSceneIndex = sceneIndices.stageSceneIndex;
        ASSERT_TRUE(stageSceneIndex);
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(
            sceneIndices.finalSceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineLeadPrimAdded");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    hvt::Outline::OutlineManager outline;
    outline.Install(*sceneFramePass);

    // A lead colour clearly distinct from the selected colour: the assertion turns on whether the
    // inserted prim is recoloured as lead or left as plain selected.
    hvt::Outline::OutlineStyle style;
    style.selectedColor      = GfVec4f(0.10f, 0.55f, 1.0f, 1.0f);
    style.selectionLeadColor = GfVec4f(0.10f, 1.0f, 0.30f, 1.0f);
    outline.SetStyle(style);

    // leadPath is the ancestor spanning the selection, so it resolves through GetRprimSubtree() to
    // every rprim underneath -- which is what has to pick up a later insertion.
    hvt::Outline::OutlineInputs inputs;
    inputs.selectedPaths = { SdfPath("/Root/Selected") };
    inputs.leadPath      = SdfPath("/Root/Selected");
    outline.SetInputs(inputs);

    auto renderAndSave = [&](std::string const& suffix)
    {
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
        EXPECT_TRUE(testContext->_backend->saveImage(computedImageName + suffix));
    };

    // Phase 1 -- resolve the lead against a scene holding the box alone.
    renderAndSave("_beforeInsertion");

    // Phase 2 -- the case under test: insert an rprim under the lead path and render with no
    // SetInputs and no SetStyle, so the gate has to notice by itself.
    {
        auto& usdStage = stage.stage();
        auto sphere    = UsdGeomSphere::Define(usdStage, SdfPath("/Root/Selected/Sphere"));
        sphere.GetRadiusAttr().Set(4.0);
        UsdGeomXformCommonAPI(sphere).SetTranslate(GfVec3d(10.0, 0.0, -6.0));
    }
    hvt::ViewportEngine::UpdateUSDSceneIndex(stageSceneIndex, UsdTimeCode::EarliestTime());
    renderAndSave("_afterInsertion");

    // Phase 3 -- the expected image, reached by the params path instead of the rprim-version path.
    // These SetInputs() calls do not simulate the host; pushing the inputs away and back forces a
    // params delta, which resolves from scratch. The cleared state needs its own render to be
    // committed, otherwise pushing the original value straight back compares equal and never
    // dirties the task.
    outline.SetInputs({});
    renderAndSave("_cleared");
    outline.SetInputs(inputs);
    renderAndSave("_expected");

    // Pin the reference against a baseline first: both shapes carry selectionLeadColor. Without
    // this the comparison below would also pass when lead colouring is lost entirely, since that
    // affects both renders equally.
    ASSERT_TRUE(testContext->validateImages(
        computedImageName + "_expected", TestHelpers::gTestNames.fixtureName + "_expected"));

    // If the gate misses the insertion, "_afterInsertion" leaves the sphere in selectedColor while
    // "_expected" recolours it with selectionLeadColor.
    ASSERT_TRUE(testContext->_backend->compareOutputImages(
        computedImageName + "_afterInsertion", computedImageName + "_expected"));
}
