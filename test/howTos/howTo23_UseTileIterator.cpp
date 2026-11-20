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

// glew.h has to be included first, through TestContextCreator.h.
#include <RenderingFramework/TestContextCreator.h>

#include <RenderingFramework/ImageUtils.h>
#include <RenderingFramework/GpuImageCapture.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/tileIterator.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/blurTask.h>
#include <hvt/tasks/ssaoTask.h>

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace TestHelpers;
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
//
// The parameters of the renderAndCompositeTiles method below.
//
struct RenderAndCompositeTilesParams
{
    TestHelpers::TestContext& context;
    TestHelpers::TestStage& stage;
    hvt::FramePassPtr const& sceneFramePass;
    hvt::TileIterator& tileIterator;
    GfMatrix4d viewMatrix;

    int imageWidth  = 0;
    int imageHeight = 0;

    std::string computedImageName;
    std::string imageFile;
    bool validateAgainstBaseline = false;
};

//
// Common implementation for the tiled-rendering how-tos.
//
// Renders the scene tile by tile with the given (initialized) TileIterator and composites the
// valid region of each tile into a single full-resolution image, which is written as a PNG file.
// When inParams.validateAgainstBaseline is set, the composited image is compared against the
// baseline one.
//
// Note: TestContext::validateImages cannot be used here because it saves and compares the
// backend's current frame i.e., the last rendered tile, instead of the composited image.
//
void renderAndCompositeTiles(RenderAndCompositeTilesParams const& inParams)
{
    // Unpacks the parameters i.e., to keep the implementation below readable.
    auto& context                      = inParams.context;
    auto& stage                        = inParams.stage;
    auto& sceneFramePass               = inParams.sceneFramePass;
    auto& tileIterator                 = inParams.tileIterator;
    const auto& viewMatrix             = inParams.viewMatrix;
    const int imageWidth               = inParams.imageWidth;
    const int imageHeight              = inParams.imageHeight;
    const auto& computedImageName      = inParams.computedImageName;
    const auto& imageFile              = inParams.imageFile;
    const bool validateAgainstBaseline = inParams.validateAgainstBaseline;

    const GfVec2i tileSize = tileIterator.GetTileSize();
    const int tileW        = tileSize[0];
    const int tileH        = tileSize[1];
    const int margin       = tileIterator.GetTileMargin();

    // The data window covers the whole tile render buffer.
    const GfRect2i dataWindow(GfVec2i(0, 0), tileW, tileH);

    // The composited image: 8-bit RGBA pixels, the first row being the top of the image.
    std::vector<uint8_t> fullImage(static_cast<size_t>(imageWidth) * imageHeight * 4, 0);

    // Reads the tiles back in the row order of the backend.
    GpuImageCapture gpuCapture;
    gpuCapture.flipVertically(context._backend->imageCapture().flipVertically());

    hvt::TileContext tileContext;

    // Renders the current tile and composites it into the full image.
    auto render = [&]()
    {
        auto& params = sceneFramePass->params();

        params.renderBufferSize = tileSize;
        params.visualizeAOV     = HdAovTokens->color;

        params.viewInfo.viewMatrix       = viewMatrix;
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = stage.defaultMaterial();
        params.viewInfo.ambient          = stage.defaultAmbient();
        params.viewInfo.framing          = CameraUtilFraming(tileContext.displayWindow, dataWindow);

        params.colorspace      = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = TestHelpers::ColorDarkGrey;
        params.selectionColor  = TestHelpers::ColorYellow;

        params.enablePresentation = context.presentationEnabled();

        sceneFramePass->Render();
        context._backend->waitForGPUIdle();

        // Captures the tile: the color texture has the tile size (see renderBufferSize).
        gpuCapture.capture(sceneFramePass.get(), context._backend->hgi());
        const std::vector<uint8_t> tilePixels = gpuCapture.rgba8Data();

        // Composites the valid region of the tile into the full image. The tile covers the
        // full-image rectangle [minCoordinate, maxCoordinate); the margin band around it
        // only exists to keep post-processing effects stable at the tile borders, so it is
        // not copied.
        const int validW = tileContext.maxCoordinate[0] - tileContext.minCoordinate[0];
        const int validH = tileContext.maxCoordinate[1] - tileContext.minCoordinate[1];

        for (int row = 0; row < validH; ++row)
        {
            // The tile row in the y-down convention of the framing. A backend whose read-back
            // starts with the bottom row (i.e., the capture flips the image when writing it)
            // stores it mirrored.
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

    // Writes the composited image as a single PNG file.
    static const std::filesystem::path filePath = getOutputDataFolder();
    const std::filesystem::path screenShotPath =
        context._backend->getFilename(filePath, computedImageName + "_computed");

    ASSERT_TRUE(RenderingUtils::writeImage(
        screenShotPath, imageWidth, imageHeight, fullImage, /*flipVertically=*/false));

    // Validates the composited image against the baseline one.
    if (validateAgainstBaseline)
    {
        ASSERT_TRUE(context._backend->compareImage(computedImageName, imageFile));
    }
}

//
// Creates a Storm frame pass rendering the stage.
//
void createFramePass(TestHelpers::TestContext& context, TestHelpers::TestStage& stage,
    hvt::RenderIndexProxyPtr& renderIndex, hvt::FramePassPtr& sceneFramePass)
{
    // Creates the render index by providing the hgi driver and the requested renderer name.
    hvt::RendererDescriptor renderDesc;
    renderDesc.hgiDriver    = &context._backend->hgiDriver();
    renderDesc.rendererName = "HdStormRendererPlugin";
    hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

    // Creates the scene index containing the model.
    HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
    renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());

    // Creates the frame pass instance.
    hvt::FramePassDescriptor passDesc;
    passDesc.renderIndex = renderIndex->RenderIndex();
    passDesc.uid         = SdfPath("/sceneFramePass");
    sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);
}

