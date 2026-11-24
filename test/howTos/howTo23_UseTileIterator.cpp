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

#include <RenderingFramework/GpuImageCapture.h>
#include <RenderingFramework/ImageUtils.h>
#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/tileIterator.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/blurTask.h>
#include <hvt/tasks/ssaoTask.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/texture.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace TestHelpers;
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
//
// Renders the tile of the given display window, the render buffer being the whole tile.
//
void renderTile(TestHelpers::TestContext& context, TestHelpers::TestStage& stage,
    hvt::FramePassPtr const& sceneFramePass, GfMatrix4d const& viewMatrix, GfVec2i const& tileSize,
    GfRange2f const& displayWindow)
{
    auto& params = sceneFramePass->params();

    params.renderBufferSize = tileSize;
    params.visualizeAOV     = HdAovTokens->color;

    params.viewInfo.viewMatrix       = viewMatrix;
    params.viewInfo.projectionMatrix = stage.projectionMatrix();
    params.viewInfo.lights           = stage.defaultLights();
    params.viewInfo.material         = stage.defaultMaterial();
    params.viewInfo.ambient          = stage.defaultAmbient();
    params.viewInfo.framing =
        CameraUtilFraming(displayWindow, GfRect2i(GfVec2i(0, 0), tileSize[0], tileSize[1]));

    params.colorspace      = HdxColorCorrectionTokens->sRGB;
    params.backgroundColor = TestHelpers::ColorDarkGrey;
    params.selectionColor  = TestHelpers::ColorYellow;

    params.enablePresentation = context.presentationEnabled();

    sceneFramePass->Render();
    context._backend->waitForGPUIdle();
}

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

    // The composited image: 8-bit RGBA pixels, the first row being the top of the image.
    std::vector<uint8_t> fullImage(static_cast<size_t>(imageWidth) * imageHeight * 4, 0);

    // Reads the tiles back in the row order of the backend.
    GpuImageCapture gpuCapture;
    gpuCapture.flipVertically(context._backend->imageCapture().flipVertically());

    hvt::TileContext tileContext;

    // Renders the current tile and composites it into the full image.
    auto render = [&]()
    {
        renderTile(context, stage, sceneFramePass, viewMatrix, tileSize, tileContext.displayWindow);

        // Captures the tile.
        gpuCapture.capture(sceneFramePass.get(), context._backend->hgi(), tileW, tileH);
        const std::vector<uint8_t> tilePixels = gpuCapture.rgba8Data();

        // Extracts the valid region of the tile (i.e., the tile minus its margin band) as a
        // standalone image, the first row being the top of the image. The margin band around
        // the valid region only exists to keep post-processing effects stable at the tile
        // borders, so it is not part of the composited image.
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
// Creates the depth guide of the SSAO task for a tiled render: a full-image depth texture,
// downsampled by guideScale, which the task reads for the samples that fall outside a tile.
//
// The guide is produced by a depth-only sweep over the image, with the SSAO task disabled: each
// tile depth AOV is read back, then the full-image depth is min-downsampled, so that every guide
// texel stores the depth of the closest surface of its block of pixels, as the task expects.
// The sweep runs on the CPU for clarity; an application would rather reduce the depth on the
// GPU, without the read-back.
//
// Note: The guide stores the raw depth values in the row order of the backend's render
// textures, which is what the task reads from.
//
HgiTextureHandle createDepthGuide(TestHelpers::TestContext& context, TestHelpers::TestStage& stage,
    hvt::FramePassPtr const& sceneFramePass, SdfPath const& ssaoTask, GfMatrix4d const& viewMatrix,
    int imageWidth, int imageHeight, GfVec2i const& tileSize, int guideScale)
{
    hvt::TaskManagerPtr const& taskManager = sceneFramePass->GetTaskManager();
    Hgi* hgi                               = context._backend->hgi();

    // The depth AOV does not depend on the SSAO task, which only post-processes the color AOV.
    taskManager->EnableTask(ssaoTask, false);

    // The full-image depth, in the row order of the backend's render textures. The sweep needs
    // no margin, as the depth has no post-processing.
    std::vector<float> imageDepth(static_cast<size_t>(imageWidth) * imageHeight, 1.0f);
    const bool isBottomUp = context._backend->imageCapture().flipVertically();

    hvt::TileIterator sweep;
    if (!sweep.Initialize(GfVec2i(imageWidth, imageHeight), 0, tileSize))
    {
        return HgiTextureHandle();
    }

    const int tileW = tileSize[0];
    const int tileH = tileSize[1];
    std::vector<float> tileDepth(static_cast<size_t>(tileW) * tileH);

    hvt::TileContext tileContext;
    auto render = [&]()
    {
        renderTile(context, stage, sceneFramePass, viewMatrix, tileSize, tileContext.displayWindow);

        // Reads the tile depth back.
        HgiTextureHandle depthTexture = sceneFramePass->GetRenderTexture(HdAovTokens->depth);
        HgiTextureGpuToCpuOp readBackOp {};
        readBackOp.gpuSourceTexture          = depthTexture;
        readBackOp.cpuDestinationBuffer      = tileDepth.data();
        readBackOp.destinationBufferByteSize = tileDepth.size() * sizeof(float);
        HgiBlitCmdsUniquePtr blitCmds        = hgi->CreateBlitCmds();
        blitCmds->CopyTextureGpuToCpu(readBackOp);
        hgi->SubmitCmds(blitCmds.get(), HgiSubmitWaitTypeWaitUntilCompleted);

        // Copies the tile depth into the full-image one, keeping the texture row order: a
        // bottom-up tile row r is the top-down tile row tileH - 1 - r, and so on for the image.
        const int validW = tileContext.maxCoordinate[0] - tileContext.minCoordinate[0];
        const int validH = tileContext.maxCoordinate[1] - tileContext.minCoordinate[1];
        for (int row = 0; row < validH; ++row)
        {
            const int imageRow  = tileContext.minCoordinate[1] + row;
            const size_t srcRow = static_cast<size_t>(isBottomUp ? tileH - 1 - row : row);
            const size_t dstRow =
                static_cast<size_t>(isBottomUp ? imageHeight - 1 - imageRow : imageRow);
            std::memcpy(imageDepth.data() + dstRow * imageWidth + tileContext.minCoordinate[0],
                tileDepth.data() + srcRow * tileW, static_cast<size_t>(validW) * sizeof(float));
        }

        return false; // The tile depth is copied, no more frame is needed.
    };

    while (!sweep.IsDone())
    {
        tileContext = sweep.Next();
        context.run(render, sceneFramePass.get());
    }

    taskManager->EnableTask(ssaoTask, true);

    // Min-downsamples the full-image depth: a guide texel keeps the closest surface of its
    // guideScale x guideScale block, the last row and column of blocks being partial when the
    // image size is not a multiple of the scale.
    const int guideW = (imageWidth + guideScale - 1) / guideScale;
    const int guideH = (imageHeight + guideScale - 1) / guideScale;
    std::vector<float> guideDepth(static_cast<size_t>(guideW) * guideH, 1.0f);
    for (int y = 0; y < imageHeight; ++y)
    {
        for (int x = 0; x < imageWidth; ++x)
        {
            float& texel =
                guideDepth[static_cast<size_t>(y / guideScale) * guideW + x / guideScale];
            texel = std::min(texel, imageDepth[static_cast<size_t>(y) * imageWidth + x]);
        }
    }

    // Creates the guide texture: a single channel float color texture.
    HgiTextureDesc guideDesc;
    guideDesc.debugName      = "SSAO Depth Guide";
    guideDesc.dimensions     = GfVec3i(guideW, guideH, 1);
    guideDesc.format         = HgiFormatFloat32;
    guideDesc.usage          = HgiTextureUsageBitsShaderRead;
    guideDesc.initialData    = guideDepth.data();
    guideDesc.pixelsByteSize = guideDepth.size() * sizeof(float);

    return hgi->CreateTexture(guideDesc);
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
// ambient-occluded on its own. The SSAO samples can reach far outside a tile, so the task is
// given a depth guide: a downsampled depth of the full image, produced by a first depth-only
// sweep over the image, which the task reads for the samples outside the tile. The tile margin,
// queried from the task through the KernelPostProcessingEffect interface, then only covers the
// SSAO denoising blur. The task also keys its per-pixel sampling pattern on the full image
// (through the framing), so the composited image does not show any tile seam.
//
HVT_TEST(howTo, useTileIteratorWithSSAO)
{
    // The sample radius of the SSAO effect. It is set on the task below.
    static constexpr float kSampleRadius = 10.f;

    // The downsampling scale of the depth guide: a quarter of the image resolution in each
    // direction, i.e. 1/16 of the full-resolution guide memory.
    constexpr int kGuideScale = 4;

    // The final image size. It is not a multiple of the valid tile size, so the last row and
    // column of tiles are partial.
    constexpr int kImageWidth  = 6000;
    constexpr int kImageHeight = 6000;

    // The tile size. With the depth guide, the tile margin is small, so the tiles keep this
    // size.
    constexpr int kTileSize = 1000;

    // Only one tile is rendered at a time, so the backend only needs a tile-sized window.
    auto context = TestHelpers::CreateTestContext(kTileSize, kTileSize);

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    // Shifts the model up and to the left. The default framing of the test scene is symmetric,
    // so a mirrored or misplaced tile would not change the image otherwise.
    const double shift = stage.computeStageBounds().GetSize().GetLength() * 0.1;
    const GfMatrix4d viewMatrix =
        stage.viewMatrix() * GfMatrix4d().SetTranslate(GfVec3d(-1.5 * shift, shift, 0.0));

    // Defines the frame pass.
    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;
    createFramePass(*context, stage, renderIndex, sceneFramePass);

    // Adds the ssao task i.e., 'ssaoTask' before the color correction one.
    hvt::TaskManagerPtr const& taskManager = sceneFramePass->GetTaskManager();
    SdfPath ssaoTask;
    {
        // Defines the ssao task update function. It only sets the view, so the effect
        // parameters and the depth guide, stored in the task manager below, are kept.
        auto fnCommit = [&sceneFramePass](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
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

        // The effect parameters are given as the initial task parameters, because the tile
        // margin below is computed from the parameters stored in the task manager, before the
        // commit function first runs.
        hvt::SSAOTaskParams initialParams;
        initialParams.ao.isEnabled         = true;
        initialParams.ao.isShowOnlyEnabled = true;
        initialParams.ao.amount            = 2.0f;
        initialParams.ao.sampleRadius      = kSampleRadius;

        ssaoTask = taskManager->AddTask<hvt::SSAOTask>(hvt::SSAOTask::GetToken(), initialParams,
            fnCommit, colorCorrectionTask, hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Creates the depth guide with a depth-only sweep over the image, then stores it in the task
    // parameters. It must be stored before the TileIterator initialization, which computes the
    // tile margin from the stored parameters, and be complete before the first tile renders.
    HgiTextureHandle depthGuide = createDepthGuide(*context, stage, sceneFramePass, ssaoTask,
        viewMatrix, kImageWidth, kImageHeight, GfVec2i(kTileSize, kTileSize), kGuideScale);
    ASSERT_TRUE(depthGuide);
    {
        hvt::SSAOTaskParams params =
            taskManager->GetTaskValue(ssaoTask, HdTokens->params).Get<hvt::SSAOTaskParams>();
        params.depthGuide = depthGuide;
        params.guideScale = kGuideScale;
        ASSERT_TRUE(taskManager->SetTaskValue(ssaoTask, HdTokens->params, VtValue(params)));
    }

    // Initializes the TileIterator. The margin is queried from the post-processing tasks of the
    // frame pass i.e., the ssao task: with the depth guide, it only covers the denoising blur.
    hvt::TileIterator tileIterator;

    hvt::ScreenSpaceKernelEffect::MarginContext viewContext;
    viewContext.imageResolution  = GfVec2i(kImageWidth, kImageHeight);
    viewContext.projectionMatrix = stage.projectionMatrix();
    viewContext.viewMatrix       = viewMatrix;

    ASSERT_TRUE(tileIterator.Initialize(GfVec2i(kImageWidth, kImageHeight), *taskManager,
        GfVec2i(kTileSize, kTileSize), viewContext));
    EXPECT_EQ(tileIterator.GetTileMargin(), 8);
    EXPECT_EQ(tileIterator.GetTileSize(), GfVec2i(kTileSize, kTileSize));

    // Renders the tiles and composites them into the final image.
    renderAndCompositeTiles({ *context, stage, sceneFramePass, tileIterator, viewMatrix,
        kImageWidth, kImageHeight, computedImageName, imageFile,
        /*validateAgainstBaseline=*/true });

    // The guide is owned by the application, which destroys it once the tiles are rendered.
    context->_backend->hgi()->DestroyTexture(&depthGuide);
}

//
// How to use the TileIterator class with a post-processing task like the blur one?
//
// The blur task is added to the frame pass as in the createACustomRenderTask example, so each
// tile is blurred on its own. The tile margin, queried from the task through the
// ScreenSpaceKernelEffect interface, covers the reach of the blur effect, so it stays stable
// at the tile borders and the composited image does not show any tile seam.
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

    // The tile render buffer size. The margin, queried from the blur task, covers the reach of
    // the blur effect so it stays stable at the tile borders; the composite skips it.
    constexpr int kTileSize = 256;

    // Only one tile is rendered at a time, so the backend only needs a tile-sized window.
    auto context = TestHelpers::CreateTestContext(kTileSize, kTileSize);

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

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

        // The blur amount is also given as the initial task parameter, because the tile margin
        // below is computed from the parameters stored in the task manager, before the commit
        // function first runs.
        hvt::BlurTaskParams initialParams;
        initialParams.blurAmount = kBlurAmount;

        sceneFramePass->GetTaskManager()->AddTask<hvt::BlurTask>(hvt::BlurTask::GetToken(),
            initialParams, fnCommit, presentTask, hvt::TaskManager::InsertionOrder::insertBefore);
    }

    // Initializes the TileIterator. The margin is queried from the post-processing tasks of the
    // frame pass i.e., the blur task, so the effect stays stable at the tile borders.
    hvt::TileIterator tileIterator;

    hvt::ScreenSpaceKernelEffect::MarginContext viewContext;
    viewContext.imageResolution  = GfVec2i(kImageWidth, kImageHeight);
    viewContext.projectionMatrix = stage.projectionMatrix();
    viewContext.viewMatrix       = viewMatrix;

    ASSERT_TRUE(tileIterator.Initialize(GfVec2i(kImageWidth, kImageHeight),
        *sceneFramePass->GetTaskManager(), GfVec2i(kTileSize, kTileSize), viewContext));

    // Renders the tiles and composites them into the final image, validated against the
    // baseline one.
    renderAndCompositeTiles({ *context, stage, sceneFramePass, tileIterator, viewMatrix,
        kImageWidth, kImageHeight, computedImageName, imageFile,
        /*validateAgainstBaseline=*/true });
}
