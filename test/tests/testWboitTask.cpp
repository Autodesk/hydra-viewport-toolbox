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

#include <RenderingFramework/CollectTraces.h>
#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/wboitRenderTask.h>
#include <hvt/tasks/wboitResolveTask.h>

#include <pxr/pxr.h>

#include <RenderingFramework/UsdHelpers.h>

#include <pxr/imaging/hdx/oitRenderTask.h>
#include <pxr/imaging/hdx/oitResolveTask.h>
#include <pxr/imaging/hdx/renderTask.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ---------------------------------------------------------------------------
// Core tests
// ---------------------------------------------------------------------------

HVT_TEST(TestWboitTask, wboit_taskConstruction)
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
        passDesc.renderIndex                  = pRenderIndexProxy->RenderIndex();
        passDesc.uid                          = SdfPath("/TestWbOit");
        passDesc.taskCreationOptions.useWbOit = true;
        auto framePass                        = hvt::ViewportEngine::CreateFramePass(passDesc);

        // CreateFramePass already calls CreatePresetTasks internally.
        auto& taskManager = framePass->GetTaskManager();
        ASSERT_TRUE(taskManager->HasTask(TfToken("wboitResolveTask")));
        ASSERT_FALSE(taskManager->HasTask(TfToken("oitResolveTask")));
    }
}

HVT_TEST(TestWboitTask, wboit_renderFullOpacity)
{
    // Mimics the HowTo WBOIT workflow but with a scene where all geometry is fully opaque
    // (opacity = 1). This exercises the WBOIT pipeline when no translucent fragments are
    // produced, verifying it handles the edge case gracefully.

    auto testContext = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(
        stage.open((TestHelpers::getAssetsDataFolder() / "usd/fully_opaque_cube.usda").string()));

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
        passDesc.renderIndex                  = pRenderIndexProxy->RenderIndex();
        passDesc.uid                          = SdfPath("/TestWbOitFullOpacity");
        passDesc.taskCreationOptions.useWbOit = true;

        sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
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

HVT_TEST(TestWboitTask, wboit_renderNearZeroOpacity)
{
    // Mimics the HowTo WBOIT workflow but with a scene where the front rectangle has
    // near-zero opacity. This exercises the WBOIT weight function at the lower extreme,
    // verifying the pipeline does not produce artifacts or numerical instability.

    auto testContext = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(
        (TestHelpers::getAssetsDataFolder() / "usd/near_zero_opacity_cube.usda").string()));

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
        passDesc.renderIndex                  = pRenderIndexProxy->RenderIndex();
        passDesc.uid                          = SdfPath("/TestWbOitNearZeroOpacity");
        passDesc.taskCreationOptions.useWbOit = true;

        sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
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

HVT_TEST(TestWboitTask, wboit_renderOverriddenZeroOpacity)
{
    // Loads the fully_opaque_cube.usda scene and overrides the front rectangle's material
    // opacity to 0.0 via the USD API before creating the scene index. This verifies the
    // WBOIT pipeline handles a runtime opacity change to zero on an originally opaque prim.

    auto testContext = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(
        stage.open((TestHelpers::getAssetsDataFolder() / "usd/fully_opaque_cube.usda").string()));

    {
        UsdPrim shaderPrim = stage.stage()->GetPrimAtPath(
            SdfPath("/Root/Materials/FullyOpaqueMaterial/PreviewSurface"));
        ASSERT_TRUE(shaderPrim.IsValid());

        UsdAttribute opacityAttr = shaderPrim.GetAttribute(TfToken("inputs:opacity"));
        ASSERT_TRUE(opacityAttr.IsValid());
        ASSERT_TRUE(opacityAttr.Set(0.0f));
    }

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
        passDesc.renderIndex                  = pRenderIndexProxy->RenderIndex();
        passDesc.uid                          = SdfPath("/TestWbOitOverriddenZeroOpacity");
        passDesc.taskCreationOptions.useWbOit = true;

        sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
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

HVT_TEST(TestWboitTask, wboit_renderLiveOpacityChange)
{
    // Loads the fully_opaque_cube.usda scene (both rectangles at opacity 1) and changes
    // the front rectangle's opacity to 0.0 mid-render when frameCount reaches 8 (after the
    // second frame). This verifies the WBOIT pipeline correctly handles a live scene edit
    // where a material transitions from fully opaque to fully transparent during rendering.

    auto testContext = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(
        stage.open((TestHelpers::getAssetsDataFolder() / "usd/fully_opaque_cube.usda").string()));

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::FramePassPtr sceneFramePass;
    UsdImagingStageSceneIndexRefPtr stageSceneIndex;

    {
        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

        auto sceneIndices = hvt::ViewportEngine::CreateUSDSceneIndices(stage.stage());
        stageSceneIndex   = sceneIndices.stageSceneIndex;
        pRenderIndexProxy->RenderIndex()->InsertSceneIndex(
            sceneIndices.finalSceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex                  = pRenderIndexProxy->RenderIndex();
        passDesc.uid                          = SdfPath("/TestWbOitLiveOpacityChange");
        passDesc.taskCreationOptions.useWbOit = true;

        sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
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

        --frameCount;

        if (frameCount == 8)
        {
            UsdPrim shaderPrim = stage.stage()->GetPrimAtPath(
                SdfPath("/Root/Materials/FullyOpaqueMaterial/PreviewSurface"));
            UsdAttribute opacityAttr = shaderPrim.GetAttribute(TfToken("inputs:opacity"));
            opacityAttr.Set(0.0f);

            hvt::ViewportEngine::UpdateUSDSceneIndex(stageSceneIndex);
        }

        return frameCount > 0;
    };

    testContext->run(render, sceneFramePass.get());

    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName));
}

HVT_TEST(TestWboitTask, wboit_renderVolume)
{
    // Validates that volume rendering with WBOIT enabled completes without errors.
    // When WBOIT is active, the volume render task falls back to a standard HdxRenderTask
    // (instead of HdxOitVolumeRenderTask), so volumes render without OIT transparency.
    // The scene includes a Volume prim with missing field data alongside opaque geometry
    // to verify the pipeline handles this gracefully.

    auto testContext = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(
        (TestHelpers::getAssetsDataFolder() / "usd/volume_with_geometry.usda").string()));

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
        passDesc.renderIndex                  = pRenderIndexProxy->RenderIndex();
        passDesc.uid                          = SdfPath("/TestWbOitVolume");
        passDesc.taskCreationOptions.useWbOit = true;

        sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
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

    // The OpenVDBAsset has no filePath, which triggers an expected
    // "[PluginLoad] Unknown field data type" diagnostic during rendering.
    {
        ScopedDiagnosticQuiet quietGuard;
        testContext->run(render, sceneFramePass.get());
    }

    ASSERT_TRUE(
        testContext->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName));
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

