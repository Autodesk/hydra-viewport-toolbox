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
#include <hvt/tasks/outline/outlineMaskTask.h>
#include <hvt/tasks/outline/outlineOverlayTask.h>
#include <hvt/tasks/outline/outlinePrimIdsTask.h>

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/colorCorrectionTask.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/pxr.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/usd/sdf/path.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

struct OutlineTaskFixture
{
    std::shared_ptr<TestHelpers::TestContext> testContext;
    hvt::RenderIndexProxyPtr renderIndexProxy;
    HdRenderIndex* pRenderIndex = nullptr;
    std::unique_ptr<hvt::Engine> engine;
    HdRetainedSceneIndexRefPtr retainedSceneIndex;
    std::unique_ptr<hvt::TaskManager> taskManager;

    OutlineTaskFixture()
    {
        testContext = TestHelpers::CreateTestContext();

        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndexProxy, rendererDesc);

        pRenderIndex = renderIndexProxy->RenderIndex();

        engine             = std::make_unique<hvt::Engine>();
        retainedSceneIndex = HdRetainedSceneIndex::New();
        pRenderIndex->InsertSceneIndex(retainedSceneIndex, SdfPath::AbsoluteRootPath());

        static SdfPath const uid("/TestOutlineTasks");
        taskManager = std::make_unique<hvt::TaskManager>(uid, pRenderIndex, retainedSceneIndex);
    }

    ~OutlineTaskFixture() { taskManager = nullptr; }
};

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((outlinePrimIdsTask, "outlinePrimIdsTask"))
    ((outlineMaskTask, "outlineMaskTask"))(
    (outlineOverlayTask, "outlineOverlayTask")));

void _ConfigureFrameParams(
    hvt::FramePassPtr const& sceneFramePass,
    TestHelpers::TestContext& testContext,
    TestHelpers::TestStage& stage)
{
    auto& params             = sceneFramePass->params();
    params.renderBufferSize  = GfVec2i(testContext.width(), testContext.height());
    params.viewInfo.framing  =
        hvt::ViewParams::GetDefaultFraming(testContext.width(), testContext.height());
    params.viewInfo.viewMatrix       = stage.viewMatrix();
    params.viewInfo.projectionMatrix = stage.projectionMatrix();
    params.viewInfo.lights           = stage.defaultLights();
    params.viewInfo.material         = stage.defaultMaterial();
    params.viewInfo.ambient          = stage.defaultAmbient();
    params.colorspace         = HdxColorCorrectionTokens->disabled;
    params.backgroundColor    = TestHelpers::ColorDarkGrey;
    params.selectionColor     = TestHelpers::ColorYellow;
    params.enablePresentation = testContext.presentationEnabled();
}

