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

namespace
{

struct FramePassInstance
{
    hvt::RenderIndexProxyPtr renderIndex;
    HdSceneIndexBaseRefPtr sceneIndex;
    hvt::FramePassPtr framePass;
};

/// Helper method to simplify the unit test code.
FramePassInstance _CreateFramePass(
    const std::shared_ptr<TestHelpers::TestContext>& context, TestHelpers::TestStage& stage)
{
    FramePassInstance frameInst;

    // Creates the render index by providing the hgi driver and the requested renderer name.

    hvt::RendererDescriptor renderDesc;
    renderDesc.hgiDriver    = &context->_backend->hgiDriver();
    renderDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(frameInst.renderIndex, renderDesc);

    // Creates the scene index.

    frameInst.sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
    frameInst.renderIndex->RenderIndex()->InsertSceneIndex(
        frameInst.sceneIndex, SdfPath::AbsoluteRootPath());

    // Creates the frame pass instance.

    hvt::FramePassDescriptor passDesc;
    passDesc.renderIndex = frameInst.renderIndex->RenderIndex();
    passDesc.uid         = SdfPath("/sceneFramePass");
    frameInst.framePass  = hvt::ViewportEngine::CreateFramePass(passDesc);

    // Performs the first update.

    auto& params = frameInst.framePass->params();

    params.renderBufferSize = GfVec2i(context->width(), context->height());
    params.viewInfo.framing =
        hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

    params.viewInfo.viewMatrix       = stage.viewMatrix();
    params.viewInfo.projectionMatrix = stage.projectionMatrix();
    params.viewInfo.lights           = stage.defaultLights();
    params.viewInfo.material         = stage.defaultMaterial();
    params.viewInfo.ambient          = stage.defaultAmbient();

    params.colorspace      = HdxColorCorrectionTokens->sRGB;
    params.backgroundColor = TestHelpers::ColorDarkGrey;
    params.selectionColor  = TestHelpers::ColorYellow;

    return frameInst;
}

/// Prints a single set of values.
void _PrintData(
    [[maybe_unused]] std::string const& txt, [[maybe_unused]] std::vector<VtIntArray> const& values)
{
// #define _PRINT_DATA
#ifdef _PRINT_DATA
    std::cout << txt << ": " << std::endl;
    for (const auto& val : values)
    {
        for (const auto& elt : val)
        {
            std::cout << elt << ",";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
#endif
}

} // namespace

#if TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestSearchPrims)
#else
HVT_TEST(TestViewportToolbox, TestSearchPrims)
#endif
{
    // The unit test searches for some prims and highlights them.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);

    // Works with an arbitrary basic scene containing several prims.
    const std::string filepath =
        TestHelpers::getAssetsDataFolder().string() + "/usd/default_scene.usdz";
    ASSERT_TRUE(stage.open(filepath));

    FramePassInstance frameInst = _CreateFramePass(context, stage);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    // Keep the search result for later validation.
    HdSelectionSharedPtr sel;

    auto render = [&]()
    {
        // Selects some prims to validate the display to highlight them.

        sel = frameInst.framePass->Pick(HdxPickTokens->pickPrimsAndInstances);
        frameInst.framePass->SetSelection(sel);

        hvt::FramePassParams& params = frameInst.framePass->params();
        params.enablePresentation    = context->presentationEnabled();

        // Renders with the selection highlights.
        frameInst.framePass->Render();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, frameInst.framePass.get());

    // Checks the selection content.
    // Note: Even if the rendered image is correct check the content.

    ASSERT_FALSE(sel->IsEmpty());
    ASSERT_EQ(sel->GetAllSelectedPrimPaths().size(), 1);
    ASSERT_EQ(sel->GetSelectedPrimPaths(HdSelection::HighlightModeSelect).size(), 1);
    ASSERT_EQ(sel->GetSelectedPointColors().size(), 0);

    auto primState = sel->GetPrimSelectionState(HdSelection::HighlightModeSelect,
        sel->GetSelectedPrimPaths(HdSelection::HighlightModeSelect)[0]);
    ASSERT_TRUE(primState != nullptr);

    ASSERT_EQ(primState->fullySelected, true);
    ASSERT_EQ(primState->instanceIndices.size(), 0);
    ASSERT_EQ(primState->elementIndices.size(), 0);
    ASSERT_EQ(primState->edgeIndices.size(), 0);
    ASSERT_EQ(primState->pointIndices.size(), 0);
    ASSERT_EQ(primState->pointColorIndices.size(), 0);

    // Validates the rendering result.

    const std::string computedImagePath = TestHelpers::getComputedImagePath();
    ASSERT_TRUE(context->validateImages(computedImagePath, TestHelpers::gTestNames.fixtureName));
}

#if TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestSearchFaces)
#else
HVT_TEST(TestViewportToolbox, TestSearchFaces)
#endif
{
    // The unit test searches for some faces and highlights them.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);

    // Works with an arbitrary basic scene containing several prims.
    const std::string filepath =
        TestHelpers::getAssetsDataFolder().string() + "/usd/default_scene.usdz";
    ASSERT_TRUE(stage.open(filepath));

    FramePassInstance frameInst = _CreateFramePass(context, stage);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    // Keep the search result for later validation.
    HdSelectionSharedPtr sel;

    auto render = [&]()
    {
        // Selects some faces to validate the display to highlight them.

        sel = frameInst.framePass->Pick(HdxPickTokens->pickFaces);
        frameInst.framePass->SetSelection(sel);

        hvt::FramePassParams& params = frameInst.framePass->params();
        params.enablePresentation    = context->presentationEnabled();

        // Renders with the selection highlights.
        frameInst.framePass->Render();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, frameInst.framePass.get());

    // Checks the selection content.
    // Note: Even if the rendered image is correct check the content.

    ASSERT_FALSE(sel->IsEmpty());
    ASSERT_EQ(sel->GetAllSelectedPrimPaths().size(), 17);
    ASSERT_EQ(sel->GetSelectedPrimPaths(HdSelection::HighlightModeSelect).size(), 17);
    ASSERT_EQ(sel->GetSelectedPointColors().size(), 0);

    auto primState = sel->GetPrimSelectionState(HdSelection::HighlightModeSelect,
        sel->GetSelectedPrimPaths(HdSelection::HighlightModeSelect)[0]);
    ASSERT_TRUE(primState != nullptr);

    ASSERT_EQ(primState->fullySelected, false);
    ASSERT_EQ(primState->instanceIndices.size(), 0);
    ASSERT_EQ(primState->elementIndices.size(), 1); // Found one list of faces.
    ASSERT_EQ(primState->edgeIndices.size(), 0);
    ASSERT_EQ(primState->pointIndices.size(), 0);
    ASSERT_EQ(primState->pointColorIndices.size(), 0);

    // Validates the rendering result.

    std::string computedFileName = TestHelpers::getComputedImagePath();

#if PXR_VERSION <= 2505 && __APPLE__
    computedFileName = "origin_dev/02505/" + computedFileName;
#endif

    ASSERT_TRUE(context->validateImages(computedImageName, TestHelpers::gTestNames.fixtureName));
}

// FIXME: Android unit test framework does not report the error message, make it impossible to fix
// issues. Refer to OGSMOD-5546.
//
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestSearchEdges)
#else
HVT_TEST(TestViewportToolbox, TestSearchEdges)
#endif
{
    // The unit test searches for some edges.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    FramePassInstance frameInst = _CreateFramePass(context, stage);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    // Keep the search result for later validation.
    HdSelectionSharedPtr sel;

    auto render = [&]()
    {
        // Selecting some edges should do nothing.

        sel = frameInst.framePass->Pick(HdxPickTokens->pickEdges);
        frameInst.framePass->SetSelection(sel);

        hvt::FramePassParams& params = frameInst.framePass->params();
        params.enablePresentation    = context->presentationEnabled();

        // Renders with the selection highlights.
        frameInst.framePass->Render();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, frameInst.framePass.get());

    // Checks the selection content.
    // Note: Even if the rendered image is correct check the content.

    ASSERT_FALSE(sel->IsEmpty());
    ASSERT_EQ(sel->GetAllSelectedPrimPaths().size(), 1);
    ASSERT_EQ(sel->GetSelectedPrimPaths(HdSelection::HighlightModeSelect).size(), 1);
    ASSERT_EQ(sel->GetSelectedPointColors().size(), 0);

    auto primState = sel->GetPrimSelectionState(HdSelection::HighlightModeSelect,
        sel->GetSelectedPrimPaths(HdSelection::HighlightModeSelect)[0]);
    ASSERT_TRUE(primState != nullptr);

    ASSERT_EQ(primState->fullySelected, false);
    ASSERT_EQ(primState->instanceIndices.size(), 0);
    ASSERT_EQ(primState->elementIndices.size(), 0);
    ASSERT_EQ(primState->edgeIndices.size(), 1); // Found one list of edges.
    ASSERT_EQ(primState->pointIndices.size(), 0);
    ASSERT_EQ(primState->pointColorIndices.size(), 0);

    std::vector<VtIntArray> results;
#if defined(_WIN32) || defined(__linux__)
    results =
        { { 0, 3, 21, 60, 69, 75, 84, 93, 102, 105, 108, 109, 110, 111, 112, 113, 114, 117, 123,
            135, 141, 153, 159, 162, 165, 171, 177, 183, 189, 195, 207, 213, 216, 219, 225, 228,
            231, 237, 243, 249, 261, 264, 267, 276, 321, 327, 328, 329, 333, 561, 570, 573, 618,
            619, 620, 621, 624, 627, 628, 630, 633, 637, 639, 642, 646, 648, 651, 654, 655, 657,
            660, 663, 747, 749, 768, 769, 770, 771, 774, 780, 783, 786, 787, 788, 789, 792, 795,
            796, 797, 799, 800, 801, 804, 807, 808, 810, 826, 940, 945, 948, 951, 1461, 1590, 1599,
            1626, 1656, 1659, 1665, 1692, 1725, 1761, 1791, 1890, 1926, 1977, 1983, 1992, 2022,
            2049, 2070, 4089, 4173, 4701, 4702, 4704, 4719, 4725, 4728, 4734, 4743, 4749, 4755,
            4764, 4767, 4773, 4782, 4785, 4788, 4791, 4794, 4800, 4803, 4956, 5898, 5955, 5970,
            5976, 5991, 6003, 6006, 6012, 6018, 6036, 6045 } };

#if defined(_WIN32) && defined(ENABLE_VULKAN)
    if (TestHelpers::gRunVulkanTests)
    {
        results =
        { { 0, 3, 21, 60, 69, 75, 84, 93, 102, 105, 108, 109, 110, 111, 112, 113, 114, 117, 123,
            135, 141, 153, 159, 162, 165, 171, 177, 183, 189, 195, 207, 213, 216, 219, 225, 228,
            231, 237, 243, 249, 261, 264, 267, 276, 321, 327, 328, 329, 333, 561, 570, 573, 618,
            619, 620, 621, 624, 627, 628, 630, 633, 637, 639, 642, 646, 648, 651, 654, 655, 657,
            660, 663, 747, 749, 768, 769, 770, 771, 774, 780, 783, 786, 787, 788, 789, 792, 795,
            796, 797, 799, 800, 801, 804, 807, 808, 810, 826, 945, 948, 1461, 1590, 1599,
            1626, 1656, 1659, 1665, 1692, 1725, 1761, 1794, 1890, 1926, 1977, 1983, 1992, 2022,
            2049, 2070, 4089, 4173, 4701, 4702, 4704, 4719, 4725, 4728, 4734, 4743, 4749, 4755,
            4764, 4767, 4773, 4782, 4785, 4788, 4791, 4794, 4800, 4803, 4956, 5898, 5955, 5970,
            5976, 5991, 6003, 6006, 6012, 6018, 6036, 6045 } };
    }
#endif

#elif PXR_VERSION <= 2505 && __APPLE__
    results =
        { { 102, 105, 108, 109, 110, 111, 112, 113, 114, 117, 159, 243, 618, 619, 620, 621, 624,
            627, 628, 630, 633, 636, 637, 639, 642, 646, 648, 651, 655, 657, 660, 768, 769, 770,
            771, 774, 777, 778, 780, 783, 786, 787, 789, 792, 795, 796, 798, 799, 801, 804, 807,
            808, 810 } };
#else
    results =
        { { 102, 105, 108, 109, 110, 111, 112, 113, 114, 117, 159, 243, 615, 618, 619, 620, 621,
            624, 627, 628, 630, 633, 636, 637, 639, 642, 645, 646, 648, 651, 654, 655, 657, 660,
            666, 768, 769, 770, 771, 774, 777, 778, 780, 783, 786, 787, 789, 792, 795, 796, 798,
            799, 801, 804, 807, 808, 810 } };
#endif

    _PrintData("Edges: ", primState->edgeIndices);
    ASSERT_TRUE(primState->edgeIndices == results);

    // Validates the rendering result.

    // As the edge selection should do nothing use an existing baseline image.
    const std::string imageFilename = std::string("TestFramePasses_MainOnly");
    ASSERT_TRUE(context->validateImages(computedImageName, imageFilename));
}

// FIXME: Android unit test framework does not report the error message, make it impossible to fix
// issues. Refer to OGSMOD-5546.
//
#if defined(__ANDROID__) || TARGET_OS_IPHONE == 1
HVT_TEST(TestViewportToolbox, DISABLED_TestSearchPoints)
#else
HVT_TEST(TestViewportToolbox, TestSearchPoints)
#endif
{
    // The unit test searches for some points.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    FramePassInstance frameInst = _CreateFramePass(context, stage);

    // Renders 10 times (i.e., arbitrary number to guarantee best result).
    int frameCount = 10;

    // Keep the search result for later validation.
    HdSelectionSharedPtr sel;

    auto render = [&]()
    {
        // Selecting some points should do nothing.

        sel = frameInst.framePass->Pick(HdxPickTokens->pickPoints);
        frameInst.framePass->SetSelection(sel);

        hvt::FramePassParams& params = frameInst.framePass->params();
        params.enablePresentation    = context->presentationEnabled();

        // Renders with the selection highlights.
        frameInst.framePass->Render();

        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, frameInst.framePass.get());

    // Checks the selection content.
    // Note: Even if the rendered image is correct check the content.

    ASSERT_FALSE(sel->IsEmpty());
    ASSERT_EQ(sel->GetAllSelectedPrimPaths().size(), 1);
    ASSERT_EQ(sel->GetSelectedPrimPaths(HdSelection::HighlightModeSelect).size(), 1);
    ASSERT_EQ(sel->GetSelectedPointColors().size(), 0);

    auto primState = sel->GetPrimSelectionState(HdSelection::HighlightModeSelect,
        sel->GetSelectedPrimPaths(HdSelection::HighlightModeSelect)[0]);
    ASSERT_TRUE(primState != nullptr);

    ASSERT_EQ(primState->fullySelected, false);
    ASSERT_EQ(primState->instanceIndices.size(), 0);
    ASSERT_EQ(primState->elementIndices.size(), 0);
    ASSERT_EQ(primState->edgeIndices.size(), 0);
    ASSERT_EQ(primState->pointIndices.size(), 1); // Found one list of points.
    ASSERT_EQ(primState->pointColorIndices.size(), 1);

    static const std::vector<VtIntArray> results
#if defined(_WIN32) || defined(__linux__)
        { { 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233,
            234, 235, 236, 237, 238, 239, 240, 241, 242, 244, 245, 247, 272, 273, 274, 290, 336,
            872, 1129, 1155, 1156, 1157, 1158, 1159, 1160, 1161, 1162, 1163, 1164, 1165, 1166, 1167,
            1168, 1169 } };
#elif PXR_VERSION <= 2505 && __APPLE__
        { { 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233,
            234, 235, 236, 237, 238, 239, 240, 241, 242, 244, 245, 247, 272, 273, 274, 290, 336,
            872, 1129, 1155, 1156, 1157, 1158, 1159, 1160, 1161, 1162, 1163, 1164, 1165, 1166, 1167,
            1168, 1169 } };
#else
        { { 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233,
            234, 235, 236, 237, 238, 239, 240, 241, 242, 244, 245, 273, 274, 290, 336, 872, 1129,
            1155, 1156, 1157, 1158, 1159, 1160, 1161, 1162, 1163, 1164, 1165, 1166, 1167, 1168,
            1169, 1171 } };
#endif

    _PrintData("Points: ", primState->pointIndices);
    ASSERT_TRUE(primState->pointIndices == results);

    // Validates the rendering result.

    // As the point selection should do nothing use an existing baseline image.
    const std::string imageFilename = std::string("TestFramePasses_MainOnly");
    ASSERT_TRUE(context->validateImages(computedImageName, imageFilename));
}

HVT_TEST(TestViewportToolbox, TestSearchUsingCube)
{
    // The unit test executes all the searches on a very basic model to better check / understand
    // the content of the search results.

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);

