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

// Tests of hvt::DepthGuideBuilder, the producer of the SSAO depth guide for tiled renders, and
// of the shared tile-framing helpers it relies on (see include/hvt/engine/depthGuideBuilder.h
// and docs/README.md, tiled rendering).

// glew.h has to be included first, through TestContextCreator.h.
#include <RenderingFramework/TestContextCreator.h>

#include <RenderingFramework/GpuImageCapture.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/depthGuideBuilder.h>
#include <hvt/engine/framePass.h>
#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/engine/taskManager.h>
#include <hvt/engine/tileIterator.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/aovInputTask.h>
#include <hvt/tasks/depthGuide.h>
#include <hvt/tasks/screenSpaceKernelEffect.h>
#include <hvt/tasks/ssaoTask.h>
#include <hvt/tasks/tileFramingUtils.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/presentTask.h>
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/texture.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/sdf/path.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace TestHelpers;
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

// The test image: odd sizes, so the last row and column of pixel blocks are partial at every
// scale, and small enough to keep the rendering tests fast.
constexpr int kImageWidth  = 501;
constexpr int kImageHeight = 351;

constexpr int kTileSize = 128;

// Divides and rounds up. The divisor must be positive.
int CeilDiv(int value, int divisor)
{
    return (value + divisor - 1) / divisor;
}

// A stand-in for the resolved depth AOV texture, for the tests that validate the builder
// arguments without rendering: the reduction is never run.
class FakeDepthTexture : public PXR_NS::HgiTexture
{
public:
    explicit FakeDepthTexture(PXR_NS::HgiTextureDesc const& desc) : HgiTexture(desc) {}

    size_t GetByteSizeOfResource() const override { return 0; }
    uint64_t GetRawResource() const override { return 0; }
    PXR_NS::HgiTextureUsage SubmitLayoutChange(PXR_NS::HgiTextureUsage) override { return 0; }
};

// Keeps the fake textures alive: an HgiTextureHandle does not own its texture.
std::vector<std::unique_ptr<FakeDepthTexture>>& FakeDepthTextures()
{
    static std::vector<std::unique_ptr<FakeDepthTexture>> textures;
    return textures;
}

HgiTextureHandle MakeFakeDepthTexture(GfVec2i const& size)
{
    HgiTextureDesc desc;
    desc.dimensions = GfVec3i(size[0], size[1], 1);
    desc.format     = HgiFormatFloat32;
    FakeDepthTextures().push_back(std::make_unique<FakeDepthTexture>(desc));
    FakeDepthTexture* texture = FakeDepthTextures().back().get();
    return HgiTextureHandle(texture, reinterpret_cast<uint64_t>(texture));
}

// A test context, a stage and a Storm frame pass rendering it.
struct SceneFixture
{
    std::shared_ptr<TestHelpers::TestContext> testContext;
    TestHelpers::TestStage stage;
    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // The depth-only frame pass of the DepthGuideBuilder sweep.
    hvt::FramePassPtr sweepFramePass;

    explicit SceneFixture(int contextSize) :
        testContext(TestHelpers::CreateTestContext(contextSize, contextSize)),
        stage(testContext->_backend)
    {
        // Creates the render index with the hgi driver of the test context.
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);
    }

    // Opens the test scene and creates the frame passes rendering it: the shaded one and the
    // depth-only one the DepthGuideBuilder sweeps with.
    void OpenScene()
    {
        if (!stage.open(testContext->_sceneFilepath))
        {
            return;
        }

        HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = SdfPath("/sceneFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);

        hvt::FramePassDescriptor sweepPassDesc;
        sweepPassDesc.renderIndex = renderIndex->RenderIndex();
        sweepPassDesc.uid         = SdfPath("/depthSweepFramePass");
        sweepFramePass            = hvt::ViewportEngine::CreateFramePass(
            sweepPassDesc, hvt::FramePass::PresetTaskLists::DepthOnly);
    }

    // True once the scene is open and the frame passes created.
    bool IsOpen() const { return static_cast<bool>(sceneFramePass); }

    // The asymmetric view of the how-tos: the default framing of the test scene is symmetric,
    // so a mirrored or misplaced tile would not change the image otherwise.
    GfMatrix4d ViewMatrix() const
    {
        const double shift = stage.computeStageBounds().GetSize().GetLength() * 0.1;
        return stage.viewMatrix() * GfMatrix4d().SetTranslate(GfVec3d(-1.5 * shift, shift, 0.0));
    }
};

// Sets the frame pass parameters shared by every render of the tests.
void SetRenderParams(hvt::FramePassPtr const& sceneFramePass, TestHelpers::TestContext& context,
    TestHelpers::TestStage& stage, GfMatrix4d const& viewMatrix, GfVec2i const& bufferSize,
    CameraUtilFraming const& framing)
{
    auto& params = sceneFramePass->params();

    params.renderBufferSize = bufferSize;
    params.visualizeAOV     = HdAovTokens->color;

    params.viewInfo.viewMatrix       = viewMatrix;
    params.viewInfo.projectionMatrix = stage.projectionMatrix();
    params.viewInfo.lights           = stage.defaultLights();
    params.viewInfo.material         = stage.defaultMaterial();
    params.viewInfo.ambient          = stage.defaultAmbient();
    params.viewInfo.framing          = framing;

    params.colorspace      = HdxColorCorrectionTokens->sRGB;
    params.backgroundColor = TestHelpers::ColorDarkGrey;
    params.selectionColor  = TestHelpers::ColorYellow;

    params.enablePresentation = context.presentationEnabled();
}

// Renders the tile of the given framing (see TileContext::framing), the render buffer being the
// whole tile, and waits for the GPU.
void renderTile(TestHelpers::TestContext& context, TestHelpers::TestStage& stage,
    hvt::FramePassPtr const& sceneFramePass, GfMatrix4d const& viewMatrix,
    CameraUtilFraming const& framing)
{
    SetRenderParams(sceneFramePass, context, stage, viewMatrix,
        GfVec2i(framing.dataWindow.GetWidth(), framing.dataWindow.GetHeight()), framing);

    sceneFramePass->Render();
    context._backend->waitForGPUIdle();
}