// ------
// Performance comparison: linked-list OIT vs WBOIT on a complex translucent scene.
// Run with PXR_ENABLE_GLOBAL_TRACE=1 and inspect report.json in Perfetto / chrome://tracing.
// ------

HVT_TEST(TestWboitTask, wboit_performance_test)
{
    auto runBenchmark = [](bool useWbOit, int runCount = 100, bool saveImages = false)
    {
        auto context = TestHelpers::CreateTestContext();

        TestHelpers::TestStage stage(context->_backend);
        ASSERT_TRUE(stage.open(
            (TestHelpers::getAssetsDataFolder() / "usd/wboit_perf_scene.usda").string()));

        hvt::RenderIndexProxyPtr renderIndex;
        hvt::FramePassPtr framePass;

        {
            HD_TRACE_SCOPE("Create");

            hvt::RendererDescriptor renderDesc;
            renderDesc.hgiDriver    = &context->_backend->hgiDriver();
            renderDesc.rendererName = "HdStormRendererPlugin";
            hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

            HdSceneIndexBaseRefPtr sceneIndex =
                hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
            renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

            hvt::FramePassDescriptor passDesc;
            passDesc.renderIndex                  = renderIndex->RenderIndex();
            passDesc.uid                          = SdfPath("/PerfPass");
            passDesc.taskCreationOptions.useWbOit = useWbOit;

            framePass = hvt::ViewportEngine::CreateFramePass(passDesc);
        }

        int remaining = runCount;
        auto render   = [&]()
        {
            HD_TRACE_SCOPE("Render");

            auto& params = framePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = HdxColorCorrectionTokens->disabled;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            params.enablePresentation = context->presentationEnabled();

            framePass->Render();
            context->_backend->waitForGPUIdle();

            return --remaining > 0;
        };

        context->run(render, framePass.get());

        if (saveImages)
        {
            const std::string computedImageName = "wboit_performance_test_" +
                std::string(useWbOit ? "WBOIT" : "LinkedListOIT") + ".png";
            ASSERT_TRUE(context->_backend->saveImage(computedImageName));
        }
    };

    // Runs to initialize everything before running the benchmark.

    runBenchmark(false, 2);
    runBenchmark(true, 2);

    // Runs and collects traces for the benchmark if PXR_ENABLE_GLOBAL_TRACE is set.

    RenderingUtils::CollectTraces collectTraces;

    {
        HD_TRACE_SCOPE("LinkedListOIT");
        runBenchmark(false);
    }
    {
        HD_TRACE_SCOPE("WBOIT");
        runBenchmark(true);
    }
}