    // Works with an arbitrary basic scene containing several prims.
    const std::string filepath = TestHelpers::getAssetsDataFolder().string() + "/usd/cube.usda";
    ASSERT_TRUE(stage.open(filepath));

    FramePassInstance frameInst = _CreateFramePass(context, stage);

    // Only one render is needed.
    int frameCount = 1;

    // Keep the search result for later validation.
    HdSelectionSharedPtr sel1, sel2, sel3, sel4;

    auto render = [&]()
    {
        sel1 = frameInst.framePass->Pick(HdxPickTokens->pickPrimsAndInstances);

        sel2 = frameInst.framePass->Pick(HdxPickTokens->pickFaces);

        sel3 = frameInst.framePass->Pick(HdxPickTokens->pickEdges);

        sel4 = frameInst.framePass->Pick(HdxPickTokens->pickPoints);

        hvt::FramePassParams& params = frameInst.framePass->params();
        params.enablePresentation    = context->presentationEnabled();

        frameInst.framePass->Render();
        return --frameCount > 0;
    };

    // Runs the render loop (i.e., that's backend specific).

    context->run(render, frameInst.framePass.get());

    // Checks the selection content for prims.

    ASSERT_FALSE(sel1->IsEmpty());
    ASSERT_EQ(sel1->GetAllSelectedPrimPaths().size(), 1);
    ASSERT_EQ(sel1->GetAllSelectedPrimPaths()[0], SdfPath("/Root/SimpleCube"));