// The sweep callback of the tests: renders the tile through the test context, like the shaded
// tiles of the how-tos.
hvt::DepthGuideBuilder::RenderTileFn MakeRenderTileFn(TestHelpers::TestContext& context,
    TestHelpers::TestStage& stage, hvt::FramePassPtr const& sceneFramePass,
    GfMatrix4d const& viewMatrix)
{
    return [&context, &stage, &sceneFramePass, &viewMatrix](hvt::TileContext const& tile)
    {
        context.run(
            [&context, &stage, &sceneFramePass, &viewMatrix, &tile]()
            {
                renderTile(context, stage, sceneFramePass, viewMatrix, tile.framing);
                return false; // The tile is rendered, no more frame is needed.
            },
            sceneFramePass.get());
    };
}

// Reads a float texture back, in the row order of the backend's textures.
std::vector<float> readBackFloatTexture(Hgi* hgi, HgiTextureHandle const& texture)
{
    const HgiTextureDesc& desc = texture->GetDescriptor();
    std::vector<float> texels(static_cast<size_t>(desc.dimensions[0]) * desc.dimensions[1]);

    HgiTextureGpuToCpuOp readBackOp {};
    readBackOp.gpuSourceTexture          = texture;
    readBackOp.cpuDestinationBuffer      = texels.data();
    readBackOp.destinationBufferByteSize = texels.size() * sizeof(float);
    HgiBlitCmdsUniquePtr blitCmds        = hgi->CreateBlitCmds();
    blitCmds->CopyTextureGpuToCpu(readBackOp);
    hgi->SubmitCmds(blitCmds.get(), HgiSubmitWaitTypeWaitUntilCompleted);

    return texels;
}

// Min-reduces a full-image depth, in texture row order, into its guide, in texture row order:
// each guide texel keeps the depth of the closest surface of its block of image pixels, the
// pixels beyond the image (a partial last block) being skipped.
std::vector<float> minReduce(
    std::vector<float> const& imageDepth, GfVec2i const& imageSize, int scale)
{
    const int guideW = CeilDiv(imageSize[0], scale);
    const int guideH = CeilDiv(imageSize[1], scale);
    std::vector<float> guide(static_cast<size_t>(guideW) * guideH, 1.0f);
    for (int y = 0; y < imageSize[1]; ++y)
    {
        for (int x = 0; x < imageSize[0]; ++x)
        {
            float& texel = guide[static_cast<size_t>(y / scale) * guideW + x / scale];
            texel        = std::min(texel, imageDepth[static_cast<size_t>(y) * imageSize[0] + x]);
        }
    }
    return guide;
}

// Flips the rows of an 8-bit RGBA image, so the first row is the top of the image.
std::vector<uint8_t> flipRows(std::vector<uint8_t> const& image, int width, int height)
{
    std::vector<uint8_t> flipped(image.size());
    for (int row = 0; row < height; ++row)
    {
        std::memcpy(flipped.data() + static_cast<size_t>(row) * width * 4,
            image.data() + static_cast<size_t>(height - 1 - row) * width * 4,
            static_cast<size_t>(width) * 4);
    }
    return flipped;
}

// Renders the tiles of the iterator and composites their valid regions into a single image, the
// first row being the top (the composite math of howTo23).
std::vector<uint8_t> renderAndCompositeTiles(TestHelpers::TestContext& context,
    TestHelpers::TestStage& stage, hvt::FramePassPtr const& sceneFramePass,
    hvt::TileIterator& tileIterator, GfMatrix4d const& viewMatrix, int imageWidth, int imageHeight)
{
    const GfVec2i tileSize = tileIterator.GetTileSize();
    const int tileW        = tileSize[0];
    const int tileH        = tileSize[1];
    const int margin       = tileIterator.GetTileMargin();

    // The composited image: 8-bit RGBA pixels, the first row being the top of the image.
    std::vector<uint8_t> fullImage(static_cast<size_t>(imageWidth) * imageHeight * 4, 0);

    // Reads the tiles back in the row order of the backend.
    GpuImageCapture gpuCapture;
    gpuCapture.flipVertically(context._backend->imageCapture().flipVertically());

    hvt::TileContext tileContext;
    auto render = [&]()
    {
        renderTile(context, stage, sceneFramePass, viewMatrix, tileContext.framing);

        gpuCapture.capture(sceneFramePass.get(), context._backend->hgi(), tileW, tileH);
        const std::vector<uint8_t> tilePixels = gpuCapture.rgba8Data();

        // Extracts the valid region of the tile (i.e., the tile minus its margin band).
        const int validW = tileContext.maxCoordinate[0] - tileContext.minCoordinate[0];
        const int validH = tileContext.maxCoordinate[1] - tileContext.minCoordinate[1];

        for (int row = 0; row < validH; ++row)
        {
            const int tileRow = margin + row;
            const size_t srcRow =
                static_cast<size_t>(gpuCapture.flipVertically() ? tileH - 1 - tileRow : tileRow);
            const size_t dstRow = static_cast<size_t>(tileContext.minCoordinate[1] + row);

            const uint8_t* src = tilePixels.data() + (srcRow * tileW + margin) * 4;
            uint8_t* dst =
                fullImage.data() + (dstRow * imageWidth + tileContext.minCoordinate[0]) * 4;

            std::memcpy(dst, src, static_cast<size_t>(validW) * 4);
        }

        return false; // The tile is composited, no more frame is needed.
    };

    while (!tileIterator.IsDone())
    {
        tileContext = tileIterator.Next();
        context.run(render, sceneFramePass.get());
    }

    return fullImage;
}

} // namespace

// ===========================================================================
// Guide memory and scale selection
// ===========================================================================

