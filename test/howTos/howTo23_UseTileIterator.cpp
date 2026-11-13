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

#include <hvt/engine/viewportEngine.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/tileIterator.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/imaging/hdx/tokens.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace TestHelpers;
PXR_NAMESPACE_USING_DIRECTIVE

//
// How to use the TileIterator class for tiled rendering.
//
// The iterator splits an image that is too large for a single render buffer into tiles. Each
// tile is rendered on its own, then the valid region of the tile (the tile minus its margin) is
// composited into a single full-resolution image.
//
HVT_TEST(howTo, useTileIterator)
{
    // final image dimensions
    int imageWidth  = 5000;
    int imageHeight = 5000;

    auto context = TestHelpers::CreateTestContext(imageWidth, imageHeight);

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    hvt::RenderIndexProxyPtr renderIndex;
    hvt::FramePassPtr sceneFramePass;

    // Define and initialize the TileIterator
    //
    // For the purpose of this test we set custom values for the default size and maximum size.
    // The default values are 2048 and 4096.
    int defaultTileSize = 500;
    int maxTileSize     = 1000;
    TileContext tileContext;
    TileIterator tileIterator       = TileIterator(defaultTileSize, maxTileSize);
    TileIterator::TileLayout layout = tileIterator.computeTileLayout(
        0.f, stage.projectionMatrix(), stage.viewMatrix(), imageHeight);

    ASSERT_TRUE(tileIterator.Initialize(
        pxr::GfVec2i(imageWidth, imageHeight), layout.margin, layout.tileSize));

    // Output folder for the TEMP DEBUG reference image (see the HVT_DEBUG_TILE_REFERENCE block
    // below).
    const std::filesystem::path tileImagesFolder =
        getOutputDataFolder() / (computedImageName + "_tiles");

    // The composited image: 8-bit RGBA pixels, the first row being the top of the image.
    std::vector<uint8_t> fullImage(static_cast<size_t>(imageWidth) * imageHeight * 4, 0);

    // Defines the frame pass
    {
        // Creates the render index by providing the hgi driver and the requested renderer name.
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &context->_backend->hgiDriver();
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Creates the scene index containing the model.

        pxr::HdSceneIndexBaseRefPtr sceneIndex =
            hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex = renderIndex->RenderIndex();
        passDesc.uid         = pxr::SdfPath("/sceneFramePass");
        sceneFramePass       = hvt::ViewportEngine::CreateFramePass(passDesc);

        // Renders the current tile and composites it into the full image.
        auto render = [&]()
        {
            auto& params = sceneFramePass->params();

            params.renderBufferSize = layout.tileSize;
            params.visualizeAOV     = HdAovTokens->color;

            params.viewInfo.viewMatrix       = stage.viewMatrix();
            params.viewInfo.projectionMatrix = stage.projectionMatrix();
            params.viewInfo.lights           = stage.defaultLights();
            params.viewInfo.material         = stage.defaultMaterial();
            params.viewInfo.ambient          = stage.defaultAmbient();

            params.colorspace      = pxr::HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = TestHelpers::ColorDarkGrey;
            params.selectionColor  = TestHelpers::ColorYellow;

            params.enablePresentation = context->presentationEnabled();

            sceneFramePass->Render();
            context->_backend->waitForGPUIdle();

            // Captures the tile.
            const int tileW = layout.tileSize[0];
            const int tileH = layout.tileSize[1];

            GpuImageCapture gpuCapture;
            gpuCapture.capture(sceneFramePass.get(), context->_backend->hgi(), tileW, tileH);
            const std::vector<uint8_t> tilePixels = gpuCapture.rgba8Data(tileW, tileH);

            // Composites the valid region of the tile into the full image. The tile covers the
            // full-image rectangle [minCoordinate, maxCoordinate); the margin band around it
            // only exists to keep post-processing effects stable at the tile borders, so it is
            // not copied.
            const int margin = tileIterator.GetTileMargin();
            const int validW = tileContext.maxCoordinate[0] - tileContext.minCoordinate[0];
            const int validH = tileContext.maxCoordinate[1] - tileContext.minCoordinate[1];

            for (int row = 0; row < validH; ++row)
            {
                const size_t srcRow = static_cast<size_t>(margin + row);
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
            const pxr::GfRect2i dataWindow =
                pxr::GfRect2i(pxr::GfVec2i(0, 0), layout.tileSize[0], layout.tileSize[1]);

            auto& params            = sceneFramePass->params();
            params.viewInfo.framing = pxr::CameraUtilFraming(tileContext.displayWindow, dataWindow);

            context->run(render, sceneFramePass.get());
        }

        // Writes the composited image as a single PNG file.
        static const std::filesystem::path filePath = getOutputDataFolder();
        const std::filesystem::path screenShotPath =
            context->_backend->getFilename(filePath, computedImageName + "_computed");

        ASSERT_TRUE(GpuImageCapture::writePng(
            screenShotPath, imageWidth, imageHeight, fullImage, /*flipVertically=*/false));
    }
}
