// Copyright 2025 Autodesk, Inc.
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

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

//
// Include or exclude geometry prims on the fly?
//

namespace
{

// The default path for the grid.
const SdfPath gridPath("/gizmos/grid");

} // anonymous namespace.

// The method creates a single frame pass and executes it.
void CreateTest(const std::shared_ptr<TestHelpers::TestContext>& context,
    TestHelpers::TestStage& stage, const HdRprimCollection& collection)
{
    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Defines the main frame pass i.e., the one containing the scene to display.

    {
        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Adds another model to the current stage.
        hvt::ViewportEngine::CreateGrid(stage.stage(), gridPath, GfVec3d(0.0, 0.0, 0.0), true);

        // Creates the scene index containing the model.

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/sceneFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]() {
        // Updates the main frame pass.

        {
            auto& params = sceneFramePass->params();

            params.renderBufferSize = GfVec2i(context->width(), context->height());

            params.viewInfo.viewport   = { { 0, 0 }, { context->width(), context->height() } };
            params.viewInfo.viewMatrix = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            params.enablePresentation = context->presentationEnabled();

            // Selects what prims to render.
            params.collection = collection;

            // Renders.
            sceneFramePass->Render();
        }

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, sceneFramePass.get());
}

// FIXME: It's sometime failed to render on iOS.Refer to OGSMOD-6933.
// Need to investigate if Android has similar issue too
#if defined(__ANDROID__) || (TARGET_OS_IPHONE == 1)
TEST(howTo, DISABLED_useCollectionToExclude)
#else
TEST(howTo, useCollectionToExclude)
#endif
{
    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Executes the test.

    // Only excludes geometry prims from the grid i.e., selects everything else.
    HdRprimCollection collection { hvt::FramePassParams().collection };
    collection.SetExcludePaths({ gridPath });

    // Creates and runs the test.
    CreateTest(context, stage, collection);

    // Validates the rendering result.

    const std::string imageFile = std::string(test_info_->test_suite_name()) + std::string("/") +
        std::string(test_info_->name());

    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile));
}

// FIXME: It's sometime failed to render on iOS.Refer to OGSMOD-6933.
// Need to investigate if Android has similar issue too
#if defined(__ANDROID__) || (TARGET_OS_IPHONE == 1)
TEST(howTo, DISABLED_useCollectionToInclude)
#else
TEST(howTo, useCollectionToInclude)
#endif
{
    // Helper to create the Hgi implementation.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Executes the test.

    // Only includes the geometry prims from the grid i.e., nothing else.
    HdRprimCollection collection { hvt::FramePassParams().collection };
    collection.SetRootPath(gridPath);

    // Creates and runs the test.
    CreateTest(context, stage, collection);

    // Validates the rendering result.

    const std::string imageFile = std::string(test_info_->test_suite_name()) + std::string("/") +
        std::string(test_info_->name());

    ASSERT_TRUE(context->_backend->saveImage(imageFile));

    ASSERT_TRUE(context->_backend->compareImages(imageFile));
}