TEST(TestDepthGuideBuilder, GetByteSize_IsTheGuideTexelCountTimesFourBytes)
{
    // A quarter of 15000x15000: ceil(15000/4)^2 texels, 4 bytes each (56 MB).
    EXPECT_EQ(hvt::DepthGuideBuilder::GetByteSize(GfVec2i(15000, 15000), 4),
        static_cast<size_t>(3750) * 3750 * 4);
    // The full-resolution guide of 6000x6000 is 144 MB.
    EXPECT_EQ(hvt::DepthGuideBuilder::GetByteSize(GfVec2i(6000, 6000), 1),
        static_cast<size_t>(6000) * 6000 * 4);
    // An odd image size rounds the guide up.
    EXPECT_EQ(hvt::DepthGuideBuilder::GetByteSize(GfVec2i(501, 351), 4),
        static_cast<size_t>(126) * 88 * 4);
    EXPECT_EQ(hvt::DepthGuideBuilder::GetByteSize(GfVec2i(0, 100), 4), 0u);
    EXPECT_EQ(hvt::DepthGuideBuilder::GetByteSize(GfVec2i(100, 100), 0), 0u);
}

TEST(TestDepthGuideBuilder, ChooseScale_PicksTheSmallestScaleThatFits)
{
    // The full-resolution guide of 6000x6000 is 144 MB.
    EXPECT_EQ(hvt::DepthGuideBuilder::ChooseScale(
                  GfVec2i(6000, 6000), static_cast<size_t>(144) * 1000 * 1000),
        1);
    // A half-resolution budget (36 MB) picks the half scale.
    EXPECT_EQ(hvt::DepthGuideBuilder::ChooseScale(
                  GfVec2i(6000, 6000), static_cast<size_t>(72) * 1000 * 1000),
        2);
    // A quarter-resolution budget (9 MB) picks the quarter scale.
    EXPECT_EQ(hvt::DepthGuideBuilder::ChooseScale(
                  GfVec2i(6000, 6000), static_cast<size_t>(9) * 1000 * 1000),
        4);
    // No scale fits a budget smaller than the quarter guide.
    EXPECT_EQ(hvt::DepthGuideBuilder::ChooseScale(GfVec2i(6000, 6000), 1u), 0);
    // A guide side above the maximum texture dimension is skipped, whatever the budget.
    EXPECT_EQ(hvt::DepthGuideBuilder::ChooseScale(
                  GfVec2i(20000, 100), static_cast<size_t>(400) * 1000 * 1000),
        2);
    EXPECT_EQ(hvt::DepthGuideBuilder::ChooseScale(GfVec2i(20000, 100), 1u), 0);
    EXPECT_EQ(hvt::DepthGuideBuilder::ChooseScale(GfVec2i(0, 100), 1000u), 0);
}

TEST(TestDepthGuideBuilder, GetSweepMargin_CoversTheBlockReachRoundedUpToEven)
{
    EXPECT_EQ(hvt::DepthGuideBuilder::GetSweepMargin(1), 0);
    EXPECT_EQ(hvt::DepthGuideBuilder::GetSweepMargin(2), 2);
    EXPECT_EQ(hvt::DepthGuideBuilder::GetSweepMargin(3), 2);
    EXPECT_EQ(hvt::DepthGuideBuilder::GetSweepMargin(4), 4);
    EXPECT_EQ(hvt::DepthGuideBuilder::GetSweepMargin(5), 4);
    EXPECT_EQ(hvt::DepthGuideBuilder::GetSweepMargin(0), 0);
    EXPECT_EQ(hvt::DepthGuideBuilder::GetSweepMargin(-4), 0);
}

// ===========================================================================
// The shared tile-framing helpers
// ===========================================================================

TEST(TestDepthGuideBuilder, ComputeImagePixelOffset_InvalidFramingAddressesTheWholeBuffer)
{
    GfVec2i offset, imageSize;
    hvt::ComputeImagePixelOffset(
        CameraUtilFraming(), GfVec2i(128, 64), /*isTopDown=*/false, &offset, &imageSize);
    EXPECT_EQ(offset, GfVec2i(0, 0));
    EXPECT_EQ(imageSize, GfVec2i(128, 64));
}

TEST(TestDepthGuideBuilder, ComputeImagePixelOffset_TopDownKeepsTheFramingOrigin)
{
    // The display window is the full image and the data window is the render buffer, so the
    // buffer origin is the negated display window minimum.
    const GfRange2f displayWindow(GfVec2f(-96.f, -40.f), GfVec2f(4.f, 60.f));
    const CameraUtilFraming framing(displayWindow, GfRect2i(GfVec2i(0, 0), 64, 64));

    GfVec2i offset, imageSize;
    hvt::ComputeImagePixelOffset(framing, GfVec2i(64, 64), /*isTopDown=*/true, &offset, &imageSize);
    EXPECT_EQ(offset, GfVec2i(96, 40));
    EXPECT_EQ(imageSize, GfVec2i(100, 100));
}

TEST(TestDepthGuideBuilder, ComputeImagePixelOffset_BottomUpFlipsTheYAxis)
{
    // On a backend whose textures store the bottom row first, the buffer origin is the bottom
    // row of the buffer: image row imageSize.y - bufferSize.y - originY in texture row order.
    // The tile of the framing below extends past the bottom of the image (a partial last tile
    // row), so the offset is negative.
    const GfRange2f displayWindow(GfVec2f(-96.f, -40.f), GfVec2f(4.f, 60.f));
    const CameraUtilFraming framing(displayWindow, GfRect2i(GfVec2i(0, 0), 64, 64));

    GfVec2i offset, imageSize;
    hvt::ComputeImagePixelOffset(
        framing, GfVec2i(64, 64), /*isTopDown=*/false, &offset, &imageSize);
    EXPECT_EQ(offset, GfVec2i(96, -4));
    EXPECT_EQ(imageSize, GfVec2i(100, 100));
}

HVT_TEST(TestDepthGuideBuilder, IsTopDownTextureRowOrder_MatchesTheBackend)
{
    auto context       = TestHelpers::CreateTestContext(64, 64);
    const bool isMetal = TestHelpers::GetRenderingBackend() == HgiTokens->Metal;
    EXPECT_EQ(hvt::IsTopDownTextureRowOrder(context->_backend->hgi()), isMetal);
    EXPECT_FALSE(hvt::IsTopDownTextureRowOrder(nullptr));
}

