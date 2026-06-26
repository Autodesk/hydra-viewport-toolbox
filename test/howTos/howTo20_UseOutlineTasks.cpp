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

#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/outline/outlineMaskTask.h>
#include <hvt/tasks/outline/outlineOverlayTask.h>
#include <hvt/tasks/outline/outlinePrimIdsTask.h>

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

//
// How to add an outline highlight pass to a frame pass?
//
// The full outline highlight pass uses five tasks:
//
//   OutlinePrimIdsTask (x3) — one per object category, each renders its collection
//                             into a primId and depth AOV buffer pair and exports
//                             them to the task context under:
//                               "outline<Prefix>PrimIdsTexture"
//                               "outline<Prefix>DepthTexture"
//                             Three standard prefixes are used:
//                               "Base"    — selected / highlighted objects
//                               "Overlay" — on-top objects (e.g. manipulators)
//                               "Default" — background scene objects
//
//   OutlineMaskTask         — reads all six texture-context entries (two per prefix)
//                             and runs a GPU compute shader that generates a single
//                             RGBA color mask ("outlineMaskTexture"). Per-category
//                             colors and softness are controlled via style params.
//
//   OutlineOverlayTask      — composites "outlineMaskTexture" onto the scene color
//                             AOV using src-alpha blending. An optional Gaussian blur
//                             (Blur3x3 or Blur5x5) softens edges before compositing.
//
// All tasks are opt-in (enabled = false by default) and must be added manually.
// Every task's size must be kept in sync with the render buffer on each frame via
// its commit function.
//
// Note: OutlinePrimIdsTask relies on primId rendering, which is non-deterministic on Apple/Metal.
// The test is therefore disabled on that platform.
//

namespace
{

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((outlinePrimIdsTaskBase, "outlinePrimIdsTaskBase"))
    ((outlinePrimIdsTaskOverlay, "outlinePrimIdsTaskOverlay"))
    ((outlinePrimIdsTaskDefault, "outlinePrimIdsTaskDefault"))
    ((outlineMaskTask, "outlineMaskTask"))
    ((outlineOverlayTask, "outlineOverlayTask"))
);

} // namespace

