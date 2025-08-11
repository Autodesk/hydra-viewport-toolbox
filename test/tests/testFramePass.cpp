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

// Include the appropriate test context declaration.
#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/taskManager.h>
#include <hvt/tasks/aovInputTask.h>
#include <hvt/tasks/blurTask.h>

#include <pxr/base/gf/rotation.h>
#include <pxr/imaging/hd/selection.h>
#include <pxr/imaging/hdx/aovInputTask.h>
#include <pxr/imaging/hdx/colorCorrectionTask.h>
#include <pxr/imaging/hdx/colorizeSelectionTask.h>
#include <pxr/imaging/hdx/oitResolveTask.h>
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hdx/shadowTask.h>
#include <pxr/imaging/hdx/simpleLightTask.h>

#include <gtest/gtest.h>

TEST(TestViewportToolbox, framePassUID)
{
    // The unit tests the name & unique identifier using the various ways to create a frame pass
    // instance.

    {
        // That's the case when code does not care about the content of the unique identifier.

        static const std::string name { "Main" };
        hvt::FramePass fp(name);
        ASSERT_EQ(fp.GetName(), name);

        static const std::string res { "/framePass_Main_" };
        ASSERT_TRUE(fp.GetPath().GetAsString().compare(0, res.length(), res) == 0);
    }

    {
        static const std::string name { "/Main" };
        hvt::FramePass fp(name);
        ASSERT_EQ(fp.GetName(), name);

        static const std::string res { "/Main_" };
        ASSERT_TRUE(fp.GetPath().GetAsString().compare(0, res.length(), res) == 0);
    }

    {
        static const std::string name { "Main" };
        hvt::FramePass fp(name, SdfPath::EmptyPath());
        ASSERT_EQ(fp.GetName(), name);

        static const std::string res { "/framePass_Main_" };
        ASSERT_TRUE(fp.GetPath().GetAsString().compare(0, res.length(), res) == 0);
    }

    {
        static const std::string name { "/Main" };
        hvt::FramePass fp(name, SdfPath::EmptyPath());
        ASSERT_EQ(fp.GetName(), name);

        static const std::string res { "/Main_" };
        ASSERT_TRUE(fp.GetPath().GetAsString().compare(0, res.length(), res) == 0);
    }

    // The next case is the most appropriate ones when code needs a very specific
    // unique identifier.

    {
        static const std::string name { "Main" };
        static const SdfPath uid { "/UniqueId" };

        hvt::FramePass fp(name, uid);
        ASSERT_EQ(fp.GetName(), name);
        ASSERT_EQ(fp.GetPath(), uid);
    }
}

// FIXME: Android unit test framework does not report the error message, make it impossible to fix
// issues. Refer to OGSMOD-5546.
//
#if defined(__ANDROID__) || defined(__APPLE__) || defined(__linux__)
TEST(TestViewportToolbox, DISABLED_testFramePassColorSpace)
#else
TEST(TestViewportToolbox, testFramePassColorSpace)
#endif
{
    // The goal of the unit test is to validate the colorspace value in FramePassParams is properly
    // assigned to the HdxColorCorrection parameters.

    // Prepares a test context and loads the sample file.

    auto testContext = TestHelpers::CreateTestContext();

    // Creates the render index.

    hvt::RenderIndexProxyPtr pRenderIndexProxy;
    hvt::RendererDescriptor rendererDesc;
    rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
    rendererDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(pRenderIndexProxy, rendererDesc);

    {
        // Creates the frame pass.

        static const SdfPath uid { "/TestFramePass" };
        hvt::FramePassDescriptor desc { pRenderIndexProxy->RenderIndex(), uid, {} };
        auto framePass = std::make_unique<hvt::FramePass>(desc.uid.GetText());
        framePass->Initialize(desc);

        // Creates the default list of tasks.

        const auto [taskIds, renderTaskIds] =
            framePass->CreatePresetTasks(hvt::FramePass::PresetTaskLists::Default);

        // Sets a different colorspace using the FramePass params.

        static const TfToken kColorCorrectionMode("openColorIO");
        hvt::FramePassParams& params = framePass->params();
        params.colorspace            = kColorCorrectionMode;

        // Call the commit functions.

        hvt::TaskManagerPtr& taskManager = framePass->GetTaskManager();

        taskManager->CommitTaskValues(hvt::TaskFlagsBits::kExecutableBit);

        // Make sure the frame pass colorspace was assigned to the HdxColorCorrectionTaskParam.

        const SdfPath kColorCorrectionPath =
            taskManager->GetTaskPath(HdxPrimitiveTokens->colorCorrectionTask);
        auto paramValue = taskManager->GetTaskValue(kColorCorrectionPath, HdTokens->params);
        auto colorCorrectionParams = paramValue.Get<HdxColorCorrectionTaskParams>();
        ASSERT_EQ(colorCorrectionParams.colorCorrectionMode, kColorCorrectionMode);
    }
}