// ===========================================================================
// Sweep ownership (no rendering)
// ===========================================================================

TEST(TestDepthGuideBuilder, SweepOwnership_EveryGuideTexelIsOwnedExactlyOnce)
{
    struct Case
    {
        GfVec2i imageSize;
        int tileSize;
        int scale;
    };
    const Case cases[] = {
        { GfVec2i(501, 351), 128, 1 },
        { GfVec2i(501, 351), 128, 2 },
        { GfVec2i(501, 351), 128, 4 },
        { GfVec2i(4097, 803), 256, 4 },
        { GfVec2i(64, 64), 64, 4 }, // One tile per axis, with a margin.
        { GfVec2i(1000, 1000), 1000, 2 },
    };

    for (Case const& testCase : cases)
    {
        const int margin = hvt::DepthGuideBuilder::GetSweepMargin(testCase.scale);
        const int guideW = CeilDiv(testCase.imageSize[0], testCase.scale);
        const int guideH = CeilDiv(testCase.imageSize[1], testCase.scale);

        // The ownership is verified in both row orders of the backends.
        for (const bool isTopDown : { false, true })
        {
            hvt::TileIterator sweep;
            ASSERT_TRUE(sweep.Initialize(
                testCase.imageSize, margin, GfVec2i(testCase.tileSize, testCase.tileSize)));

            std::vector<int> ownerCount(static_cast<size_t>(guideW) * guideH, 0);

            for (unsigned int row = 0; row < sweep.GetNumberOfRows(); ++row)
            {
                for (unsigned int col = 0; col < sweep.GetNumberOfColumns(); ++col)
                {
                    const hvt::TileContext tile = sweep.GetTile(row, col);

                    // The valid region of the tile, in texture row order: the y-down convention
                    // of the tile coordinates flips on a bottom-up backend.
                    const int validMinY = isTopDown ? tile.minCoordinate[1]
                                                    : testCase.imageSize[1] - tile.maxCoordinate[1];
                    const int validMaxY = isTopDown ? tile.maxCoordinate[1]
                                                    : testCase.imageSize[1] - tile.minCoordinate[1];

                    // The guide texels the tile owns: those whose block of image pixels starts
                    // in the valid region, i.e. [ceil(v0/s), ceil(v1/s)) per axis.
                    const GfVec2i guideOrigin(CeilDiv(tile.minCoordinate[0], testCase.scale),
                        CeilDiv(validMinY, testCase.scale));
                    const GfVec2i guideEnd(CeilDiv(tile.maxCoordinate[0], testCase.scale),
                        CeilDiv(validMaxY, testCase.scale));

                    // The buffer origin, in texture row order, from the framing of the tile.
                    GfVec2i pixelOffset, imageSize;
                    hvt::ComputeImagePixelOffset(tile.framing,
                        GfVec2i(testCase.tileSize, testCase.tileSize), isTopDown, &pixelOffset,
                        &imageSize);
                    ASSERT_EQ(imageSize, testCase.imageSize);

                    for (int gy = guideOrigin[1]; gy < guideEnd[1]; ++gy)
                    {
                        for (int gx = guideOrigin[0]; gx < guideEnd[0]; ++gx)
                        {
                            ++ownerCount[static_cast<size_t>(gy) * guideW + gx];

                            // Every pixel of the block lies inside the tile buffer: the sweep
                            // margin covers the reach of the block past the valid region.
                            for (int j = 0; j < testCase.scale; ++j)
                            {
                                for (int i = 0; i < testCase.scale; ++i)
                                {
                                    const GfVec2i imagePixel(
                                        gx * testCase.scale + i, gy * testCase.scale + j);
                                    if (imagePixel[0] >= imageSize[0] ||
                                        imagePixel[1] >= imageSize[1])
                                    {
                                        continue; // A partial last block.
                                    }

                                    const GfVec2i bufferPixel = imagePixel - pixelOffset;
                                    ASSERT_GE(bufferPixel[0], 0);
                                    ASSERT_LT(bufferPixel[0], testCase.tileSize);
                                    ASSERT_GE(bufferPixel[1], 0);
                                    ASSERT_LT(bufferPixel[1], testCase.tileSize);
                                }
                            }
                        }
                    }
                }
            }

            // Every guide texel is owned exactly once.
            for (size_t i = 0; i < ownerCount.size(); ++i)
            {
                ASSERT_EQ(ownerCount[i], 1) << "guide texel " << i << " of " << ownerCount.size();
            }
        }
    }
}

// ===========================================================================
// Argument validation
// ===========================================================================

HVT_TEST(TestDepthGuideBuilder, AccumulateTile_RejectsAMarginSmallerThanTheScaleNeeds)
{
    auto context = TestHelpers::CreateTestContext(64, 64);

    hvt::DepthGuideBuilder builder(context->_backend->hgi());
    ASSERT_TRUE(builder.Begin(GfVec2i(200, 100), /*scale=*/4));

    // A tile whose framing implies a margin of 1, smaller than the 3 a scale of 4 needs: a
    // pixel block would read outside the tile buffer.
    hvt::TileContext tile;
    tile.minCoordinate = GfVec2i(0, 0);
    tile.maxCoordinate = GfVec2i(62, 62);
    const GfRange2f displayWindow(GfVec2f(1.f, 1.f), GfVec2f(201.f, 101.f));
    tile.framing = CameraUtilFraming(displayWindow, GfRect2i(GfVec2i(0, 0), 64, 64));

    TfErrorMark mark;
    builder.AccumulateTile(tile, MakeFakeDepthTexture(GfVec2i(64, 64)));
    EXPECT_FALSE(mark.IsClean());
    mark.Clear();

    // The tile was rejected, so the guide stays incomplete.
    EXPECT_FALSE(builder.IsComplete());
}