void _AddOutlineTasks(
    hvt::FramePassPtr const& sceneFramePass,
    std::string const& bufferPrefix,
    GfVec4f const& selectedColor,
    hvt::BlurMode initialBlurMode,
    GfVec2i& currentBufSize,
    hvt::BlurMode* pCurrentBlurMode)
{
    std::string const kPrimIdsTextureName = "outline" + bufferPrefix + "PrimIdsTexture";
    std::string const kDepthTextureName   = "outline" + bufferPrefix + "DepthTexture";
    auto& taskManager                     = sceneFramePass->GetTaskManager();

    {
        hvt::OutlinePrimIdsTaskParams init;
        init.enabled      = true;
        init.bufferPrefix = bufferPrefix;
        init.collection =
            HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));

        auto fnCommit = [&currentBufSize, &sceneFramePass](
                            hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            VtValue const val               = fnGetValue(HdTokens->params);
            hvt::OutlinePrimIdsTaskParams p = val.Get<hvt::OutlinePrimIdsTaskParams>();
            p.size                          = currentBufSize;
            auto const& rp                  = sceneFramePass->params().renderParams;
            p.camera                        = rp.camera;
            p.framing                       = rp.framing;
            p.overrideWindowPolicy          = rp.overrideWindowPolicy;
            fnSetValue(HdTokens->params, VtValue(p));
        };

        taskManager->AddTask<hvt::OutlinePrimIdsTask>(_tokens->outlinePrimIdsTask, init, fnCommit);
    }

    {
        hvt::OutlineMaskTaskParams init;
        init.enabled               = true;
        init.defaultPrimIdsTexture = kPrimIdsTextureName;
        init.defaultDepthTexture   = kDepthTextureName;
        init.basePrimIdsTexture    = kPrimIdsTextureName;
        init.baseDepthTexture      = kDepthTextureName;
        init.overlayPrimIdsTexture = kPrimIdsTextureName;
        init.overlayDepthTexture   = kDepthTextureName;
        init.style.selectedColor   = selectedColor;

        auto fnCommit = [&currentBufSize](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            VtValue const val            = fnGetValue(HdTokens->params);
            hvt::OutlineMaskTaskParams p = val.Get<hvt::OutlineMaskTaskParams>();
            p.size                       = currentBufSize;
            fnSetValue(HdTokens->params, VtValue(p));
        };

        taskManager->AddTask<hvt::OutlineMaskTask>(_tokens->outlineMaskTask, init, fnCommit);
    }

    {
        hvt::OutlineOverlayTaskParams init;
        init.enabled  = true;
        init.blurMode = initialBlurMode;

        auto fnCommit = [&currentBufSize, pCurrentBlurMode](
                            hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            VtValue const val               = fnGetValue(HdTokens->params);
            hvt::OutlineOverlayTaskParams p = val.Get<hvt::OutlineOverlayTaskParams>();
            p.size                          = currentBufSize;
            if (pCurrentBlurMode != nullptr)
                p.blurMode = *pCurrentBlurMode;
            fnSetValue(HdTokens->params, VtValue(p));
        };

        taskManager->AddTask<hvt::OutlineOverlayTask>(_tokens->outlineOverlayTask, init, fnCommit);
    }
}

} // namespace

/// Test: Verifies OutlineMaskStyleParams equality detects
/// differences in the mask style parameters.
HVT_TEST(TestOutlineTasks, outline_maskStyleParamsEquality)
{
    hvt::OutlineMaskStyleParams a;
    hvt::OutlineMaskStyleParams b;

    ASSERT_EQ(a, b);
    ASSERT_FALSE(a != b);

    b.selectedColor = GfVec4f(1.0f, 0.0f, 0.0f, 1.0f);
    ASSERT_NE(a, b);

    b                    = {};
    b.selectedHoverColor = GfVec4f(0.0f, 1.0f, 0.0f, 1.0f);
    ASSERT_NE(a, b);

    b                    = {};
    b.selectionLeadColor = GfVec4f(0.0f, 0.0f, 1.0f, 1.0f);
    ASSERT_NE(a, b);

    b                         = {};
    b.selectionLeadHoverColor = GfVec4f(1.0f, 1.0f, 0.0f, 1.0f);
    ASSERT_NE(a, b);

    b              = {};
    b.overlayColor = GfVec4f(1.0f, 0.0f, 1.0f, 1.0f);
    ASSERT_NE(a, b);

    b                   = {};
    b.overlayHoverColor = GfVec4f(0.0f, 1.0f, 1.0f, 1.0f);
    ASSERT_NE(a, b);

    b                      = {};
    b.unselectedHoverColor = GfVec4f(0.5f, 0.5f, 0.5f, 1.0f);
    ASSERT_NE(a, b);

    b              = {};
    b.defaultColor = GfVec4f(0.2f, 0.2f, 0.2f, 1.0f);
    ASSERT_NE(a, b);

    b                = {};
    b.activeIdsCount = 3;
    ASSERT_NE(a, b);

    b                  = {};
    b.softnessStrength = 0.5f;
    ASSERT_NE(a, b);

    b                 = {};
    b.softnessFalloff = 0.8f;
    ASSERT_NE(a, b);

    b                 = {};
    b.overlayIdsCount = 2;
    ASSERT_NE(a, b);

    b               = {};
    b.hoverIdsCount = 1;
    ASSERT_NE(a, b);

    b                    = {};
    b.hasDistinctOverlay = 1;
    ASSERT_NE(a, b);

    b                    = {};
    b.hasDistinctDefault = 1;
    ASSERT_NE(a, b);

    b                 = {};
    b.isHoverSelected = 1;
    ASSERT_NE(a, b);
}

