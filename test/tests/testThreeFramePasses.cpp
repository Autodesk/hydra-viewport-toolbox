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

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include "composeTaskHelpers.h"

#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestFlags.h>
#include <RenderingFramework/CollectTraces.h>

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/blurTask.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/mergingSceneIndex.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

HVT_TEST(TestViewportToolbox, TestThreeFramePasses)
{
    RenderingUtils::CollectTraces collectTraces;

    // This unit test validates the rendering with three frame passes.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);

    auto filepath =
        (TestHelpers::getAssetsDataFolder() / "usd" / "default_scene.usdz").generic_u8string();
    ASSERT_TRUE(stage.open(filepath));

    TestHelpers::FramePassInstance framePass1, framePass2, framePass3;
    HdMergingSceneIndexRefPtr mergingSceneIndex;

    // Create two scene indices and merge them for the first two frame passes.

    {
        auto sceneStage1 = hvt::ViewportEngine::CreateStageFromFile(filepath);
        auto sceneIndex1 = hvt::ViewportEngine::CreateUSDSceneIndex(sceneStage1);

        auto sceneStage2 = hvt::ViewportEngine::CreateStageFromFile(context->_sceneFilepath);
        {
            // Get the root prim from scene stage.
            UsdPrim rootPrim = sceneStage2->GetPrimAtPath(SdfPath("/mesh_0"));

            // Add a zoom (scale transform) to the root prim.
            UsdGeomXformable xformable(rootPrim);
            if (xformable)
            {
                static constexpr double zoomFactor = 20.0;
                GfVec3d scale(zoomFactor, zoomFactor, zoomFactor);

                UsdGeomXformOp scaleOp = xformable.AddScaleOp(UsdGeomXformOp::PrecisionDouble);
                scaleOp.Set(scale);
            }
        }
        auto sceneIndex2 = hvt::ViewportEngine::CreateUSDSceneIndex(sceneStage2);

        // Merge the scene indices.
        mergingSceneIndex = HdMergingSceneIndex::New();
        mergingSceneIndex->AddInputScene(sceneIndex1, SdfPath::AbsoluteRootPath());
        mergingSceneIndex->AddInputScene(sceneIndex2, SdfPath::AbsoluteRootPath());
    }

    // Create the first frame pass instance (main scene with blur effect).

    {
        // Create the render index with the Storm render delegate.
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass1.renderIndex, renderDesc);

        framePass1.sceneIndex = mergingSceneIndex;

        framePass1.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass1.sceneIndex, SdfPath::AbsoluteRootPath());

        // Create the frame pass instance.
        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex      = framePass1.renderIndex->RenderIndex();
        passDesc.uid              = SdfPath("/sceneFramePass1");
        framePass1.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);

        // Add a blur effect to the frame pass.

        static constexpr float blurValue = 8.0f;
        hvt::TaskManagerPtr& taskManager = framePass1.sceneFramePass->GetTaskManager();

        auto fnCommit = [](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const VtValue value        = fnGetValue(HdTokens->params);
            hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
            params.blurAmount          = blurValue;
            fnSetValue(HdTokens->params, VtValue(params));
        };

        const SdfPath& insertPos = taskManager->GetTaskPath(HdxPrimitiveTokens->presentTask);
        taskManager->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(), hvt::BlurTaskParams(),
            fnCommit, insertPos, hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Create the second frame pass instance (additional content in center quarter).

    {
        // Create the render index with the Storm render delegate.
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass2.renderIndex, renderDesc);

        framePass2.sceneIndex = mergingSceneIndex;

        framePass2.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass2.sceneIndex, SdfPath::AbsoluteRootPath());

        // Create the frame pass instance.
        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex      = framePass2.renderIndex->RenderIndex();
        passDesc.uid              = SdfPath("/sceneFramePass2");
        framePass2.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Create the third frame pass instance (additional content displayed at bottom right).

    {
        // Create the render index with the Storm render delegate.
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(framePass3.renderIndex, renderDesc);

        framePass3.sceneIndex = mergingSceneIndex;

        framePass3.renderIndex->RenderIndex()->InsertSceneIndex(
            framePass3.sceneIndex, SdfPath::AbsoluteRootPath());

        // Create the frame pass instance.
        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex      = framePass3.renderIndex->RenderIndex();
        passDesc.uid              = SdfPath("/sceneFramePass3");
        framePass3.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    const auto width  = context->width();
    const auto height = context->height();

    // Render loop.
    int frameCount = 5;

    auto render = [&]()
    {
        // First frame pass: full display with blur effect.

        {
            HD_TRACE_SCOPE("Three Frame Passes: Render Frame Pass 1");

            auto& pass   = framePass1.sceneFramePass;
            auto& params = pass->params();

            params.renderBufferSize = GfVec2i(width, height);
            params.viewInfo.framing = hvt::ViewParams::GetDefaultFraming(width, height);

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace     = HdxColorCorrectionTokens->disabled;
            params.selectionColor = TestHelpers::ColorYellow;

            params.clearBackgroundColor = true;
            params.backgroundColor      = TestHelpers::ColorDarkGrey;

            params.clearBackgroundDepth = true;
            params.backgroundDepth      = 1.0f;

            // Do not present yet, wait for subsequent frame passes.
            params.enablePresentation = false;

            const HdTaskSharedPtrVector renderTasks = pass->GetRenderTasks();

            pass->Render(renderTasks);

            {
                HD_TRACE_SCOPE("Three Frame Passes: waitForGPUIdle Frame Pass 1");
                context->_backend->waitForGPUIdle();
            }
        }

        // Get color & depth from the first frame pass to share the buffers.
        hvt::RenderBufferBindings inputAOVs =
            framePass1.sceneFramePass->GetRenderBufferBindingsForNextPass(
                { HdAovTokens->color, HdAovTokens->depth });

        // Second frame pass: display center quarter only.

        {
            HD_TRACE_SCOPE("Three Frame Passes: Render Frame Pass 2");

            // Define center quarter framing.
            const int quarterWidth  = width / 4;
            const int quarterHeight = height / 4;
            const int offsetX       = width / 3;
            const int offsetY       = height / 3;

            auto& pass   = framePass2.sceneFramePass;
            auto& params = pass->params();

            params.renderBufferSize = GfVec2i(width, height);
            params.viewInfo.framing = {
                { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
                { { offsetX, offsetY }, { offsetX + quarterWidth, offsetY + quarterHeight } }, 1.0f
            };

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace = HdxColorCorrectionTokens->disabled;

            // Do not clear the background as it contains the first frame pass result.
            params.clearBackgroundColor = false;
            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor       = TestHelpers::ColorYellow;

            params.clearBackgroundDepth = false;
            params.backgroundDepth      = 1.0f;

            // Do not present yet, wait for the third frame pass.
            params.enablePresentation = false;

            // Get render tasks with input AOVs and render.
            const HdTaskSharedPtrVector renderTasks = pass->GetRenderTasks(inputAOVs);

            pass->Render(renderTasks);

            {
                HD_TRACE_SCOPE("Three Frame Passes: waitForGPUIdle Frame Pass 2");
                context->_backend->waitForGPUIdle();
            }
        }

        // Get updated AOVs after second frame pass.
        hvt::RenderBufferBindings inputAOVsForPass3 =
            framePass2.sceneFramePass->GetRenderBufferBindingsForNextPass(
                { HdAovTokens->color, HdAovTokens->depth });

        // Third frame pass: display content at bottom right.

        {
            HD_TRACE_SCOPE("Three Frame Passes: Render Frame Pass 3");

            // Define bottom right framing.
            const int passWidth  = width / 4;
            const int passHeight = height / 4;
            const int offsetX    = width - passWidth - 10;
            const int offsetY    = height - passHeight - 10;

            auto& pass   = framePass3.sceneFramePass;
            auto& params = pass->params();

            params.renderBufferSize = GfVec2i(width, height);
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(offsetX, offsetY, passWidth, passHeight);

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace = HdxColorCorrectionTokens->disabled;

            // Do not clear the background color as it contains the previous frame passes
            // result.
            params.clearBackgroundColor = false;
            params.backgroundColor      = TestHelpers::ColorBlackNoAlpha;
            params.selectionColor       = TestHelpers::ColorYellow;

            params.clearBackgroundDepth = false;
            params.backgroundDepth      = 1.0f;

            // This is the final frame pass, enable presentation.
            params.enablePresentation = context->presentationEnabled();

            // Get render tasks with input AOVs and render.
            const HdTaskSharedPtrVector renderTasks = pass->GetRenderTasks(inputAOVsForPass3);

            pass->Render(renderTasks);

            {
                HD_TRACE_SCOPE("Three Frame Passes: waitForGPUIdle Frame Pass 3");
                context->_backend->waitForGPUIdle();
            }
        }

        return --frameCount > 0;
    };

    context->run(render, framePass3.sceneFramePass.get());

    // Validate the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}