// ===========================================================================
// The reduction, against the CPU reference and the untiled depth
// ===========================================================================

HVT_TEST(TestDepthGuideBuilder, Build_TheGpuAndCpuReductionsAgree)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    const GfMatrix4d viewMatrix = f.ViewMatrix();
    const auto renderTileFn =
        MakeRenderTileFn(*f.testContext, f.stage, f.sweepFramePass, viewMatrix);

    for (const int scale : { 1, 2, 4 })
    {
        // The GPU reduction and the CPU reference produce the same guide, bit for bit: a min
        // over the same depth values is exact.
        hvt::DepthGuideBuilder gpuBuilder(f.testContext->_backend->hgi());
        hvt::DepthGuidePtr const gpuDepthGuide = gpuBuilder.Build(*f.sweepFramePass,
            GfVec2i(kImageWidth, kImageHeight), scale, GfVec2i(kTileSize, kTileSize), renderTileFn);
        ASSERT_TRUE(gpuDepthGuide);
        EXPECT_EQ(gpuDepthGuide->GetScale(), scale);
        EXPECT_EQ(gpuDepthGuide->GetImageSize(), GfVec2i(kImageWidth, kImageHeight));

        hvt::DepthGuideBuilder cpuBuilder(
            f.testContext->_backend->hgi(), hvt::DepthGuideBuilder::Reduction::Cpu);
        hvt::DepthGuidePtr const cpuDepthGuide = cpuBuilder.Build(*f.sweepFramePass,
            GfVec2i(kImageWidth, kImageHeight), scale, GfVec2i(kTileSize, kTileSize), renderTileFn);
        ASSERT_TRUE(cpuDepthGuide);

        const std::vector<float> gpuGuide =
            readBackFloatTexture(f.testContext->_backend->hgi(), gpuDepthGuide->GetTexture());
        const std::vector<float> cpuGuide =
            readBackFloatTexture(f.testContext->_backend->hgi(), cpuDepthGuide->GetTexture());

        ASSERT_EQ(gpuGuide.size(), cpuGuide.size());
        EXPECT_EQ(gpuGuide, cpuGuide) << "scale " << scale;
    }
}

HVT_TEST(TestDepthGuideBuilder, Build_MatchesAMinReduceOfTheUntiledDepth)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    auto& context               = *f.testContext;
    const GfMatrix4d viewMatrix = f.ViewMatrix();

    // Renders the full image in a single buffer and reads its depth AOV back, in the row order
    // of the backend's textures.
    SetRenderParams(f.sceneFramePass, context, f.stage, viewMatrix,
        GfVec2i(kImageWidth, kImageHeight),
        CameraUtilFraming(GfRange2f(GfVec2f(0.f, 0.f), GfVec2f(kImageWidth, kImageHeight)),
            GfRect2i(GfVec2i(0, 0), kImageWidth, kImageHeight)));
    context.run(
        [&f]()
        {
            f.sceneFramePass->Render();
            return false; // The image is rendered, no more frame is needed.
        },
        f.sceneFramePass.get());
    context._backend->waitForGPUIdle();

    const std::vector<float> untiledDepth = readBackFloatTexture(
        context._backend->hgi(), f.sceneFramePass->GetRenderTexture(HdAovTokens->depth));

    const auto renderTileFn = MakeRenderTileFn(context, f.stage, f.sweepFramePass, viewMatrix);

    for (const int scale : { 1, 2, 4 })
    {
        hvt::DepthGuideBuilder builder(context._backend->hgi());
        hvt::DepthGuidePtr const depthGuide = builder.Build(*f.sweepFramePass,
            GfVec2i(kImageWidth, kImageHeight), scale, GfVec2i(kTileSize, kTileSize), renderTileFn);
        ASSERT_TRUE(depthGuide);

        const std::vector<float> guide =
            readBackFloatTexture(context._backend->hgi(), depthGuide->GetTexture());
        const std::vector<float> expected =
            minReduce(untiledDepth, GfVec2i(kImageWidth, kImageHeight), scale);

        ASSERT_EQ(guide.size(), expected.size());

        // The swept guide equals a min-reduce of the untiled depth, up to one quantization step
        // of the depth buffer: the sweep tiles and the untiled render rasterize through
        // different projection matrices (the framing scales the projection to the buffer), so
        // the interpolated depth of a handful of pixels falls on the other side of a 24-bit
        // quantization boundary. A min is exact, so any larger difference would be a real
        // defect e.g. a misplaced tile or a wrong block.
        float maxDiff    = 0.0f;
        size_t diffCount = 0;
        for (size_t i = 0; i < guide.size(); ++i)
        {
            const float diff = std::fabs(guide[i] - expected[i]);
            if (diff > 0.0f)
            {
                ++diffCount;
                maxDiff = std::max(maxDiff, diff);
            }
        }
        // One step of a [0, 1] depth quantized to 24 bits.
        constexpr float kDepthQuantum = 1.0f / (1 << 24);
        EXPECT_LE(maxDiff, kDepthQuantum)
            << "scale " << scale << ": " << diffCount << " of " << guide.size()
            << " guide texels differ from the untiled min-reduce";
    }
}

// ===========================================================================
// Task handling
// ===========================================================================

HVT_TEST(TestDepthGuideBuilder, DepthOnlyPass_ContainsOnlyTheDepthTasks)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    EXPECT_EQ(f.sweepFramePass->GetPresetTaskLists(), hvt::FramePass::PresetTaskLists::DepthOnly);
    EXPECT_EQ(f.sceneFramePass->GetPresetTaskLists(), hvt::FramePass::PresetTaskLists::Default);

    // Only render tasks, the AOV input task and the present task: no lighting, shadow,
    // selection, color correction or effect task.
    hvt::TaskManagerPtr const& sweepTaskManager = f.sweepFramePass->GetTaskManager();
    SdfPathVector taskPaths;
    sweepTaskManager->GetTaskPaths(
        hvt::TaskFlagsBits::kAllTaskBits, /*ignoreDisabled=*/false, taskPaths);
    ASSERT_FALSE(taskPaths.empty());

    size_t renderTaskCount = 0;
    for (SdfPath const& taskPath : taskPaths)
    {
        HdTaskSharedPtr const task = sweepTaskManager->GetTask(taskPath);
        const bool isRenderTask    = dynamic_cast<HdxRenderTask const*>(task.get()) != nullptr;
        renderTaskCount += isRenderTask ? 1 : 0;
        EXPECT_TRUE(isRenderTask || dynamic_cast<hvt::AovInputTask const*>(task.get()) ||
            dynamic_cast<HdxPresentTask const*>(task.get()))
            << "Unexpected task " << taskPath << " in the depth-only pass.";
    }
    EXPECT_GT(renderTaskCount, 0u);
}