/// Test: Verifies OutlineMaskTaskParams equality detects 
/// differences in the mask task parameters
HVT_TEST(TestOutlineTasks, outline_maskTaskParamsEquality)
{
    hvt::OutlineMaskTaskParams a;
    hvt::OutlineMaskTaskParams b;

    ASSERT_EQ(a, b);
    ASSERT_FALSE(a != b);

    b.enabled = true;
    ASSERT_NE(a, b);

    b      = {};
    b.size = GfVec2i(256, 256);
    ASSERT_NE(a, b);

    b               = {};
    b.multisampling = true;
    ASSERT_NE(a, b);

    b                       = {};
    b.maskVisualizationMode = hvt::VisualizationMode::VISUALIZE_DEPTH;
    ASSERT_NE(a, b);

    b                       = {};
    b.defaultPrimIdsTexture = "outlineDefaultPrimIdsTexture";
    ASSERT_NE(a, b);

    b                     = {};
    b.defaultDepthTexture = "outlineDefaultDepthTexture";
    ASSERT_NE(a, b);

    b                    = {};
    b.basePrimIdsTexture = "outlineBasePrimIdsTexture";
    ASSERT_NE(a, b);

    b                  = {};
    b.baseDepthTexture = "outlineBaseDepthTexture";
    ASSERT_NE(a, b);

    b                       = {};
    b.overlayPrimIdsTexture = "outlineOverlayPrimIdsTexture";
    ASSERT_NE(a, b);

    b                     = {};
    b.overlayDepthTexture = "outlineOverlayDepthTexture";
    ASSERT_NE(a, b);

    b            = {};
    b.hoverPaths = { SdfPath("/Root/Cube") };
    ASSERT_NE(a, b);

    b            = {};
    b.activePath = SdfPath("/Root/Cube");
    ASSERT_NE(a, b);

    b              = {};
    b.overlayPaths = { SdfPath("/Root/Cube") };
    ASSERT_NE(a, b);

    b                 = {};
    b.overlayIdValues = { 1, 2, 3 };
    ASSERT_NE(a, b);

    b               = {};
    b.hoverIdValues = { 4 };
    ASSERT_NE(a, b);

    b                = {};
    b.activeIdValues = { 5, 6 };
    ASSERT_NE(a, b);

    b                     = {};
    b.style.selectedColor = GfVec4f(0.5f, 0.5f, 0.5f, 1.0f);
    ASSERT_NE(a, b);
}

/// Test: Verifies OutlineMaskTask can be added to and removed from TaskManager.
HVT_TEST(TestOutlineTasks, outline_maskTaskConstruction)
{
    OutlineTaskFixture f;

    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlineMaskTask));

    SdfPath const maskPath = f.taskManager->AddTask<hvt::OutlineMaskTask>(
        _tokens->outlineMaskTask, hvt::OutlineMaskTaskParams(), nullptr);

    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlineMaskTask));
    ASSERT_TRUE(f.taskManager->HasTask(maskPath));
    ASSERT_FALSE(maskPath.IsEmpty());

    f.taskManager->RemoveTask(maskPath);
    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlineMaskTask));
}

