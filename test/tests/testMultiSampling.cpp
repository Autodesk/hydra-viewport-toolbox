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

#include <RenderingFramework/TestContextCreator.h>
#include <RenderingFramework/TestHelpers.h>

#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/sceneIndex/displayStyleOverrideSceneIndex.h>
#include <hvt/sceneIndex/wireFrameSceneIndex.h>
#include <hvt/tasks/resources.h>

#include <pxr/base/plug/registry.h>
#include <pxr/imaging/hd/renderBuffer.h>

#include <gtest/gtest.h>

struct FramePassData
{
    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr framePass;
};

struct MsaaTestSettings
{
    int msaaSampleCount        = 4;
    bool enableMsaa            = true;
    bool enableColorCorrection = true;
    bool enableLights          = false;
    bool copyPassContents      = true;
    bool createSkyDome         = true;
    bool wireframeSecondPass   = false;
    pxr::GfVec2i renderSize    = pxr::GfVec2i(300, 200);
};

FramePassData LoadFramePass(pxr::HdDriver* hgiDriver, pxr::UsdStageRefPtr stage,
    pxr::SdfPath const& passId,
    std::function<pxr::HdSceneIndexBaseRefPtr(pxr::HdSceneIndexBaseRefPtr const&)>
        createSceneIndexOverrides = nullptr)
{
    FramePassData passData;

    // Creates the renderer.
    hvt::RendererDescriptor renderDesc;
    renderDesc.hgiDriver    = hgiDriver;
    renderDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(passData.renderIndex, renderDesc);

    // Creates the scene index and adds overrides, if applicable.
    pxr::HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage);
    if (createSceneIndexOverrides)
    {
        sceneIndex = createSceneIndexOverrides(sceneIndex);
    }
    passData.renderIndex->RenderIndex()->InsertSceneIndex(
        sceneIndex, pxr::SdfPath::AbsoluteRootPath());

    // Creates the frame pass instance.
    hvt::FramePassDescriptor passDesc;
    passDesc.renderIndex = passData.renderIndex->RenderIndex();
    passDesc.uid         = passId;
    passData.framePass   = hvt::ViewportEngine::CreateFramePass(passDesc);

    return passData;
}

// This function applied FramePass parameters that are common for all passes.
void setCommonFramePassParams(hvt::FramePassParams& outParams, TestHelpers::TestStage const& stage,
    MsaaTestSettings testSettings)
{
    pxr::GlfSimpleLightVector activeLights = {};

    if (testSettings.enableLights)
    {
        activeLights = stage.defaultLights();
        // Reduce specular: this is important to prevent noticeable aliasing with lighting
        // values above 1.0, even with MSAA enabled.
        activeLights[0].SetSpecular(pxr::GfVec4f(0.1f, 0.1f, 0.1f, 1.0f));
    }

    // Add a dome light to the default stage lights.
    // This dome light is required to activate the SkyDome.
    if (testSettings.createSkyDome)
    {
        pxr::GlfSimpleLight domeLight;
        domeLight.SetID(pxr::SdfPath("DomeLight"));
        domeLight.SetIsDomeLight(true);
        activeLights.push_back(domeLight);
    }

    outParams.renderBufferSize = testSettings.renderSize;

    outParams.viewInfo.framing =
        hvt::ViewParams::GetDefaultFraming(testSettings.renderSize[0], testSettings.renderSize[1]);
    outParams.viewInfo.viewMatrix       = stage.viewMatrix();
    outParams.viewInfo.projectionMatrix = stage.projectionMatrix();
    outParams.viewInfo.lights           = activeLights;
    outParams.viewInfo.material         = stage.defaultMaterial();
    outParams.viewInfo.ambient          = stage.defaultAmbient();

    outParams.collection = pxr::HdRprimCollection(
        pxr::HdTokens->geometry, pxr::HdReprSelector(pxr::TfToken("refined")));
    outParams.renderParams.wireframeColor = pxr::GfVec4f(0.263f, 1.0f, 0.639f, 1.0f);
    outParams.selectionColor              = pxr::GfVec4f(1.0f, 1.0f, 0.0f, 0.5f);

    outParams.msaaSampleCount     = testSettings.msaaSampleCount;
    outParams.enableMultisampling = testSettings.enableMsaa;
}