HVT_TEST(TestDepthGuideBuilder, DepthOnlyPass_RendersTheDepthAovAndAuthorsNoLight)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    const GfMatrix4d viewMatrix = f.ViewMatrix();
    const CameraUtilFraming framing(GfRange2f(GfVec2f(0.f, 0.f), GfVec2f(kTileSize, kTileSize)),
        GfRect2i(GfVec2i(0, 0), kTileSize, kTileSize));

    // Both passes are given the default lights of the test stage.
    f.sweepFramePass->params().renderOutputs = { HdAovTokens->depth };
    f.testContext->run(
        [&f, &viewMatrix, &framing]()
        {
            renderTile(*f.testContext, f.stage, f.sceneFramePass, viewMatrix, framing);
            renderTile(*f.testContext, f.stage, f.sweepFramePass, viewMatrix, framing);
            return false; // Both passes are rendered, no more frame is needed.
        },
        f.sweepFramePass.get());

    // The depth-only pass publishes its depth texture without a color AOV.
    EXPECT_TRUE(f.sweepFramePass->GetRenderTexture(HdAovTokens->depth));
    EXPECT_EQ(f.sweepFramePass->GetRenderBuffer(HdAovTokens->color), nullptr);

    // The lights of a pass are authored under its identifier in the shared render index, where
    // the lighting task of every pass would sync them.
    HdRenderIndex* renderIndex = f.renderIndex->RenderIndex();
    auto countLights           = [renderIndex](SdfPath const& root)
    {
        size_t count = 0;
        for (TfToken const& type :
            { HdPrimTypeTokens->simpleLight, HdPrimTypeTokens->distantLight })
        {
            if (renderIndex->IsSprimTypeSupported(type))
            {
                count += renderIndex->GetSprimSubtree(type, root).size();
            }
        }
        return count;
    };
    EXPECT_GT(countLights(SdfPath("/sceneFramePass")), 0u); // The count does see lights.
    EXPECT_EQ(countLights(SdfPath("/depthSweepFramePass")), 0u);
}

HVT_TEST(TestDepthGuideBuilder, Build_RejectsAPassThatIsNotDepthOnly)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    // Build overwrites the render outputs of its pass: a shaded pass would stop rendering color.
    const TfTokenVector shadedRenderOutputs = f.sceneFramePass->params().renderOutputs;
    bool rendered                           = false;

    hvt::DepthGuideBuilder builder(f.testContext->_backend->hgi());
    TfErrorMark mark;
    EXPECT_FALSE(builder.Build(*f.sceneFramePass, GfVec2i(kImageWidth, kImageHeight),
        /*scale=*/4, GfVec2i(kTileSize, kTileSize),
        [&rendered](hvt::TileContext const&) { rendered = true; }));
    EXPECT_FALSE(mark.IsClean());
    mark.Clear();

    EXPECT_FALSE(rendered);
    EXPECT_EQ(f.sceneFramePass->params().renderOutputs, shadedRenderOutputs);
    EXPECT_FALSE(builder.GetGuide());
}

HVT_TEST(TestDepthGuideBuilder, Build_RejectsACallbackThatChangesTheTileFraming)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    // The callback renders every tile with the framing of the whole image, instead of the tile
    // framing the builder set.
    const GfMatrix4d viewMatrix = f.ViewMatrix();
    const CameraUtilFraming wrongFraming(
        GfRange2f(GfVec2f(0.f, 0.f), GfVec2f(kImageWidth, kImageHeight)),
        GfRect2i(GfVec2i(0, 0), kTileSize, kTileSize));
    int renderedTiles = 0;
    const hvt::DepthGuideBuilder::RenderTileFn wrongFramingFn =
        [&f, &viewMatrix, &wrongFraming, &renderedTiles](hvt::TileContext const&)
    {
        ++renderedTiles;
        f.testContext->run(
            [&f, &viewMatrix, &wrongFraming]()
            {
                renderTile(*f.testContext, f.stage, f.sweepFramePass, viewMatrix, wrongFraming);
                return false; // The tile is rendered, no more frame is needed.
            },
            f.sweepFramePass.get());
    };

    hvt::DepthGuideBuilder builder(f.testContext->_backend->hgi());
    TfErrorMark mark;
    EXPECT_FALSE(builder.Build(*f.sweepFramePass, GfVec2i(kImageWidth, kImageHeight),
        /*scale=*/4, GfVec2i(kTileSize, kTileSize), wrongFramingFn));
    EXPECT_FALSE(mark.IsClean());
    mark.Clear();

    // The sweep stopped at the first tile and left no guide.
    EXPECT_EQ(renderedTiles, 1);
    EXPECT_FALSE(builder.GetGuide());
}