/// Test: Verifies default OutlineMaskTaskParams values are as expected.
HVT_TEST(TestOutlineTasks, outline_maskTaskParamsDefaultValues)
{
    hvt::OutlineMaskTaskParams params;

    ASSERT_FALSE(params.enabled);
    ASSERT_EQ(params.size, GfVec2i(0, 0));
    ASSERT_FALSE(params.multisampling);
    ASSERT_EQ(params.maskVisualizationMode, hvt::VisualizationMode::VISUALIZE_MASK_3x3);
    ASSERT_TRUE(params.defaultPrimIdsTexture.empty());
    ASSERT_TRUE(params.defaultDepthTexture.empty());
    ASSERT_TRUE(params.basePrimIdsTexture.empty());
    ASSERT_TRUE(params.baseDepthTexture.empty());
    ASSERT_TRUE(params.overlayPrimIdsTexture.empty());
    ASSERT_TRUE(params.overlayDepthTexture.empty());
    ASSERT_TRUE(params.hoverPaths.empty());
    ASSERT_TRUE(params.activePath.IsEmpty());
    ASSERT_TRUE(params.overlayPaths.empty());
    ASSERT_TRUE(params.overlayIdValues.empty());
    ASSERT_TRUE(params.hoverIdValues.empty());
    ASSERT_TRUE(params.activeIdValues.empty());
}

/// Test: Verifies OutlinePrimIdsTaskParams equality detects
/// differences in the prim IDs task parameters.
HVT_TEST(TestOutlineTasks, outline_primIdsTaskParamsEquality)
{
    hvt::OutlinePrimIdsTaskParams a;
    hvt::OutlinePrimIdsTaskParams b;

    ASSERT_EQ(a, b);
    ASSERT_FALSE(a != b);

    b.enabled = true;
    ASSERT_NE(a, b);

    b              = {};
    b.bufferPrefix = "Overlay";
    ASSERT_NE(a, b);

    b      = {};
    b.size = GfVec2i(512, 512);
    ASSERT_NE(a, b);

    b        = {};
    b.camera = SdfPath("/Root/Camera");
    ASSERT_NE(a, b);

    b           = {};
    b.cullStyle = HdCullStyleBack;
    ASSERT_NE(a, b);

    b            = {};
    b.collection = HdRprimCollection(TfToken("outline"), HdReprSelector(HdReprTokens->hull));
    ASSERT_NE(a, b);

    b         = {};
    b.framing = CameraUtilFraming(GfRect2i(GfVec2i(0, 0), GfVec2i(64, 64)));
    ASSERT_NE(a, b);

    b                      = {};
    b.overrideWindowPolicy = CameraUtilMatchVertically;
    ASSERT_NE(a, b);

    b = {};
    {
        HdRenderPassAovBinding binding;
        binding.aovName        = TfToken("primId");
        binding.renderBufferId = SdfPath("/test/aovBuffer");
        b.aovBindings          = { binding };
    }
    ASSERT_NE(a, b);
}

/// Test: Verifies default OutlinePrimIdsTaskParams values are as expected.
HVT_TEST(TestOutlineTasks, outline_primIdsTaskParamsDefaultValues)
{
    hvt::OutlinePrimIdsTaskParams params;

    ASSERT_FALSE(params.enabled);
    ASSERT_EQ(params.bufferPrefix, "Base");
    ASSERT_EQ(params.size, GfVec2i(0, 0));
    ASSERT_EQ(params.cullStyle, HdCullStyleNothing);
    ASSERT_TRUE(params.camera.IsEmpty());
    ASSERT_FALSE(params.framing.IsValid());
    ASSERT_TRUE(params.overrideWindowPolicy.has_value());
    ASSERT_EQ(params.overrideWindowPolicy.value(), CameraUtilDontConform);
    ASSERT_TRUE(params.aovBindings.empty());
}

/// Test: Verifies OutlinePrimIdsTask can be added to and removed from TaskManager.
HVT_TEST(TestOutlineTasks, outline_primIdsTaskConstruction)
{
    OutlineTaskFixture f;

    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlinePrimIdsTask));

    SdfPath const primIdsPath = f.taskManager->AddTask<hvt::OutlinePrimIdsTask>(
        _tokens->outlinePrimIdsTask, hvt::OutlinePrimIdsTaskParams(), nullptr);

    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlinePrimIdsTask));
    ASSERT_TRUE(f.taskManager->HasTask(primIdsPath));
    ASSERT_FALSE(primIdsPath.IsEmpty());

    f.taskManager->RemoveTask(primIdsPath);
    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlinePrimIdsTask));
}