FramePassData LoadAndInitializeFirstPass(pxr::HdDriver* pHgiDriver,
    TestHelpers::TestStage const& testStage, MsaaTestSettings const& testSettings)
{
    FramePassData passData0 = LoadFramePass(pHgiDriver, testStage.stage(), pxr::SdfPath("/Pass0"));

    hvt::FramePass* pass0 = passData0.framePass.get();

    if (testSettings.createSkyDome)
    {
        // Find first render task, to insert the SkyDome before other render tasks.
        pxr::SdfPath skyDomeInsertPos =
            pass0->GetTaskManager()->GetTasks(hvt::TaskFlagsBits::kRenderTaskBit)[0]->GetId();

        // Defines the layer parameter getter function.
        const auto getLayerSettings = [pass0]() -> hvt::BasicLayerParams const*
        { return &pass0->params(); };

        // Creates and adds the SkyDomeTask to the main pass.
        hvt::CreateSkyDomeTask(pass0->GetTaskManager(), pass0->GetRenderBufferAccessor(),
            getLayerSettings, skyDomeInsertPos, hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Initialize FramePass 0 parameters.
    auto& passParams0 = pass0->params();
    {
        setCommonFramePassParams(passParams0, testStage, testSettings);

        passParams0.renderTags = { pxr::HdRenderTagTokens->geometry, pxr::HdRenderTagTokens->render,
            pxr::HdRenderTagTokens->proxy, pxr::HdRenderTagTokens->guide };

        passParams0.colorspace           = testSettings.enableColorCorrection
                      ? pxr::HdxColorCorrectionTokens->sRGB
                      : pxr::HdxColorCorrectionTokens->disabled;
        passParams0.clearBackgroundColor = true;
        passParams0.backgroundColor      = TestHelpers::ColorWhite;

        // Do not display right now, wait for the second frame pass.
        passParams0.enablePresentation = false;
    }

    return passData0;
}

FramePassData LoadAndInitializeSecondPass(pxr::HdDriver* pHgiDriver,
    TestHelpers::TestStage const& pass0TestStage, pxr::UsdStageRefPtr const& pass1Stage,
    MsaaTestSettings const& testSettings, bool enablePresentTask)
{
    auto addSceneIndices = [testSettings](pxr::HdSceneIndexBaseRefPtr const& inputSceneIndex)
    {
        if (!testSettings.wireframeSecondPass)
        {
            return inputSceneIndex;
        }
        
        pxr::HdSceneIndexBaseRefPtr si = hvt::DisplayStyleOverrideSceneIndex::New(inputSceneIndex);
        si                             = hvt::WireFrameSceneIndex::New(si);
        return si;
    };

    // Create the Frame Pass, the Storm Render Delegate and the Scene Index using the usd stage.
    FramePassData passData1 =
        LoadFramePass(pHgiDriver, pass1Stage, pxr::SdfPath("/Pass1"), addSceneIndices);

    // Initialize FramePass 1 parameters.
    auto& passParams1 = passData1.framePass->params();
    {
        setCommonFramePassParams(passParams1, pass0TestStage, testSettings);
        passParams1.colorspace = pxr::HdxColorCorrectionTokens->disabled;

        // Do not clear the background as it contains the previous frame pass result.
        passParams1.clearBackgroundColor = false;
        passParams1.clearBackgroundDepth = false;

        passParams1.enablePresentation = enablePresentTask;
    }

    return passData1;
}

void TestMultiSampling(MsaaTestSettings const& testSettings, std::string const& test_name,
    uint8_t pixelValueThreshold = 1)
{
    auto testContext =
        TestHelpers::CreateTestContext(testSettings.renderSize[0], testSettings.renderSize[1]);

    pxr::HdDriver* pHgiDriver = &testContext->_backend->hgiDriver();

    // ------------------------------------------------------------------------------
    // Create and setup first render pass, "Pass0".
    // ------------------------------------------------------------------------------

    TestHelpers::TestStage testStage(testContext->_backend);

    // Pass0 contains the default test scene.
    ASSERT_TRUE(testStage.open(testContext->_sceneFilepath));

    FramePassData passData0 = LoadAndInitializeFirstPass(pHgiDriver, testStage, testSettings);

    // ------------------------------------------------------------------------------
    // Create and setup second render pass, "Pass1".
    // ------------------------------------------------------------------------------

    // Load another stage for pass 1.
    auto pass1stage = hvt::ViewportEngine::CreateStageFromFile(
        (TestHelpers::getAssetsDataFolder() / "usd" / "cube_msaa_transformed.usda")
            .generic_u8string());

    // Note: Lighting and view parameters from the test stage (pass0) are reused in the 2nd pass.
    FramePassData passData1 =
        LoadAndInitializeSecondPass(pHgiDriver, testStage, pass1stage, testSettings,
            testContext->presentationEnabled());

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    auto render = [&]()
    {
        hvt::FramePass& framePass0 = *passData0.framePass;
        hvt::FramePass& framePass1 = *passData1.framePass;

        framePass0.Render();

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        testContext->_backend->waitForGPUIdle();

        hvt::RenderBufferBindings inputAOVs;
        inputAOVs = framePass0.GetRenderBufferBindingsForNextPass(
            { pxr::HdAovTokens->color, pxr::HdAovTokens->depth }, testSettings.copyPassContents);

        // Render the 2nd frame pass into the pass 0 AOVs.
        auto pass1RenderTasks = framePass1.GetRenderTasks(inputAOVs);
        framePass1.Render(pass1RenderTasks);

        // Force GPU sync. Wait for all GPU commands to complete before proceeding.
        // This ensures render operations are fully finished before the next frame
        // or validation step, preventing race conditions and ensuring consistent results.
        testContext->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    // Runs the render loop.
    testContext->run(render, passData1.framePass.get());

    // Saves the frame pass parameters to a file.
    // Note: The is disabled by default, but could be enable to compare the frame pass parameters
    // of this test with the frame pass parameters of other application.
    if (0)
    {
        std::ostringstream passParamsStream;
        passParamsStream << "Main Frame Pass parameters:";
        passParamsStream << (*passData0.framePass);
        passParamsStream << "Second Frame Pass parameters:";
        passParamsStream << (*passData1.framePass);

        std::ofstream passParamsFile("hvt_passParams.txt");
        passParamsFile << passParamsStream.str();
        passParamsFile.close();
    }

    // Validates the rendering result.
    ASSERT_TRUE(testContext->_backend->saveImage(test_name));

    ASSERT_TRUE(testContext->_backend->compareImages(test_name, pixelValueThreshold));
}

// Describes a pair of chained frame passes, and the render buffer sharing it should result in.
struct MsaaChainingExpectation
{
    /// The settings of the first pass, whose AOVs are chained into the second pass.
    MsaaTestSettings pass0;
    /// The settings of the second pass, which renders into the chained AOVs.
    MsaaTestSettings pass1;
    /// Whether the second pass is expected to allocate render buffers of its own instead of
    /// rendering into the ones it received from the first pass.
    bool expectsOwnBuffers = true;
};

/// Builds the settings of two chained passes that only differ in their multisample state.
MsaaChainingExpectation MakeChainingExpectation(bool pass0Msaa, bool pass1Msaa)
{
    const auto makeSettings = [](bool enableMsaa)
    {
        MsaaTestSettings settings;

        settings.msaaSampleCount       = enableMsaa ? 4 : 1;
        settings.enableMsaa            = enableMsaa;
        settings.enableColorCorrection = false;
        settings.enableLights          = true;

        // Copying the contents is what provides the second pass with input textures, and therefore
        // what exercises the buffer chaining under test.
        settings.copyPassContents = true;

        // The SkyDomeTask is unsupported on some platforms and is not needed here.
        settings.createSkyDome       = false;
        settings.wireframeSecondPass = false;

        return settings;
    };

    MsaaChainingExpectation expectation;

    expectation.pass0 = makeSettings(pass0Msaa);
    expectation.pass1 = makeSettings(pass1Msaa);

    // Buffers can only be shared between passes that agree on the sample count.
    expectation.expectsOwnBuffers = (pass0Msaa != pass1Msaa);

    return expectation;
}

/// Chains two frame passes with the given multisample settings, and validates which render buffers
/// the second pass ends up drawing into.
///
/// Note: this is a state assertion rather than an image comparison. Attaching buffers of differing
/// sample counts is invalid, but backends disagree on whether they reject it, tolerate it, or
/// render something plausible, so the buffer allocation is asserted instead of the pixels.
///
/// Note: buffer sharing also depends on progressive rendering being disabled, which is the default
/// (it is opted into with the AGP_ENABLE_PROGRESSIVE_RENDERING environment variable).
void TestMsaaBufferChaining(MsaaChainingExpectation const& expectation)
{
    MsaaTestSettings const& settings0 = expectation.pass0;
    MsaaTestSettings const& settings1 = expectation.pass1;

    // Both passes render into buffers of the same size, only the sample counts differ.
    ASSERT_EQ(settings0.renderSize, settings1.renderSize);

    auto testContext =
        TestHelpers::CreateTestContext(settings0.renderSize[0], settings0.renderSize[1]);

    pxr::HdDriver* pHgiDriver = &testContext->_backend->hgiDriver();

    // ------------------------------------------------------------------------------
    // Create and setup both render passes.
    // ------------------------------------------------------------------------------

    TestHelpers::TestStage testStage(testContext->_backend);

    ASSERT_TRUE(testStage.open(testContext->_sceneFilepath));

    FramePassData passData0 = LoadAndInitializeFirstPass(pHgiDriver, testStage, settings0);

    auto pass1stage = hvt::ViewportEngine::CreateStageFromFile(
        (TestHelpers::getAssetsDataFolder() / "usd" / "cube_msaa_transformed.usda")
            .generic_u8string());

    FramePassData passData1 = LoadAndInitializeSecondPass(
        pHgiDriver, testStage, pass1stage, settings1, testContext->presentationEnabled());

    // Renders more than one frame, so that the buffer decision is also exercised on the frames
    // where the chained inputs are unchanged.
    int frameCount = 3;

    auto render = [&]()
    {
        hvt::FramePass& framePass0 = *passData0.framePass;
        hvt::FramePass& framePass1 = *passData1.framePass;

        framePass0.Render();

        // Force GPU sync, so the render operations are complete before the next frame pass.
        testContext->_backend->waitForGPUIdle();

        const hvt::RenderBufferBindings inputAOVs = framePass0.GetRenderBufferBindingsForNextPass(
            { pxr::HdAovTokens->color, pxr::HdAovTokens->depth }, settings1.copyPassContents);

        // Render the 2nd frame pass into the pass 0 AOVs.
        framePass1.Render(framePass1.GetRenderTasks(inputAOVs));

        testContext->_backend->waitForGPUIdle();

        return --frameCount > 0;
    };

    // Runs the render loop.
    testContext->run(render, passData1.framePass.get());

    // ------------------------------------------------------------------------------
    // Validate the render buffers of both passes.
    // ------------------------------------------------------------------------------

    pxr::HdRenderBuffer* color0 = passData0.framePass->GetRenderBuffer(pxr::HdAovTokens->color);
    pxr::HdRenderBuffer* depth0 = passData0.framePass->GetRenderBuffer(pxr::HdAovTokens->depth);
    pxr::HdRenderBuffer* color1 = passData1.framePass->GetRenderBuffer(pxr::HdAovTokens->color);
    pxr::HdRenderBuffer* depth1 = passData1.framePass->GetRenderBuffer(pxr::HdAovTokens->depth);

    ASSERT_NE(color0, nullptr);
    ASSERT_NE(depth0, nullptr);
    ASSERT_NE(color1, nullptr);
    ASSERT_NE(depth1, nullptr);

    if (expectation.expectsOwnBuffers)
    {
        // The sample counts differ, so the second pass cannot render into the buffers it received
        // from the first pass; it must have allocated its own.
        EXPECT_NE(color1, color0);
        EXPECT_NE(depth1, depth0);
    }
    else
    {
        // The sample counts match, so the second pass renders into the first pass's buffers.
        EXPECT_EQ(color1, color0);
        EXPECT_EQ(depth1, depth0);
    }

    // Either way, both passes must end up with buffers at their own sample count.
    EXPECT_EQ(color0->IsMultiSampled(), settings0.enableMsaa);
    EXPECT_EQ(depth0->IsMultiSampled(), settings0.enableMsaa);
    EXPECT_EQ(color1->IsMultiSampled(), settings1.enableMsaa);
    EXPECT_EQ(depth1->IsMultiSampled(), settings1.enableMsaa);
}

// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
#if defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaChainedPassResolvedToMsaa)
#else
HVT_TEST(TestViewportToolbox, TestMsaaChainedPassResolvedToMsaa)
#endif
{
    // A multisampled pass chained after a resolved one, i.e. the overlay case: the second pass must
    // allocate its own multisampled buffers rather than inherit the single-sampled ones.
    TestMsaaBufferChaining(MakeChainingExpectation(false, true));
}

// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
#if defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaChainedPassMsaaToResolved)
#else
HVT_TEST(TestViewportToolbox, TestMsaaChainedPassMsaaToResolved)
#endif
{
    // The opposite mismatch: a resolved pass chained after a multisampled one.
    TestMsaaBufferChaining(MakeChainingExpectation(true, false));
}

// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
#if defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaChainedPassMatchingSampleCount)
#else
HVT_TEST(TestViewportToolbox, TestMsaaChainedPassMatchingSampleCount)
#endif
{
    // Matching sample counts must keep sharing the buffers, i.e. the mismatch detection must not
    // allocate buffers the passes could have shared.
    TestMsaaBufferChaining(MakeChainingExpectation(true, true));
}

// FIXME: IOS does not support the SkyDomeTask.
// Refer to OGSMOD-8001
// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaAA4x)
#else
HVT_TEST(TestViewportToolbox, TestMsaaAA4x)
#endif
{
    MsaaTestSettings testSettings;

    testSettings.msaaSampleCount       = 4;
    testSettings.enableMsaa            = true;
    testSettings.enableColorCorrection = true;
    testSettings.enableLights          = false;
    testSettings.copyPassContents      = true;
    testSettings.createSkyDome         = true;
    testSettings.wireframeSecondPass   = false;
    testSettings.renderSize            = pxr::GfVec2i(300, 200);

    // Increased pixel value threshold for differences between runs.
    const uint8_t pixelValueThreshold = 20;
    TestMultiSampling(testSettings, TestHelpers::gTestNames.fixtureName, pixelValueThreshold);
}

// FIXME: IOS does not support the SkyDomeTask.
// Refer to OGSMOD-8001
// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
// FIXME: Failure to render SkyDomeTask with Linux without MSAA.
// Refer to OGSMOD-8007
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1 || defined(__linux__)
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaAAOff)
#else
HVT_TEST(TestViewportToolbox, TestMsaaAAOff)
#endif
{
    MsaaTestSettings testSettings;

    testSettings.msaaSampleCount       = 1;
    testSettings.enableMsaa            = false;
    testSettings.enableColorCorrection = true;
    testSettings.enableLights          = false;
    testSettings.copyPassContents      = true;
    testSettings.createSkyDome         = true;
    testSettings.wireframeSecondPass   = false;
    testSettings.renderSize            = pxr::GfVec2i(300, 200);

    // Increased pixel value threshold for differences between runs.
    const uint8_t pixelValueThreshold = 20;
    TestMultiSampling(testSettings, TestHelpers::gTestNames.fixtureName, pixelValueThreshold);
}

// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
#if defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaNoSkyNoCopyNoColorCorrectionAA4x)
#else
HVT_TEST(TestViewportToolbox, TestMsaaNoSkyNoCopyNoColorCorrectionAA4x)
#endif
{
    MsaaTestSettings testSettings;

    testSettings.msaaSampleCount       = 4;
    testSettings.enableMsaa            = true;
    testSettings.enableColorCorrection = false;
    testSettings.enableLights          = true;
    testSettings.copyPassContents      = false;
    testSettings.createSkyDome         = false;
    testSettings.wireframeSecondPass   = false;
    testSettings.renderSize            = pxr::GfVec2i(300, 200);

    // Increased pixel value threshold for differences between runs.
    const uint8_t pixelValueThreshold = 20;
    TestMultiSampling(testSettings, TestHelpers::gTestNames.fixtureName, pixelValueThreshold);
}

// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
#if defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaNoSkyNoCopyNoColorCorrectionAAOff)
#else
HVT_TEST(TestViewportToolbox, TestMsaaNoSkyNoCopyNoColorCorrectionAAOff)
#endif
{
    MsaaTestSettings testSettings;

    testSettings.msaaSampleCount       = 1;
    testSettings.enableMsaa            = false;
    testSettings.enableColorCorrection = false;
    testSettings.enableLights          = true;
    testSettings.copyPassContents      = false;
    testSettings.createSkyDome         = false;
    testSettings.wireframeSecondPass   = false;
    testSettings.renderSize            = pxr::GfVec2i(300, 200);

    // Increased pixel value threshold for differences between runs.
    const uint8_t pixelValueThreshold = 20;
    TestMultiSampling(testSettings, TestHelpers::gTestNames.fixtureName, pixelValueThreshold);
}

// FIXME: wireframe does not work on macOS/Metal.
// Refer to https://forum.aousd.org/t/hdstorm-mesh-wires-drawing-issue-in-usd-24-05-on-macos/1523
// FIXME: IOS does not support the SkyDomeTask.
// Refer to OGSMOD-8001
// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
#if defined(__APPLE__) || defined(__ANDROID__)
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaWireframeAA4x)
#else
HVT_TEST(TestViewportToolbox, TestMsaaWireframeAA4x)
#endif
{
    MsaaTestSettings testSettings;

    testSettings.msaaSampleCount       = 4;
    testSettings.enableMsaa            = true;
    testSettings.enableColorCorrection = true;
    testSettings.enableLights          = false;
    testSettings.copyPassContents      = true;
    testSettings.createSkyDome         = true;
    testSettings.wireframeSecondPass   = true;
    testSettings.renderSize            = pxr::GfVec2i(300, 200);

    // Increased pixel value threshold for differences between runs.
    const uint8_t pixelValueThreshold = 20;
    TestMultiSampling(testSettings, TestHelpers::gTestNames.fixtureName, pixelValueThreshold);
}

// FIXME: wireframe does not work on macOS/Metal.
// Refer to https://forum.aousd.org/t/hdstorm-mesh-wires-drawing-issue-in-usd-24-05-on-macos/1523
// FIXME: IOS does not support the SkyDomeTask.
// Refer to OGSMOD-8001
// FIXME: Android does not support multiple frame passes.
// Refer to OGSMOD-8002
// FIXME: Failure to render SkyDomeTask with Linux without MSAA.
// Refer to OGSMOD-8007
#if defined(__APPLE__) || defined(__ANDROID__) || defined(__linux__)
HVT_TEST(TestViewportToolbox, DISABLED_TestMsaaWireframeAAOff)
#else
HVT_TEST(TestViewportToolbox, TestMsaaWireframeAAOff)
#endif
{
    MsaaTestSettings testSettings;

    testSettings.msaaSampleCount       = 1;
    testSettings.enableMsaa            = false;
    testSettings.enableColorCorrection = true;
    testSettings.enableLights          = false;
    testSettings.copyPassContents      = true;
    testSettings.createSkyDome         = true;
    testSettings.wireframeSecondPass   = true;
    testSettings.renderSize            = pxr::GfVec2i(300, 200);

    // Increased pixel value threshold for differences between runs.
    const uint8_t pixelValueThreshold = 20;
    TestMultiSampling(testSettings, TestHelpers::gTestNames.fixtureName, pixelValueThreshold);
}