HVT_TEST(TestDepthGuideBuilder, Build_SetsTheTileFramingBeforeTheCallback)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    // The callback only renders: the builder placed the tile on the frame pass.
    const GfMatrix4d viewMatrix                               = f.ViewMatrix();
    int renderedTiles                                         = 0;
    const hvt::DepthGuideBuilder::RenderTileFn checkFramingFn = [&f, &viewMatrix, &renderedTiles](
                                                                    hvt::TileContext const& tile)
    {
        ++renderedTiles;
        auto const& params = f.sweepFramePass->params();
        EXPECT_EQ(params.viewInfo.framing, tile.framing);
        EXPECT_EQ(params.renderBufferSize,
            GfVec2i(tile.framing.dataWindow.GetWidth(), tile.framing.dataWindow.GetHeight()));
        f.testContext->run(
            [&f, &viewMatrix, &tile]()
            {
                renderTile(*f.testContext, f.stage, f.sweepFramePass, viewMatrix, tile.framing);
                return false; // The tile is rendered, no more frame is needed.
            },
            f.sweepFramePass.get());
    };

    hvt::DepthGuideBuilder builder(f.testContext->_backend->hgi());
    EXPECT_TRUE(builder.Build(*f.sweepFramePass, GfVec2i(kImageWidth, kImageHeight),
        /*scale=*/4, GfVec2i(kTileSize, kTileSize), checkFramingFn));
    EXPECT_GT(renderedTiles, 1);
}

HVT_TEST(TestDepthGuideBuilder, GetGuide_IsNullUntilTheGuideIsComplete)
{
    auto context = TestHelpers::CreateTestContext(64, 64);

    // Begun, but no tile accumulated: an incomplete guide is never handed out.
    hvt::DepthGuideBuilder builder(
        context->_backend->hgi(), hvt::DepthGuideBuilder::Reduction::Cpu);
    ASSERT_TRUE(builder.Begin(GfVec2i(kImageWidth, kImageHeight), /*scale=*/4));
    EXPECT_FALSE(builder.IsComplete());
    EXPECT_FALSE(builder.GetGuide());
}

HVT_TEST(TestDepthGuideBuilder, SetSSAODepthGuide_StoresTheGuideInEverySSAOTaskAndNullClearsIt)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    hvt::TaskManagerPtr const& taskManager = f.sceneFramePass->GetTaskManager();

    // Two SSAO tasks with different effect parameters.
    hvt::SSAOTaskParams paramsA;
    paramsA.ao.isEnabled = true;
    paramsA.ao.amount    = 1.5f;
    const SdfPath ssaoTaskA =
        taskManager->AddTask<hvt::SSAOTask>(hvt::SSAOTask::GetToken(), paramsA, nullptr);

    hvt::SSAOTaskParams paramsB;
    paramsB.ao.isEnabled = true;
    paramsB.ao.amount    = 2.5f;
    const SdfPath ssaoTaskB =
        taskManager->AddTask<hvt::SSAOTask>(TfToken("ssaoTaskB"), paramsB, nullptr);

    const GfMatrix4d viewMatrix = f.ViewMatrix();
    hvt::DepthGuideBuilder builder(f.testContext->_backend->hgi());
    hvt::DepthGuidePtr const depthGuide = builder.Build(*f.sweepFramePass, GfVec2i(200, 150),
        /*scale=*/4, GfVec2i(kTileSize, kTileSize),
        MakeRenderTileFn(*f.testContext, f.stage, f.sweepFramePass, viewMatrix));
    ASSERT_TRUE(depthGuide);

    EXPECT_EQ(hvt::SetSSAODepthGuide(*taskManager, depthGuide), 2u);
    for (SdfPath const& taskPath : { ssaoTaskA, ssaoTaskB })
    {
        const hvt::SSAOTaskParams params =
            taskManager->GetTaskValue(taskPath, HdTokens->params).Get<hvt::SSAOTaskParams>();
        EXPECT_EQ(params.depthGuide, depthGuide);
    }
    EXPECT_EQ(
        taskManager->GetTaskValue(ssaoTaskA, HdTokens->params).Get<hvt::SSAOTaskParams>().ao.amount,
        1.5f);
    EXPECT_EQ(
        taskManager->GetTaskValue(ssaoTaskB, HdTokens->params).Get<hvt::SSAOTaskParams>().ao.amount,
        2.5f);

    EXPECT_EQ(hvt::SetSSAODepthGuide(*taskManager, nullptr), 2u);
    for (SdfPath const& taskPath : { ssaoTaskA, ssaoTaskB })
    {
        const hvt::SSAOTaskParams params =
            taskManager->GetTaskValue(taskPath, HdTokens->params).Get<hvt::SSAOTaskParams>();
        EXPECT_FALSE(params.depthGuide);
    }
}

HVT_TEST(TestDepthGuideBuilder, BuiltGuide_OutlivesARebuildAndTheBuilder)
{
    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    Hgi* hgi                    = f.testContext->_backend->hgi();
    const GfMatrix4d viewMatrix = f.ViewMatrix();
    const auto renderTileFn =
        MakeRenderTileFn(*f.testContext, f.stage, f.sweepFramePass, viewMatrix);

    hvt::DepthGuidePtr firstGuide;
    std::vector<float> firstTexels;
    {
        hvt::DepthGuideBuilder builder(hgi);
        firstGuide = builder.Build(*f.sweepFramePass, GfVec2i(kImageWidth, kImageHeight),
            /*scale=*/4, GfVec2i(kTileSize, kTileSize), renderTileFn);
        ASSERT_TRUE(firstGuide);
        firstTexels = readBackFloatTexture(hgi, firstGuide->GetTexture());

        // A rebuild hands out a new guide, and leaves the first one untouched.
        hvt::DepthGuidePtr const secondGuide =
            builder.Build(*f.sweepFramePass, GfVec2i(kImageWidth, kImageHeight), /*scale=*/2,
                GfVec2i(kTileSize, kTileSize), renderTileFn);
        ASSERT_TRUE(secondGuide);
        EXPECT_NE(secondGuide, firstGuide);
        EXPECT_NE(secondGuide->GetTexture(), firstGuide->GetTexture());
    }

    // The builder is gone, the first guide is still alive and holds the same depth.
    ASSERT_TRUE(firstGuide->GetTexture());
    EXPECT_EQ(firstGuide->GetScale(), 4);
    EXPECT_EQ(readBackFloatTexture(hgi, firstGuide->GetTexture()), firstTexels);
}

// ===========================================================================
// End to end: a tiled render with the guide matches the untiled one
// ===========================================================================