/// Test: Verifies OutlineOverlayTaskParams equality detects
/// differences in the overlay task parameters.
HVT_TEST(TestOutlineTasks, outline_overlayTaskParamsEquality)
{
    hvt::OutlineOverlayTaskParams a;
    hvt::OutlineOverlayTaskParams b;

    ASSERT_EQ(a, b);
    ASSERT_FALSE(a != b);

    b.enabled = true;
    ASSERT_NE(a, b);

    b      = {};
    b.size = GfVec2i(1024, 768);
    ASSERT_NE(a, b);

    b             = {};
    b.screenScale = 0.5f;
    ASSERT_NE(a, b);

    b          = {};
    b.blurMode = hvt::BlurMode::Blur5x5;
    ASSERT_NE(a, b);

    b          = {};
    b.blurMode = hvt::BlurMode::None;
    ASSERT_NE(a, b);

    b               = {};
    b.blurIntensity = 2.5f;
    ASSERT_NE(a, b);

    b           = {};
    b.imageSpec = std::make_shared<HioImage::StorageSpec>();
    ASSERT_NE(a, b);
}

/// Test: Verifies OutlineOverlayTaskParams default values are as expected.
HVT_TEST(TestOutlineTasks, outline_overlayTaskParamsDefaultValues)
{
    hvt::OutlineOverlayTaskParams params;

    ASSERT_FALSE(params.enabled);
    ASSERT_EQ(params.size, GfVec2i(0, 0));
    ASSERT_FLOAT_EQ(params.screenScale, 1.0f);
    ASSERT_EQ(params.blurMode, hvt::BlurMode::Blur3x3);
    ASSERT_FLOAT_EQ(params.blurIntensity, 1.0f);
    ASSERT_FALSE(params.imageSpec);
}

/// Test: Verifies OutlineOverlayTask can be added to and removed from TaskManager.
HVT_TEST(TestOutlineTasks, outline_overlayTaskConstruction)
{
    OutlineTaskFixture f;

    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlineOverlayTask));

    SdfPath const overlayPath = f.taskManager->AddTask<hvt::OutlineOverlayTask>(
        _tokens->outlineOverlayTask, hvt::OutlineOverlayTaskParams(), nullptr);

    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlineOverlayTask));
    ASSERT_TRUE(f.taskManager->HasTask(overlayPath));
    ASSERT_FALSE(overlayPath.IsEmpty());

    f.taskManager->RemoveTask(overlayPath);
    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlineOverlayTask));
}

/// Test: Verifies all three outline tasks can be added to and removed from TaskManager.
HVT_TEST(TestOutlineTasks, outline_allThreeTasksConstruction)
{
    OutlineTaskFixture f;

    SdfPath const primIdsPath = f.taskManager->AddTask<hvt::OutlinePrimIdsTask>(
        _tokens->outlinePrimIdsTask, hvt::OutlinePrimIdsTaskParams(), nullptr);
    SdfPath const maskPath = f.taskManager->AddTask<hvt::OutlineMaskTask>(
        _tokens->outlineMaskTask, hvt::OutlineMaskTaskParams(), nullptr);
    SdfPath const overlayPath = f.taskManager->AddTask<hvt::OutlineOverlayTask>(
        _tokens->outlineOverlayTask, hvt::OutlineOverlayTaskParams(), nullptr);

    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlinePrimIdsTask));
    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlineMaskTask));
    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlineOverlayTask));

    f.taskManager->RemoveTask(primIdsPath);
    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlinePrimIdsTask));
    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlineMaskTask));
    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlineOverlayTask));

    f.taskManager->RemoveTask(maskPath);
    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlineMaskTask));
    ASSERT_TRUE(f.taskManager->HasTask(_tokens->outlineOverlayTask));

    f.taskManager->RemoveTask(overlayPath);
    ASSERT_FALSE(f.taskManager->HasTask(_tokens->outlineOverlayTask));
}