/// The tile size and margin computed by ComputeSSAOTileLayout.
struct TileLayout
{
    PXR_NS::GfVec2i tileSize { 0, 0 };
    int margin = 0;
};

/// Computes a tile size and margin wide enough for the SSAO task to render without seams at
/// the tile borders, honoring the given tile size limits.
///
/// The margin covers the reach of the SSAO raw pass (see ssao.glslfx), plus 8 pixels for its
/// blur passes. The reach is computed at the near plane, which is the worst case for a
/// perspective camera. The margin does not account for any other screen-space effect.
///
TileLayout ComputeSSAOTileLayout(float sampleRadius, GfMatrix4d const& exportProj,
    GfMatrix4d const& viewMatrix, int imageHeight, int tileSize, int maxTileSize)
{
    // SSAO blur reach
    constexpr int nBlurReach = 8;

    TileLayout layout;
    layout.tileSize = GfVec2i(tileSize, tileSize);
    if (sampleRadius <= 0.0f)
        return layout;

    // Spiral reach of the SSAO raw pass, in pixels: sampleRadius * pixelsPerUnit / zScale
    // (ssao.glslfx).
    const double pixelsPerUnit = std::fabs(exportProj[1][1]) * imageHeight / 2.0;
    GfCamera gfCamera;
    gfCamera.SetFromViewAndProjectionMatrix(viewMatrix, exportProj);
    const double nearPlane   = gfCamera.GetFrustum().GetNearFar().GetMin();
    const double minZScale   = gfCamera.GetProjection() == GfCamera::Orthographic ? 1.0 : nearPlane;
    const double spiralReach = (pixelsPerUnit > 0.0 && minZScale > 0.0)
        ? std::ceil(std::fabs(sampleRadius) * pixelsPerUnit / minZScale)
        : 0.0;

    const int nMarginCap       = (maxTileSize / 4) & ~1;
    const double desiredMargin = spiralReach + nBlurReach;
    int margin    = static_cast<int>(std::min(desiredMargin, static_cast<double>(nMarginCap)));
    margin        = std::min(margin + (margin & 1), nMarginCap);
    layout.margin = margin;

    layout.tileSize[0] = std::min(std::max(layout.tileSize[0], 4 * margin), maxTileSize);
    layout.tileSize[1] = std::min(std::max(layout.tileSize[1], 4 * margin), maxTileSize);

    return layout;
}

} // namespace

