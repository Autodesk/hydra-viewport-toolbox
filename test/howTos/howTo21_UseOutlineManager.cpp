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

#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/outline/outlineManager.h>

#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

//
// How to add an outline highlight pass using the Outline feature wrapper?
//
// The OutlineManager class is the preferred, high-level way to drive the outline
// highlight pass. It owns the five internal tasks (three OutlinePrimIdsTask
// instances, one OutlineMaskTask, one OutlineOverlayTask), the correct
// inter-task AOV bindings, and the commit logic that reads viewport
// parameters from the frame pass on every render. Callers only supply:
//
//   Install(framePass) -- wire the tasks once into the frame pass.
//   SetStyle(style)    -- push visual parameters (colors, blur mode, etc.)
//                         whenever the theme changes.
//   SetInputs(inputs)  -- push selection / hover / overlay paths on every
//                         frame (or when selection changes). The call is a
//                         no-op when the inputs are identical to the previous
//                         call, so it is safe to call every frame.
//
// Compare with howTo20_UseOutlineTasks.cpp which wires the same outline highlight pass
// manually: five AddTask<>() calls, three commit lambdas, and explicit
// token management. Outline encapsulates all of that.
//
// The three path buckets passed to SetInputs() map directly to the three
// OutlinePrimIdsTask instances:
//
//   selectedPaths + hoverPaths -> "Base"    prim-IDs task (blue by default)
//   overlayPaths               -> "Overlay" prim-IDs task (white overlay)
//   all visible prims (*)      -> "Default" prim-IDs task (faint grey)
//
// (*) The default collection covers the entire scene. Use excludePaths to
//     remove specific roots (e.g. selected paths) so they do not receive the
//     faint default outline in addition to their selection outline.
//
// Note: OutlinePrimIdsTask relies on primId rendering, which is
// non-deterministic on Apple/Metal. The test is therefore disabled there.
//

#if defined(__APPLE__)
HVT_TEST(howTo, DISABLED_useOutlineManager)
#else
HVT_TEST(howTo, useOutlineManager)
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
    //   /Root/Unselected/Sphere   -- sphere on the left  -> Default category (faint outline)
    //   /Root/Selected/Box        -- cube in the center  -> Base category (blue outline)
    //   /Root/Selected/Cylinder   -- cylinder on right   -> Base category + leadPath
    //                                                       (green lead-selection outline)
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

    // Step 2: Create and install the OutlineManager.
    //
    // Install() wires the five internal tasks into the frame pass in the correct
    // execution order (prim-IDs -> mask -> overlay), sets up AOV texture bindings,
    // and attaches commit functions that read viewport parameters (render-buffer
    // size, camera, framing) from the frame pass on every Render() call.
    //
    // This single call replaces the five AddTask<>() calls and three commit
    // lambdas in howTo20_UseOutlineTasks.cpp.

    hvt::Outline::OutlineManager outline;
    outline.Install(*sceneFramePass);

    // Step 3: Configure the visual style.
    //
    // Colors match those used in howTo20 so the rendered outputs are comparable.

    {
        hvt::Outline::OutlineStyle style;
        style.selectedColor      = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
        style.selectionLeadColor = GfVec4f(0.18f, 0.95f, 0.64f, 0.7f);
        style.defaultColor       = GfVec4f(0.2f, 0.2f, 0.2f, 1.0f);
        style.blurMode           = hvt::Outline::BlurMode::Blur3x3;
        outline.SetStyle(style);
    }

    // Step 4: Push the selection state.
    //
    // selectedPaths drives the Base prim-IDs pass (selected + hover combined).
    // leadPath is the "lead" item rendered with selectionLeadColor.
    // excludePaths removes selected roots from the Default (whole-scene) pass so
    // they do not receive the faint default outline in addition to their colored
    // selection outline.
    //
    // SetInputs() is safe to call on every frame -- it is a cheap no-op when the
    // inputs are unchanged (cache hit). Call it whenever selection state changes.

    {
        hvt::Outline::OutlineInputs inputs;
        inputs.selectedPaths = { SdfPath("/Root/Selected/Box"),
                                 SdfPath("/Root/Selected/Cylinder") };
        inputs.leadPath    = SdfPath("/Root/Selected/Cylinder");
        inputs.excludePaths  = { SdfPath("/Root/Selected") };
        outline.SetInputs(inputs);
    }

    // Step 5: Render normally. The Outline commit functions read renderBufferSize
    // and camera parameters from the frame pass on each Render() call, so no
    // manual size synchronization is required.

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

        sceneFramePass->Render();
        context->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    context->run(render, sceneFramePass.get());

    ASSERT_TRUE(context->validateImages(computedImageName, imageFile));
}