/// Test: Verifies SetVisualizationMode transitions through all supported modes without error.
HVT_TEST(TestOutlineTasks, outline_maskTaskSetVisualizationMode)
{
    OutlineTaskFixture f;

    SdfPath const maskPath = f.taskManager->AddTask<hvt::OutlineMaskTask>(
        _tokens->outlineMaskTask, hvt::OutlineMaskTaskParams(), nullptr);

    HdTaskSharedPtr const& taskBase = f.pRenderIndex->GetTask(maskPath);
    ASSERT_NE(taskBase.get(), nullptr);

    hvt::OutlineMaskTask* maskTask = dynamic_cast<hvt::OutlineMaskTask*>(taskBase.get());
    ASSERT_NE(maskTask, nullptr);

    maskTask->SetVisualizationMode(hvt::VisualizationMode::VISUALIZE_PRIM_IDS);
    maskTask->SetVisualizationMode(hvt::VisualizationMode::VISUALIZE_DEPTH);
    maskTask->SetVisualizationMode(hvt::VisualizationMode::VISUALIZE_MASK_3x3);
    maskTask->SetVisualizationMode(hvt::VisualizationMode::VISUALIZE_MASK_5x5);
}

/// Test: Verifies GetToken static methods return the expected token values.
HVT_TEST(TestOutlineTasks, outline_getTokens)
{
    ASSERT_EQ(hvt::OutlineMaskTask::GetToken(),    TfToken("outlineMaskTask"));
    ASSERT_EQ(hvt::OutlineOverlayTask::GetToken(), TfToken("outlineOverlayTask"));

    ASSERT_EQ(hvt::OutlinePrimIdsTask::GetToken("Base"),    TfToken("outlineBasePrimIdsTask"));
    ASSERT_EQ(hvt::OutlinePrimIdsTask::GetToken("Overlay"), TfToken("outlineOverlayPrimIdsTask"));
    ASSERT_EQ(hvt::OutlinePrimIdsTask::GetToken("Default"), TfToken("outlineDefaultPrimIdsTask"));

    ASSERT_EQ(
        hvt::OutlinePrimIdsTask::GetToken("Base"), hvt::OutlinePrimIdsTask::GetToken("Base"));
}

/// Test: Verifies that all three outline tasks registered but disabled must
/// not alter the baseline frame output over multiple frames.
#if defined(__APPLE__)
HVT_TEST(TestOutlineTasks, DISABLED_outline_renderDisabled)
#else
HVT_TEST(TestOutlineTasks, outline_renderDisabled)
#endif
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        // Vulkan backend render arbitrary fails.
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::FramePassPtr sceneFramePass;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineDisabled");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    auto& taskManager = sceneFramePass->GetTaskManager();

    taskManager->AddTask<hvt::OutlinePrimIdsTask>(
        _tokens->outlinePrimIdsTask, hvt::OutlinePrimIdsTaskParams(), nullptr);
    taskManager->AddTask<hvt::OutlineMaskTask>(
        _tokens->outlineMaskTask, hvt::OutlineMaskTaskParams(), nullptr);
    taskManager->AddTask<hvt::OutlineOverlayTask>(
        _tokens->outlineOverlayTask, hvt::OutlineOverlayTaskParams(), nullptr);

    ASSERT_TRUE(taskManager->HasTask(_tokens->outlinePrimIdsTask));
    ASSERT_TRUE(taskManager->HasTask(_tokens->outlineMaskTask));
    ASSERT_TRUE(taskManager->HasTask(_tokens->outlineOverlayTask));

    int frameCount = 10;
    auto render    = [&]()
    {
        _ConfigureFrameParams(sceneFramePass, *testContext, stage);
        sceneFramePass->Render();
        testContext->_backend->waitForGPUIdle();
        return --frameCount > 0;
    };

    testContext->run(render, sceneFramePass.get());

    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName));
}

