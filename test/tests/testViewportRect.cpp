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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/engine/viewportRect.h>

#include <pxr/imaging/hdx/colorCorrectionTask.h>

#include <gtest/gtest.h>

TEST(TestViewportRect, SingleFramePassSceneDisplay)
{
    // This test creates a single frame pass with a ViewportRect to display a USD 3D scene.
    // It validates that ViewportRect correctly defines the rendering region.

    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Create the rendering setup
    {
        // Create the render index
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Create the scene index containing the model
        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Create the frame pass instance
        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/testViewportRectFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Test ViewportRect functionality
    {
        // Create a ViewportRect with specific dimensions
        hvt::ViewportRect viewport;
        viewport.position = PXR_NS::GfVec2i(50, 100);  // Offset from top-left
        viewport.size = PXR_NS::GfVec2i(640, 480);     // Standard resolution

        // Test ViewportRect conversion methods
        PXR_NS::GfVec4i vec4 = viewport.ConvertToVec4i();
        EXPECT_EQ(vec4[0], 50);   // x position
        EXPECT_EQ(vec4[1], 100);  // y position
        EXPECT_EQ(vec4[2], 640);  // width
        EXPECT_EQ(vec4[3], 480);  // height

        PXR_NS::GfRect2i rect2i = viewport.ConvertToRect2i();
        EXPECT_EQ(rect2i.GetMin()[0], 50);
        EXPECT_EQ(rect2i.GetMin()[1], 100);
        EXPECT_EQ(rect2i.GetWidth(), 640);
        EXPECT_EQ(rect2i.GetHeight(), 480);

        // Test ViewportRect equality operators
        hvt::ViewportRect viewport2;
        viewport2.position = PXR_NS::GfVec2i(50, 100);
        viewport2.size = PXR_NS::GfVec2i(640, 480);
        EXPECT_TRUE(viewport == viewport2);

        hvt::ViewportRect viewport3;
        viewport3.position = PXR_NS::GfVec2i(0, 0);
        viewport3.size = PXR_NS::GfVec2i(640, 480);
        EXPECT_TRUE(viewport != viewport3);
    }

    // Setup frame pass parameters using ViewportRect
    {
        auto& params = sceneFramePass->params();

        // Use the full context size for rendering
        GfVec2i renderSize(context->width(), context->height());
        params.renderBufferSize = renderSize;

        // Create viewport from our ViewportRect but use full size for simplicity
        hvt::ViewportRect mainViewport;
        mainViewport.position = PXR_NS::GfVec2i(0, 0);
        mainViewport.size = renderSize;

        // Set viewport parameters
        params.viewInfo.viewport = mainViewport;
        params.viewInfo.viewMatrix = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights = stage.defaultLights();
        params.viewInfo.material = stage.defaultMaterial();
        params.viewInfo.ambient = stage.defaultAmbient();

        // Set rendering parameters
        params.colorspace = HdxColorCorrectionTokens->disabled;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor = TestHelpers::ColorYellow;
        params.enablePresentation = context->presentationEnabled();
    }

    // Render the scene
    auto render = [&]()
    {
        sceneFramePass->Render();
        return false; // Single frame render
    };

    // Execute the render
    context->run(render, sceneFramePass.get());

    // Save and validate the rendered image
    std::string imageFile = std::string(test_info_->name()) + "_viewport_rect";
    ASSERT_TRUE(context->_backend->saveImage(imageFile));
    ASSERT_TRUE(context->_backend->compareImages(imageFile));
}

TEST(TestViewportRect, MultipleViewportSizes)
{
    // This test validates ViewportRect with different viewport sizes within a single frame pass.

    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Setup rendering infrastructure
    {
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/testMultipleViewportSizes");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Test different viewport configurations
    std::vector<std::pair<GfVec2i, GfVec2i>> viewportConfigs = {
        {{0, 0}, {320, 240}},      // Quarter size
        {{0, 0}, {640, 480}},      // Standard size
        {{0, 0}, {800, 600}},      // Larger size
        {{100, 50}, {400, 300}}    // Offset position
    };

    for (size_t i = 0; i < viewportConfigs.size(); ++i)
    {
        hvt::ViewportRect testViewport;
        testViewport.position = viewportConfigs[i].first;
        testViewport.size = viewportConfigs[i].second;

        // Verify conversion methods work correctly
        auto vec4 = testViewport.ConvertToVec4i();
        EXPECT_EQ(vec4[0], testViewport.position[0]);
        EXPECT_EQ(vec4[1], testViewport.position[1]);
        EXPECT_EQ(vec4[2], testViewport.size[0]);
        EXPECT_EQ(vec4[3], testViewport.size[1]);

        auto rect2i = testViewport.ConvertToRect2i();
        EXPECT_EQ(rect2i.GetMin(), testViewport.position);
        EXPECT_EQ(rect2i.GetWidth(), testViewport.size[0]);
        EXPECT_EQ(rect2i.GetHeight(), testViewport.size[1]);
    }
}

TEST(TestViewportRect, ViewportRectOperators)
{
    // Test all ViewportRect operators and edge cases

    // Test default construction
    hvt::ViewportRect defaultViewport;
    EXPECT_EQ(defaultViewport.position, PXR_NS::GfVec2i(0, 0));
    EXPECT_EQ(defaultViewport.size, PXR_NS::GfVec2i(0, 0));

    // Test equality with identical viewports
    hvt::ViewportRect viewport1;
    viewport1.position = PXR_NS::GfVec2i(100, 200);
    viewport1.size = PXR_NS::GfVec2i(800, 600);

    hvt::ViewportRect viewport2;
    viewport2.position = PXR_NS::GfVec2i(100, 200);
    viewport2.size = PXR_NS::GfVec2i(800, 600);

    EXPECT_TRUE(viewport1 == viewport2);
    EXPECT_FALSE(viewport1 != viewport2);

    // Test inequality with different positions
    hvt::ViewportRect viewport3;
    viewport3.position = PXR_NS::GfVec2i(150, 200);
    viewport3.size = PXR_NS::GfVec2i(800, 600);

    EXPECT_FALSE(viewport1 == viewport3);
    EXPECT_TRUE(viewport1 != viewport3);

    // Test inequality with different sizes
    hvt::ViewportRect viewport4;
    viewport4.position = PXR_NS::GfVec2i(100, 200);
    viewport4.size = PXR_NS::GfVec2i(1024, 768);

    EXPECT_FALSE(viewport1 == viewport4);
    EXPECT_TRUE(viewport1 != viewport4);

    // Test stream operator
    std::stringstream ss;
    ss << viewport1;
    std::string output = ss.str();
    EXPECT_TRUE(output.find("ViewportRect") != std::string::npos);
    EXPECT_TRUE(output.find("Position") != std::string::npos);
    EXPECT_TRUE(output.find("Size") != std::string::npos);
} 