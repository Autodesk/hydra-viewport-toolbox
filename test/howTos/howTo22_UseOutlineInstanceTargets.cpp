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
#include <hvt/tasks/outline/outlineTarget.h>

#include <pxr/pxr.h>

#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/pointInstancer.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

//
// How to outline only some instances of an instancer?
//
// A point instancer draws all of its instances of one prototype as a single rprim, with a single
// prim ID. A path in OutlineInputs::selectedPaths therefore outlines every instance. To outline
// only some of them, pass outline targets instead (see outlineTarget.h):
//
//   OutlineTarget { path, instanceLevels }
//     path           -- the subtree to outline, as with selectedPaths.
//     instanceLevels -- per instancer, the instances to keep: { instancer, instanceIndices }.
//
// The instance indices are the instancer-wide ones: the values of the instancer's
// instancerTopology.instanceIndices, which index its per-instance primvars (for a USD point
// instancer, the positions in protoIndices / positions). For nested instancers, list one level per
// instancer; the levels combine as an intersection. A target with no levels is the same as its path
// in selectedPaths.
//
// The three target buckets of OutlineInputs work like their path counterparts:
//
//   selectedTargets -- drawn and outlined in the selected color (like selectedPaths).
//   leadTargets     -- recolor the selected instances they list in the lead color (like leadPath).
//                      They draw nothing by themselves: list the lead instances in selectedTargets
//                      too.
//   hoverTargets    -- drawn and outlined in a hover color (like hoverPaths). A hovered instance
//                      that a selected target keeps gets the selected hover color, any other one
//                      the unselected hover color.
//
// No geometry is copied and the scene is not edited: the Base prim-IDs pass uploads the targets to
// a small GPU buffer and its shader discards the instances that are not kept. Changing the targets
// only re-encodes that buffer. Touching kept instances are outlined one by one. With no target
// that has instance levels, the outline runs exactly as without targets.
//
// See docs/outline.md, "Instance isolation", for the encoding and the limitations.
//

HVT_TEST(howTo, useOutlineInstanceTargets)
{
    if (GetParam() == HgiTokens->Vulkan)
    {
        // Vulkan backend render arbitrary fails.
        GTEST_SKIP() << "Skipping test for the Vulkan backend.";
    }

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Populate the session layer with a point instancer drawing five cubes of size 6 from one
    // prototype, so the five instances share one prim ID:
    //
    //   instance 0 at x = -10.5  -- selected               -> selected color (blue)
    //   instance 1 at x =  -4.5  -- selected, lead         -> lead color (green)
    //   instance 2 at x =   4.5  -- hovered, not selected  -> unselected hover color (magenta)
    //   instance 3 at x =  13.5  -- neither                -> not outlined
    //   instance 4 at x =  22.5  -- selected               -> selected color (blue)
    //
    // Instances 0 and 1 touch: they get one outline each, with an edge between them. The test
    // camera mirrors x: instance 0 is on the right of the image.
    SdfPath const instancerPath("/Root/PI");
    {
        auto& usdStage = stage.stage();

        // Hide the default asset mesh so only the instances are visible.
        if (UsdPrim mesh0 = usdStage->GetPrimAtPath(SdfPath("/mesh_0")))
        {
            mesh0.SetActive(false);
        }

        // Prototypes under the instancer are drawn only through it.
        SdfPath const cubePath = instancerPath.AppendPath(SdfPath("Protos/Cube"));
        auto instancer         = UsdGeomPointInstancer::Define(usdStage, instancerPath);
        auto cube              = UsdGeomCube::Define(usdStage, cubePath);
        cube.GetSizeAttr().Set(6.0);
        instancer.CreatePrototypesRel().AddTarget(cube.GetPath());
        instancer.CreateProtoIndicesAttr().Set(VtIntArray { 0, 0, 0, 0, 0 });
        instancer.CreatePositionsAttr().Set(VtVec3fArray { GfVec3f(-10.5f, 0.0f, 0.0f),
            GfVec3f(-4.5f, 0.0f, 0.0f), GfVec3f(4.5f, 0.0f, 0.0f), GfVec3f(13.5f, 0.0f, 0.0f),
            GfVec3f(22.5f, 0.0f, 0.0f) });
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

    // Step 2: Create and install the OutlineManager, as in howTo21_UseOutlineManager.cpp. Nothing
    // in the installation is specific to instance targets.
    //
    // Declared after sceneFramePass so it is destroyed first: the manager caches a pointer to the
    // pass, which would otherwise dangle.

    hvt::Outline::OutlineManager outline;
    outline.Install(*sceneFramePass);

    // Step 3: Configure the visual style, with one distinct color per bucket.

    {
        hvt::Outline::OutlineStyle style;
        style.selectedColor        = GfVec4f(0.10f, 0.55f, 1.0f, 0.7f);
        style.selectionLeadColor   = GfVec4f(0.2f, 1.0f, 0.2f, 1.0f);
        style.selectedHoverColor   = GfVec4f(1.0f, 0.5f, 0.0f, 1.0f);
        style.unselectedHoverColor = GfVec4f(1.0f, 0.2f, 1.0f, 1.0f);
        style.blurMode             = hvt::Outline::BlurMode::Blur3x3;
        outline.SetStyle(style);
    }

    // Step 4: Push the instance targets.
    //
    // Each target names the subtree (/Root/PI) and, for its only instancer (/Root/PI), the kept
    // instances. Instance 3 is in no target, so it is not outlined. selectedPaths, leadPath and
    // hoverPaths can be set as well, for prims outlined whole: an rprim outlined whole stays whole,
    // whatever the targets.
    //
    // As with paths, SetInputs() is a cheap no-op when the inputs are unchanged, so it is safe to
    // call every frame.

    {
        hvt::Outline::OutlineInputs inputs;
        inputs.selectedTargets = { { instancerPath,
            { { instancerPath, VtIntArray { 0, 1, 4 } } } } };
        inputs.leadTargets     = { { instancerPath, { { instancerPath, VtIntArray { 1 } } } } };
        inputs.hoverTargets    = { { instancerPath, { { instancerPath, VtIntArray { 2 } } } } };
        outline.SetInputs(inputs);
    }

    // Step 5: Render normally.

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