HVT_TEST(TestDepthGuideBuilder, TiledWithGuide_MatchesTheUntiledRender)
{
    // The SSAO parameters: a strong effect, so a guide error shows in the comparison.
    static constexpr float kSampleRadius = 10.f;
    static constexpr float kAmount       = 2.0f;

    SceneFixture f(kTileSize);
    f.OpenScene();
    ASSERT_TRUE(f.IsOpen());

    auto& context               = *f.testContext;
    const GfMatrix4d viewMatrix = f.ViewMatrix();

    hvt::TaskManagerPtr const& taskManager = f.sceneFramePass->GetTaskManager();

    // Adds the SSAO task, as the how-to does: the commit function only sets the view, so the
    // guide stored by SetSSAODepthGuide is kept.
    {
        auto fnCommit = [&sceneFramePass = f.sceneFramePass](
                            hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const VtValue value        = fnGetValue(HdTokens->params);
            hvt::SSAOTaskParams params = value.Get<hvt::SSAOTaskParams>();

            auto renderParams                = sceneFramePass->params().renderParams;
            params.view.cameraID             = renderParams.camera;
            params.view.framing              = renderParams.framing;
            params.view.overrideWindowPolicy = renderParams.overrideWindowPolicy;

            fnSetValue(HdTokens->params, VtValue(params));
        };

        const SdfPath colorCorrectionTask =
            taskManager->GetTaskPath(HdxPrimitiveTokens->colorCorrectionTask);

        hvt::SSAOTaskParams initialParams;
        initialParams.ao.isEnabled    = true;
        initialParams.ao.amount       = kAmount;
        initialParams.ao.sampleRadius = kSampleRadius;

        taskManager->AddTask<hvt::SSAOTask>(hvt::SSAOTask::GetToken(), initialParams, fnCommit,
            colorCorrectionTask, hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // The untiled reference: the whole image in a single buffer.
    std::vector<uint8_t> untiledImage;
    {
        SetRenderParams(f.sceneFramePass, context, f.stage, viewMatrix,
            GfVec2i(kImageWidth, kImageHeight),
            CameraUtilFraming(GfRange2f(GfVec2f(0.f, 0.f), GfVec2f(kImageWidth, kImageHeight)),
                GfRect2i(GfVec2i(0, 0), kImageWidth, kImageHeight)));
        context.run(
            [&f]()
            {
                f.sceneFramePass->Render();
                return false; // The image is rendered, no more frame is needed.
            },
            f.sceneFramePass.get());
        context._backend->waitForGPUIdle();

        GpuImageCapture gpuCapture;
        gpuCapture.flipVertically(context._backend->imageCapture().flipVertically());
        gpuCapture.capture(
            f.sceneFramePass.get(), context._backend->hgi(), kImageWidth, kImageHeight);
        untiledImage = gpuCapture.rgba8Data();
        if (gpuCapture.flipVertically())
        {
            untiledImage = flipRows(untiledImage, kImageWidth, kImageHeight);
        }
    }

    for (const int scale : { 1, 4 })
    {
        // With the guide stored in the task parameters, the tile margin only covers the
        // denoising blur.
        hvt::DepthGuideBuilder builder(context._backend->hgi());
        hvt::DepthGuidePtr const depthGuide = builder.Build(*f.sweepFramePass,
            GfVec2i(kImageWidth, kImageHeight), scale, GfVec2i(kTileSize, kTileSize),
            MakeRenderTileFn(context, f.stage, f.sweepFramePass, viewMatrix));
        ASSERT_TRUE(depthGuide);
        hvt::SetSSAODepthGuide(*taskManager, depthGuide);

        hvt::ScreenSpaceKernelEffect::MarginContext viewContext;
        viewContext.imageResolution  = GfVec2i(kImageWidth, kImageHeight);
        viewContext.projectionMatrix = f.stage.projectionMatrix();
        viewContext.viewMatrix       = viewMatrix;

        hvt::TileIterator tileIterator;
        ASSERT_TRUE(tileIterator.Initialize(GfVec2i(kImageWidth, kImageHeight), *taskManager,
            GfVec2i(kTileSize, kTileSize), viewContext));
        ASSERT_EQ(tileIterator.GetTileMargin(), 8);

        const std::vector<uint8_t> tiledImage = renderAndCompositeTiles(context, f.stage,
            f.sceneFramePass, tileIterator, viewMatrix, kImageWidth, kImageHeight);

        ASSERT_EQ(tiledImage.size(), untiledImage.size());

        // The tiled render matches the untiled one up to the rasterization noise of the
        // framing-scaled projection: a tile buffer and the single buffer round the projection
        // differently, so at a handful of silhouette pixels the depth flips by one
        // quantization step, which the SSAO normal reconstruction amplifies into a small
        // color difference (measured: a mean difference of ~0.05 and ~0.1% of pixels above
        // 8/255, at both scales). A real defect e.g. a seam, a misplaced tile or a wrong
        // guide shows as diff bands along the tile borders, orders of magnitude above these
        // bounds.
        double diffSum        = 0.0;
        size_t diffPixelCount = 0;
        for (size_t i = 0; i < tiledImage.size(); i += 4)
        {
            int maxDiff = 0;
            for (int channel = 0; channel < 4; ++channel)
            {
                maxDiff = std::max(maxDiff,
                    std::abs(static_cast<int>(tiledImage[i + channel]) -
                        static_cast<int>(untiledImage[i + channel])));
            }
            diffSum += maxDiff;
            if (maxDiff > 8)
            {
                ++diffPixelCount;
            }
        }
        const size_t pixelCount = tiledImage.size() / 4;
        const double meanDiff   = diffSum / static_cast<double>(pixelCount);
        const double diffFraction =
            static_cast<double>(diffPixelCount) / static_cast<double>(pixelCount);
        EXPECT_LE(meanDiff, 0.25) << "scale " << scale;
        EXPECT_LE(diffFraction, 0.005) << "scale " << scale << ": " << diffPixelCount << " of "
                                       << pixelCount << " pixels differ by more than 8/255";
    }
}