/// Test: Verifies that enabled primIds → mask → overlay 
/// pipeline produces expected outline output when wired 
/// with matching texture names.
#if defined(__APPLE__)
HVT_TEST(TestOutlineTasks, DISABLED_outline_renderEnabled)
#else
HVT_TEST(TestOutlineTasks, outline_renderEnabled)
#endif
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        // Vulkan backend render arbitrary fails.
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::FramePassPtr sceneFramePass;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineEnabled");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    GfVec2i currentBufSize { 0, 0 };

    _AddOutlineTasks(
        sceneFramePass,
        "Base",
        GfVec4f(1.0f, 1.0f, 0.0f, 1.0f),
        hvt::BlurMode::Blur3x3,
        currentBufSize,
        nullptr);

    int frameCount = 10;
    auto render    = [&]()
    {
        _ConfigureFrameParams(sceneFramePass, *testContext, stage);
        currentBufSize = sceneFramePass->params().renderBufferSize;
        sceneFramePass->Render();
        testContext->_backend->waitForGPUIdle();
        return --frameCount > 0;
    };

    testContext->run(render, sceneFramePass.get());

    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName));
}

/// Test: Verifies that full enabled pipeline with OutlineOverlayTask 
/// cycling None, Blur3x3, and Blur5x5 produces expected per-mode output.
#if defined(__APPLE__)
HVT_TEST(TestOutlineTasks, DISABLED_outline_renderBlurModes)
#else
HVT_TEST(TestOutlineTasks, outline_renderBlurModes)
#endif
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        // Vulkan backend render arbitrary fails.
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::FramePassPtr sceneFramePass;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineBlurModes");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    static hvt::BlurMode const kBlurSequence[] = {
        hvt::BlurMode::None,
        hvt::BlurMode::Blur3x3,
        hvt::BlurMode::Blur5x5,
    };

    GfVec2i currentBufSize { 0, 0 };
    hvt::BlurMode currentBlurMode = hvt::BlurMode::None;

    _AddOutlineTasks(
        sceneFramePass,
        "Base",
        GfVec4f(0.0f, 1.0f, 0.0f, 1.0f),
        hvt::BlurMode::None,
        currentBufSize,
        &currentBlurMode);

    int frameCount = 9;
    auto render    = [&]()
    {
        _ConfigureFrameParams(sceneFramePass, *testContext, stage);
        currentBufSize  = sceneFramePass->params().renderBufferSize;
        currentBlurMode = kBlurSequence[(9 - frameCount) / 3];
        sceneFramePass->Render();
        testContext->_backend->waitForGPUIdle();
        return --frameCount > 0;
    };

    testContext->run(render, sceneFramePass.get());

    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName));
}

/// Test: Verifies that the full outline pipeline operates correctly with a non-"Base" buffer prefix.
#if defined(__APPLE__)
HVT_TEST(TestOutlineTasks, DISABLED_outline_renderNonBasePrefix)
#else
HVT_TEST(TestOutlineTasks, outline_renderNonBasePrefix)
#endif
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        // Vulkan backend render arbitrary fails.
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    auto testContext = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::FramePassPtr sceneFramePass;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = pRenderIndexProxy->RenderIndex();
        passDesc.uid         = SdfPath("/TestOutlineNonBasePrefix");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    GfVec2i currentBufSize { 0, 0 };

    _AddOutlineTasks(
        sceneFramePass,
        "Overlay",
        GfVec4f(0.0f, 0.0f, 1.0f, 1.0f),
        hvt::BlurMode::Blur5x5,
        currentBufSize,
        nullptr);

    int frameCount = 10;
    auto render    = [&]()
    {
        _ConfigureFrameParams(sceneFramePass, *testContext, stage);
        currentBufSize = sceneFramePass->params().renderBufferSize;
        sceneFramePass->Render();
        testContext->_backend->waitForGPUIdle();
        return --frameCount > 0;
    };

    testContext->run(render, sceneFramePass.get());

    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName));
}