//
// How to use the TileIterator class for tiled rendering.
//
// The iterator splits an image that is too large for a single render buffer into tiles. Each
// tile is rendered on its own, then the valid region of the tile (the tile minus its margin) is
// composited into a single full-resolution image.
//
HVT_TEST(howTo, useTileIterator)
{
    // The final image size. It is not a multiple of the valid tile size, so the last row and
    // column of tiles are partial.
    constexpr int kImageWidth  = 1200;
    constexpr int kImageHeight = 800;

    // The tile render buffer size and the margin on every side of a tile. No post-processing
    // effect runs here, but a margin is kept to show how the composite skips it.
    constexpr int kTileSize   = 256;
    constexpr int kTileMargin = 16;

    // Only one tile is rendered at a time, so the backend only needs a tile-sized window.
    auto context = TestHelpers::CreateTestContext(kTileSize, kTileSize);

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Initializes the TileIterator.
    hvt::TileIterator tileIterator;
    ASSERT_TRUE(tileIterator.Initialize(
        GfVec2i(kImageWidth, kImageHeight), kTileMargin, GfVec2i(kTileSize, kTileSize)));

    // Shifts the model up and to the left. The default framing of the test scene is symmetric,
    // so a mirrored or misplaced tile would not change the image otherwise.
    const double shift = stage.computeStageBounds().GetSize().GetLength() * 0.1;
    const GfMatrix4d viewMatrix =
        stage.viewMatrix() * GfMatrix4d().SetTranslate(GfVec3d(-1.5 * shift, shift, 0.0));

    // Defines the frame pass.
    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;
    createFramePass(*context, stage, renderIndex, sceneFramePass);

    // Renders the tiles and composites them into the final image, validated against the
    // baseline one.
    renderAndCompositeTiles({ *context, stage, sceneFramePass, tileIterator, viewMatrix,
        kImageWidth, kImageHeight, computedImageName, imageFile,
        /*validateAgainstBaseline=*/true });
}