#if defined(__APPLE__)
HVT_TEST(howTo, DISABLED_useOutlineTasks)
#else
HVT_TEST(howTo, useOutlineTasks)
#endif
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        // Vulkan backend render arbitrary fails.
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }
    
    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Populate the session layer with three distinctly positioned objects so the
    // outline categories have exclusive geometry to render:
    //
    //   /Root/Unselected/Sphere    — sphere on the left   → Default category (no outline)
    //   /Root/Selected/Box         — cube in the center   → Base category (blue outline)
    //   /Root/Selected/Cylinder    — cylinder on the right → Base category + leadPath
    //                                                        (green lead-selection outline)
    //
    // In a typical application each category is populated from selection state.
    {
        auto& usdStage = stage.stage();

        // Hide the default asset mesh so only the three showcase shapes are visible.
        if (UsdPrim mesh0 = usdStage->GetPrimAtPath(SdfPath("/mesh_0")))
        {
            mesh0.SetActive(false);
        }

        auto box = UsdGeomCube::Define(usdStage, SdfPath("/Root/Selected/Box"));
        box.GetSizeAttr().Set(9.0);
        UsdGeomXformCommonAPI(box).SetTranslate(GfVec3d(-10.0, 0.0, 0.0));

        auto sphere = UsdGeomSphere::Define(usdStage, SdfPath("/Root/Unselected/Sphere"));
        sphere.GetRadiusAttr().Set(4.5);
        UsdGeomXformCommonAPI(sphere).SetTranslate(GfVec3d(-2.0, 0.0, 0.0));

        auto cylinder = UsdGeomCylinder::Define(usdStage, SdfPath("/Root/Selected/Cylinder"));
        cylinder.GetRadiusAttr().Set(3.6);
        cylinder.GetHeightAttr().Set(13.5);
        UsdGeomXformCommonAPI(cylinder).SetTranslate(GfVec3d(14.0, 0.0, 0.0));
    }

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Step 1: Create the renderer and scene index as usual.

    {
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/FramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Step 2: Add the outline highlight pass tasks to the frame pass task manager.
    //
    // The complete outline highlight pass uses three OutlinePrimIdsTask instances
    // (one per object category) feeding a single OutlineMaskTask, which feeds
    // OutlineOverlayTask.
    //
    //   OutlinePrimIdsTask "Base"    ─┐
    //   OutlinePrimIdsTask "Overlay" ─┼─► OutlineMaskTask ─► OutlineOverlayTask
    //   OutlinePrimIdsTask "Default" ─┘
    //
    // Each OutlinePrimIdsTask exports two task-context entries keyed by its prefix:
    //   "outline<Prefix>PrimIdsTexture"
    //   "outline<Prefix>DepthTexture"
    // OutlineMaskTask reads all six entries (two per prefix) and runs a GPU compute
    // shader that produces a single RGBA mask texture.

    auto& taskManager = sceneFramePass->GetTaskManager();

    GfVec2i currentBufSize { 0, 0 };

    auto makePrimIdsCommit = [&currentBufSize, &sceneFramePass](
                                 hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                                 hvt::TaskManager::SetTaskValueFn const& fnSetValue)
    {
        hvt::Outline::OutlinePrimIdsTaskParams p =
            fnGetValue(HdTokens->params).Get<hvt::Outline::OutlinePrimIdsTaskParams>();
        p.size = currentBufSize;

        auto const& rp         = sceneFramePass->params().renderParams;
        p.camera               = rp.camera;
        p.framing              = rp.framing;
        p.overrideWindowPolicy = rp.overrideWindowPolicy;

        fnSetValue(HdTokens->params, VtValue(p));
    };

    // "Base" — selected objects: the cube and cylinder under /Root/Selected.
    {
        hvt::Outline::OutlinePrimIdsTaskParams init;
        init.enabled      = true;
        init.bufferPrefix = "Base";
        init.collection   = HdRprimCollection(HdTokens->geometry,
            HdReprSelector(HdReprTokens->smoothHull), SdfPath("/Root/Selected"));

        taskManager->AddTask<hvt::Outline::OutlinePrimIdsTask>(
            _tokens->outlinePrimIdsTaskBase, init, makePrimIdsCommit);
    }

    // "Overlay" — manipulator/on-top objects (disabled here; no overlay prims in this scene).
    // When no overlay prims exist, reuse Base textures in OutlineMaskTask so
    // hasDistinctOverlay=0. Do not render all geometry into the Overlay pass:
    // the mask shader gives Overlay layer highest priority, so unselected prims would
    // be colored as selected (blue).
    {
        hvt::Outline::OutlinePrimIdsTaskParams init;
        init.enabled      = false;
        init.bufferPrefix = "Overlay";

        taskManager->AddTask<hvt::Outline::OutlinePrimIdsTask>(
            _tokens->outlinePrimIdsTaskOverlay, init, makePrimIdsCommit);
    }

    // "Default" — unselected background objects: only the sphere under /Root/Unselected.
    {
        hvt::Outline::OutlinePrimIdsTaskParams init;
        init.enabled      = true;
        init.bufferPrefix = "Default";
        init.collection   = HdRprimCollection(HdTokens->geometry,
            HdReprSelector(HdReprTokens->smoothHull), SdfPath("/Root/Unselected"));

        taskManager->AddTask<hvt::Outline::OutlinePrimIdsTask>(
            _tokens->outlinePrimIdsTaskDefault, init, makePrimIdsCommit);
    }

    // OutlineMaskTask: reads the six texture-context entries produced above and runs a
    // GPU compute shader that generates a single RGBA color mask ("outlineMaskTexture").
    // Each pair maps to a visual category with its own color.
    {
        hvt::Outline::OutlineMaskTaskParams init;
        init.enabled = true;

        init.basePrimIdsTexture    = "outlineBasePrimIdsTexture";
        init.baseDepthTexture      = "outlineBaseDepthTexture";
        init.defaultPrimIdsTexture = "outlineDefaultPrimIdsTexture";
        init.defaultDepthTexture   = "outlineDefaultDepthTexture";

        init.overlayPrimIdsTexture = init.basePrimIdsTexture;
        init.overlayDepthTexture   = init.baseDepthTexture;

        init.leadPath = SdfPath("/Root/Selected/Cylinder");

        init.style.selectedColor      = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
        init.style.selectionLeadColor = GfVec4f(0.18f, 0.95f, 0.64f, 0.7f);
        init.style.defaultColor       = GfVec4f(0.2f, 0.2f, 0.2f, 1.0f);
        init.style.overlayColor       = GfVec4f(0.0f, 0.0f, 0.0f, 1.0f);

        auto fnCommit = [&currentBufSize](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            hvt::Outline::OutlineMaskTaskParams p =
                fnGetValue(HdTokens->params).Get<hvt::Outline::OutlineMaskTaskParams>();
            p.size = currentBufSize;
            fnSetValue(HdTokens->params, VtValue(p));
        };

        taskManager->AddTask<hvt::Outline::OutlineMaskTask>(_tokens->outlineMaskTask, init, fnCommit);
    }

    {
        hvt::Outline::OutlineOverlayTaskParams init;
        init.enabled  = true;
        init.blurMode = hvt::Outline::BlurMode::Blur3x3;

        auto fnCommit = [&currentBufSize](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            hvt::Outline::OutlineOverlayTaskParams p =
                fnGetValue(HdTokens->params).Get<hvt::Outline::OutlineOverlayTaskParams>();
            p.size = currentBufSize;
            fnSetValue(HdTokens->params, VtValue(p));
        };

        taskManager->AddTask<hvt::Outline::OutlineOverlayTask>(_tokens->outlineOverlayTask, init, fnCommit);
    }

    // Step 3: Render normally. Update the shared currentBufSize each frame so the
    // commit functions above push the correct dimensions on CommitTaskValues.

    int frameCount = 10;

    auto render = [&]()
    {
        auto& params = sceneFramePass->params();

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

        currentBufSize = params.renderBufferSize;

        sceneFramePass->Render();
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    context->run(render, sceneFramePass.get());

    ASSERT_TRUE(context->validateImages(computedImageName, imageFile));
}