    // Checks the selection content for faces.

    {
        ASSERT_FALSE(sel2->IsEmpty());
        ASSERT_EQ(sel2->GetAllSelectedPrimPaths().size(), 1);

        auto primState = sel2->GetPrimSelectionState(HdSelection::HighlightModeSelect,
            sel2->GetSelectedPrimPaths(HdSelection::HighlightModeSelect)[0]);
        ASSERT_TRUE(primState != nullptr);

        ASSERT_EQ(primState->elementIndices.size(), 1); // Found one list of faces.
        static const VtIntArray results { -1, 5 };
        ASSERT_TRUE(primState->elementIndices[0] == results);
    }

    // Checks the selection content for edges.

    {
        ASSERT_FALSE(sel3->IsEmpty());
        ASSERT_EQ(sel3->GetAllSelectedPrimPaths().size(), 1);

        auto primState = sel3->GetPrimSelectionState(HdSelection::HighlightModeSelect,
            sel3->GetSelectedPrimPaths(HdSelection::HighlightModeSelect)[0]);
        ASSERT_TRUE(primState != nullptr);

        ASSERT_EQ(primState->edgeIndices.size(), 1); // Found one list of edges.
#if defined(__ANDROID__)
        static const VtIntArray results { 20 };
#else
        static const VtIntArray results { 20, 21, 22, 23 };
#endif
        ASSERT_TRUE(primState->edgeIndices[0] == results);
    }

    // Checks the selection content for points.

    {
        ASSERT_FALSE(sel4->IsEmpty());
        ASSERT_EQ(sel4->GetAllSelectedPrimPaths().size(), 1);

        auto primState = sel4->GetPrimSelectionState(HdSelection::HighlightModeSelect,
            sel4->GetSelectedPrimPaths(HdSelection::HighlightModeSelect)[0]);
        ASSERT_TRUE(primState != nullptr);

        ASSERT_EQ(primState->pointIndices.size(), 1); // Found one list of points.

        // NOTE: On the Android platform, the search result is unstable i.e., the list of points
        // is not always the same!
#if !defined(__ANDROID__)
        static const VtIntArray results { 0, 1, 2, 3, 4, 5, 6, 7 };
        ASSERT_TRUE(primState->pointIndices[0] == results);
#endif
    }
}