void TestDynamicFramePassParams(
    std::function<GfVec2i(const TestHelpers::TestContext&, int)> getRenderSize,
    std::function<GfMatrix4d(TestHelpers::TestStage&, int)> getViewMatrix,
    std::function<GlfSimpleLightVector(TestHelpers::TestStage&, int)> getLights,
    const std::string& imageFile)
{
    auto context = TestHelpers::CreateTestContext();
    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Defines the main frame pass i.e., the one containing the scene to display.

    {
        // Creates the render index.

        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Creates the scene index.

        auto sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/sceneFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
    }

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        // Get parameters (some might be changing dynamically).
        auto renderSize = getRenderSize(*context, frameCount);
        auto lights     = getLights(stage, frameCount);
        auto viewMatrix = getViewMatrix(stage, frameCount);

        auto& params = sceneFramePass->params();

        params.renderBufferSize = renderSize;

        params.viewInfo.viewport         = { { 0, 0 }, renderSize };
        params.viewInfo.viewMatrix       = viewMatrix;
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = lights;
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();

        params.colorspace      = HdxColorCorrectionTokens->disabled;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context->presentationEnabled();

        sceneFramePass->Render();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, sceneFramePass.get());

    // Saves the rendered image and compares results.

    ASSERT_TRUE(context->_backend->saveImage(imageFile));
    ASSERT_TRUE(context->_backend->compareImages(imageFile));
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestViewportToolbox, DISABLED_testDynamicCameraAndLights)
#else
TEST(TestViewportToolbox, testDynamicCameraAndLights)
#endif
{
    // Use a fixed resolution (the image width/height do not change).
    std::function<GfVec2i(const TestHelpers::TestContext&, int)> getRenderSize =
        [](const TestHelpers::TestContext& testContext, int)
    { return GfVec2i(testContext.width(), testContext.height()); };

    // Change the view matrix while rendering, to make sure it is properly updated.
    std::function<GfMatrix4d(TestHelpers::TestStage&, int)> getViewMatrix =
        [](TestHelpers::TestStage& testStage, int framesToRender)
    {
        // Use the test stage camera for the first frames.
        if (framesToRender > 5)
        {
            return testStage.viewMatrix();
        }

        // Use a different camera position and rotation for the last frames.
        GfMatrix4d viewMatrix = testStage.viewMatrix();
        GfVec3d rotationAxis(0.15, 1.0, 0.0);
        viewMatrix.SetRotateOnly(GfRotation(rotationAxis.GetNormalized(), 200.0));
        return viewMatrix;
    };

    // Change the lights while rendering, to make sure it is properly updated.
    std::function<GlfSimpleLightVector(TestHelpers::TestStage&, int)> getLights =
        [](TestHelpers::TestStage& testStage, int framesToRender)
    {
        // Use default lights for the first frames.
        if (framesToRender > 5)
        {
            return testStage.defaultLights();
        }

        // Modify the default lighting for the last frames.
        GlfSimpleLightVector lights = testStage.defaultLights();
        lights[0].SetDiffuse({ 0.3f, 0.3f, 2.0f, 1.0f });
        lights[0].SetSpecular({ 1.0f, 0.0f, 0.0f, 1.0f });
        lights[0].SetPosition({ -25.0f, -0.7f, -40.0f, 1.0f });
        return lights;
    };

    // Test the Task Controller with dynamic lighting and camera view.
    TestDynamicFramePassParams(getRenderSize, getViewMatrix, getLights, test_info_->name());
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
TEST(TestViewportToolbox, DISABLED_testDynamicResolution)
#else
TEST(TestViewportToolbox, testDynamicResolution)
#endif
{
    // Render at half resolution for the first few frames, then change the render size to the full
    // context width & height, for the last frames. This will test the task renders buffer update,
    // to make sure it is not only valid when initialized the first time, but also when the buffers
    // are dirty and need to be recreated, reassigned and properly referenced across all Tasks.
    std::function<GfVec2i(const TestHelpers::TestContext&, int)> getRenderSize =
        [](const TestHelpers::TestContext& testContext, int framesToRender)
    {
        if (framesToRender > 5)
        {
            return GfVec2i(testContext.width() / 2, testContext.height() / 2);
        }
        return GfVec2i(testContext.width(), testContext.height());
    };

    // Use a fixed camera view matrix (the camera does not move).
    std::function<GfMatrix4d(TestHelpers::TestStage&, int)> getViewMatrix =
        [](TestHelpers::TestStage& testStage, int) { return testStage.viewMatrix(); };

    // Use a fixed set of lights (the default lights do not change).
    std::function<GlfSimpleLightVector(TestHelpers::TestStage&, int)> getLights =
        [](TestHelpers::TestStage& testStage, int) { return testStage.defaultLights(); };

    // Test the Task Controller with a dynamic render resolution.
    TestDynamicFramePassParams(getRenderSize, getViewMatrix, getLights, test_info_->name());
}

#if defined(__ANDROID__)
TEST(TestViewportToolbox, DISABLED_TestFramePassSelectionSettingsProvider)
#else
TEST(TestViewportToolbox, TestFramePassSelectionSettingsProvider)
#endif
{
    // The goal of this unit test is to validate that the FramePass correctly provides
    // access to SelectionSettingsProvider and that the provider functions as expected.

    auto testContext = TestHelpers::CreateTestContext();

    // Create the render index.
    hvt::RenderIndexProxyPtr renderIndexProxy;
    hvt::RendererDescriptor rendererDesc;
    rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
    rendererDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(renderIndexProxy, rendererDesc);

    // Create a FramePass which internally creates a SelectionHelper. (SelectionSettingsProvider)
    const SdfPath framePassId("/TestFramePassSelection");
    hvt::FramePassDescriptor desc { renderIndexProxy->RenderIndex(), framePassId, {} };
    auto framePass = std::make_unique<hvt::FramePass>(desc.uid.GetText());
    framePass->Initialize(desc);

    // Get the SelectionSettingsProvider from the FramePass.
    hvt::SelectionSettingsProviderWeakPtr selectionSettingsProvider =
        framePass->GetSelectionSettingsAccessor();

    // Verify we got a valid provider.
    ASSERT_FALSE(selectionSettingsProvider.expired());
    auto provider = selectionSettingsProvider.lock();
    ASSERT_TRUE(provider != nullptr);

    // Test 1: Verify initial default settings.
    const hvt::SelectionSettings& initialSettings = provider->GetSettings();
    EXPECT_TRUE(initialSettings.enableSelection);
    EXPECT_TRUE(initialSettings.enableOutline);
    EXPECT_EQ(initialSettings.outlineRadius, 5);                    // Default value
    EXPECT_EQ(initialSettings.selectionColor, GfVec4f(1, 1, 0, 1)); // Default yellow
    EXPECT_EQ(initialSettings.locateColor, GfVec4f(0, 0, 1, 1));    // Default blue

    // Test 2: Verify initial buffer paths. (should be empty initially)
    const hvt::SelectionBufferPaths& initialBuffers = provider->GetBufferPaths();
    EXPECT_TRUE(initialBuffers.primIdBufferPath.IsEmpty());
    EXPECT_TRUE(initialBuffers.instanceIdBufferPath.IsEmpty());
    EXPECT_TRUE(initialBuffers.elementIdBufferPath.IsEmpty());
    EXPECT_TRUE(initialBuffers.depthBufferPath.IsEmpty());

    // Test 3: Verify GetSelection functionality. (should return empty initially)
    auto selectPaths = framePass->GetSelection(HdSelection::HighlightModeSelect);
    auto locatePaths = framePass->GetSelection(HdSelection::HighlightModeLocate);
    EXPECT_TRUE(selectPaths.empty());
    EXPECT_TRUE(locatePaths.empty());

    // Test 4: Test setting selection data and verifying GetSelection returns correct results.

    // Create test selection with different highlight modes.
    auto testSelection = std::make_shared<HdSelection>();
    testSelection->AddRprim(HdSelection::HighlightModeSelect, SdfPath("/TestPrim1"));
    testSelection->AddRprim(HdSelection::HighlightModeSelect, SdfPath("/TestPrim2"));
    testSelection->AddRprim(HdSelection::HighlightModeLocate, SdfPath("/TestPrim3"));

    framePass->SetSelection(testSelection);

    // Verify GetSelection returns the correct paths for each highlight mode.
    auto selectedPaths = framePass->GetSelection(HdSelection::HighlightModeSelect);
    auto locatedPaths  = framePass->GetSelection(HdSelection::HighlightModeLocate);

    EXPECT_EQ(selectedPaths.size(), 2);
    EXPECT_EQ(locatedPaths.size(), 1);

    // Check specific paths.
    std::set<std::string> selectedStrings;
    for (const auto& path : selectedPaths)
    {
        selectedStrings.insert(path.GetAsString());
    }
    EXPECT_TRUE(selectedStrings.count("/TestPrim1"));
    EXPECT_TRUE(selectedStrings.count("/TestPrim2"));

    EXPECT_EQ(locatedPaths[0].GetAsString(), "/TestPrim3");

    // Test 5: Test dynamic updates through FramePass parameters.
    // (This is the typical way settings are updated in practice)
    hvt::FramePassParams& framePassParams = framePass->params();
    TestHelpers::TestStage stage(testContext->_backend);
    ASSERT_TRUE(stage.open(testContext->_sceneFilepath));

    framePassParams.enableSelection       = false;
    framePassParams.enableOutline         = false;
    framePassParams.selectionColor        = GfVec4f(1.0f, 0.0f, 0.0f, 1.0f); // Red
    framePassParams.locateColor           = GfVec4f(0.0f, 1.0f, 0.0f, 1.0f); // Green

    // Simulate what happens during a render. - FramePass updates provider settings
    GfVec2i renderSize(testContext->width(), testContext->height());
    framePassParams.renderBufferSize = renderSize;

    framePassParams.viewInfo.viewport         = { { 0, 0 }, renderSize };
    framePassParams.viewInfo.viewMatrix       = stage.viewMatrix();
    framePassParams.viewInfo.projectionMatrix = stage.projectionMatrix();
    framePassParams.viewInfo.lights           = stage.defaultLights();
    framePassParams.viewInfo.material         = stage.defaultMaterial();
    framePassParams.viewInfo.ambient          = stage.defaultAmbient();

    framePassParams.colorspace      = HdxColorCorrectionTokens->disabled;
    framePassParams.backgroundColor = TestHelpers::ColorDarkGrey;
    framePassParams.enablePresentation = false;

    framePass->Render();

    // Verify the provider's settings were updated.
    const hvt::SelectionSettings& updatedSettings = provider->GetSettings();
    EXPECT_FALSE(updatedSettings.enableSelection);
    EXPECT_FALSE(updatedSettings.enableOutline);
    EXPECT_EQ(updatedSettings.selectionColor, GfVec4f(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(updatedSettings.locateColor, GfVec4f(0.0f, 1.0f, 0.0f, 1.0f));

    // Test 6: Test clearing selection.
    framePass->SetSelection(nullptr);

    // Verify GetSelection returns empty results.
    auto clearedSelectPaths = framePass->GetSelection(HdSelection::HighlightModeSelect);
    auto clearedLocatePaths = framePass->GetSelection(HdSelection::HighlightModeLocate);
    EXPECT_TRUE(clearedSelectPaths.empty());
    EXPECT_TRUE(clearedLocatePaths.empty());

    // Clean up. - FramePass destructor will handle cleanup
}