//
// How to use the TileIterator class with a post-processing task like the SSAO one?
//
// The SSAO task is added to the frame pass as in the useSSAORenderTask example, so each tile is
// ambient-occluded on its own. The tile margin, computed from the sample radius of the effect,
// keeps the effect stable at the tile borders, so the composited image does not show any tile
// seam.
//
HVT_TEST(howTo, useTileIteratorWithSSAO)
{
    // The sample radius of the SSAO effect. It is set on the task below and given to
    // ComputeSSAOTileLayout so the tile margin covers the effect footprint.
    static constexpr float kSampleRadius = 10.f;

    // The final image size. It is not a multiple of the valid tile size, so the last row and
    // column of tiles are partial.
    constexpr int kImageWidth  = 1200;
    constexpr int kImageHeight = 800;

    // The tile size and the maximum tile size, the latter instead of the class default (4096),
    // to render several tiles. ComputeSSAOTileLayout grows the tile size up to the maximum
    // one to fit the margin.
    constexpr int kTileSize    = 256;
    constexpr int kMaxTileSize = 512;

    // Only one tile is rendered at a time, so the backend only needs a window as large as the
    // largest tile.
    auto context = TestHelpers::CreateTestContext(kMaxTileSize, kMaxTileSize);

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Shifts the model up and to the left. The default framing of the test scene is symmetric,
    // so a mirrored or misplaced tile would not change the image otherwise.
    const double shift = stage.computeStageBounds().GetSize().GetLength() * 0.1;
    const GfMatrix4d viewMatrix =
        stage.viewMatrix() * GfMatrix4d().SetTranslate(GfVec3d(-1.5 * shift, shift, 0.0));

    // Initializes the TileIterator. The margin covers the sample radius of the SSAO effect so it
    // stays stable at the tile borders.
    hvt::TileIterator tileIterator(kMaxTileSize);
    const TileLayout layout = ComputeSSAOTileLayout(
        kSampleRadius, stage.projectionMatrix(), viewMatrix, kImageHeight, kTileSize, kMaxTileSize);

    ASSERT_TRUE(tileIterator.Initialize(
        GfVec2i(kImageWidth, kImageHeight), layout.margin, layout.tileSize));

    // Defines the frame pass.
    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;
    createFramePass(*context, stage, renderIndex, sceneFramePass);

    // Adds the ssao task i.e., 'ssaoTask' before the color correction one.
    {
        // Defines the ssao task update function.

        auto fnCommit = [&sceneFramePass](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const VtValue value        = fnGetValue(HdTokens->params);
            hvt::SSAOTaskParams params = value.Get<hvt::SSAOTaskParams>();

            auto renderParams                = sceneFramePass->params().renderParams;
            params.view.cameraID             = renderParams.camera;
            params.view.framing              = renderParams.framing;
            params.view.overrideWindowPolicy = renderParams.overrideWindowPolicy;

            params.ao.isEnabled         = true;
            params.ao.isShowOnlyEnabled = true;
            params.ao.amount            = 2.0f;
            params.ao.sampleRadius      = kSampleRadius;

            fnSetValue(HdTokens->params, VtValue(params));
        };

        const SdfPath colorCorrectionTask =
            sceneFramePass->GetTaskManager()->GetTaskPath(HdxPrimitiveTokens->colorCorrectionTask);

        sceneFramePass->GetTaskManager()->AddTask<hvt::SSAOTask>(hvt::SSAOTask::GetToken(),
            hvt::SSAOTaskParams(), fnCommit, colorCorrectionTask,
            hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Renders the tiles and composites them into the final image.
    renderAndCompositeTiles({ *context, stage, sceneFramePass, tileIterator, viewMatrix,
        kImageWidth, kImageHeight, computedImageName, imageFile,
        /*validateAgainstBaseline=*/true });
}

//
// How to use the TileIterator class with a post-processing task like the blur one?
//
// The blur task is added to the frame pass as in the createACustomRenderTask example, so each
// tile is blurred on its own. The tile margin, set from the blur amount, covers the reach of
// the blur effect, so it stays stable at the tile borders and the composited image does not
// show any tile seam.
//
HVT_TEST(howTo, useTileIteratorWithBlur)
{
    // The amount of blur to apply. It is set on the task below and also gives the tile margin,
    // as the reach of the blur effect matches the blur amount.
    static constexpr float kBlurAmount = 8.0f;

    // The final image size. It is not a multiple of the valid tile size, so the last row and
    // column of tiles are partial.
    constexpr int kImageWidth  = 1200;
    constexpr int kImageHeight = 800;

    // The tile render buffer size and the margin on every side of a tile. The margin covers the
    // reach of the blur effect so it stays stable at the tile borders; the composite skips it.
    constexpr int kTileSize   = 256;
    constexpr int kTileMargin = static_cast<int>(kBlurAmount);

    // Only one tile is rendered at a time, so the backend only needs a tile-sized window.
    auto context = TestHelpers::CreateTestContext(kTileSize, kTileSize);

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Initializes the TileIterator.
    hvt::TileIterator tileIterator;
    ASSERT_TRUE(tileIterator.Initialize(
        GfVec2i(kImageWidth, kImageHeight), kTileMargin, GfVec2i(kTileSize, kTileSize)));

    // Shifts the model up and to the left. The default framing of the test scene is symmetric,
    // so a mirrored or misplaced tile would not change the image otherwise.
    const double shift = stage.computeStageBounds().GetSize().GetLength() * 0.1;
    const GfMatrix4d viewMatrix =
        stage.viewMatrix() * GfMatrix4d().SetTranslate(GfVec3d(-1.5 * shift, shift, 0.0));

    // Defines the frame pass.
    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;
    createFramePass(*context, stage, renderIndex, sceneFramePass);

    // Adds the blur task i.e., 'blurTask' before the present one.
    {
        // Defines the blur task update function.

        auto fnCommit = [](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                            hvt::TaskManager::SetTaskValueFn const& fnSetValue)
        {
            const VtValue value        = fnGetValue(HdTokens->params);
            hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
            params.blurAmount          = kBlurAmount;
            fnSetValue(HdTokens->params, VtValue(params));
        };

        const SdfPath presentTask =
            sceneFramePass->GetTaskManager()->GetTaskPath(HdxPrimitiveTokens->presentTask);

        sceneFramePass->GetTaskManager()->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(),
            hvt::BlurTaskParams(), fnCommit, presentTask,
            hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Renders the tiles and composites them into the final image, validated against the
    // baseline one.
    renderAndCompositeTiles({ *context, stage, sceneFramePass, tileIterator, viewMatrix,
        kImageWidth, kImageHeight, computedImageName, imageFile,
        /*validateAgainstBaseline=*/true });
}
