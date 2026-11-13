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

#include <hvt/engine/tileIterator.h>

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/frustum.h>

#include <algorithm>
#include <cmath>

TileIterator::TileIterator(int defaultTileSize, int maxTileSize)
{
    nRows        = 0;
    nCols        = 0;
    nTiles       = 0;
    currentIndex = 0;

    xCoord        = nullptr;
    yCoord        = nullptr;
    norXCoord     = nullptr;
    norYCoord     = nullptr;
    fullImageSize = pxr::GfVec2i(0, 0);

    mTileMargin    = 0;
    norXTileMargin = 0;
    norYTileMargin = 0;

    // Non-positive sizes fall back to the class defaults.
    mDefaultTileSize = defaultTileSize > 0 ? defaultTileSize : DEFAULT_TILE_SIZE;
    mMaxTileSize     = maxTileSize > 0 ? maxTileSize : MAX_TILE_SIZE;

    // The default tile size cannot exceed the maximum tile size.
    mDefaultTileSize = std::min(mDefaultTileSize, mMaxTileSize);
}

TileIterator::~TileIterator()
{
    delete[] xCoord;
    delete[] yCoord;
    delete[] norXCoord;
    delete[] norYCoord;
}

TileIterator::TileLayout TileIterator::computeTileLayout(float sampleRadius,
    const pxr::GfMatrix4d& exportProj, const pxr::GfMatrix4d& viewMatrix, int nImageHeight) const
{
    // SSAO blur reach
    constexpr int nBlurReach = 8;

    TileLayout layout;
    layout.tileSize = pxr::GfVec2i(mDefaultTileSize, mDefaultTileSize);
    if (sampleRadius <= 0.0f)
        return layout;

    // Spiral reach of the SSAO raw pass, in pixels: sampleRadius * pixelsPerUnit / zScale
    // (ssao.glslfx).
    const double pixelsPerUnit = std::fabs(exportProj[1][1]) * nImageHeight / 2.0;
    pxr::GfCamera gfCamera;
    gfCamera.SetFromViewAndProjectionMatrix(viewMatrix, exportProj);
    const double nearPlane = gfCamera.GetFrustum().GetNearFar().GetMin();
    const double minZScale =
        gfCamera.GetProjection() == pxr::GfCamera::Orthographic ? 1.0 : nearPlane;
    const double spiralReach = (pixelsPerUnit > 0.0 && minZScale > 0.0)
        ? std::ceil(std::fabs(sampleRadius) * pixelsPerUnit / minZScale)
        : 0.0;

    const int nMarginCap       = (mMaxTileSize / 4) & ~1;
    const double desiredMargin = spiralReach + nBlurReach;
    int margin    = static_cast<int>(std::min(desiredMargin, static_cast<double>(nMarginCap)));
    margin        = std::min(margin + (margin & 1), nMarginCap);
    layout.margin = margin;

    layout.tileSize[0] = std::min(std::max(layout.tileSize[0], 4 * margin), mMaxTileSize);
    layout.tileSize[1] = std::min(std::max(layout.tileSize[1], 4 * margin), mMaxTileSize);

    return layout;
}

bool TileIterator::Initialize(
    const pxr::GfVec2i& imageResolution, unsigned int tileMargin, const pxr::GfVec2i& tileSize)
{
    auto maxSize = GetMaxTileSize();
    if (tileSize[0] > maxSize[0] || tileSize[1] > maxSize[1])
        return false;

    mTileMargin = tileMargin;
    mTileSize   = tileSize;

    fullImageSize = imageResolution;

    ConstructTile(imageResolution, tileMargin, tileSize);

    tileContext  = std::make_unique<TileContext>();
    currentIndex = 0;

    return true;
}

const TileContext& TileIterator::Next()
{
    if (nCols == 0)
    {
        ClearTileContext();
        return *tileContext;
    }

    int row = currentIndex / nCols;
    int col = currentIndex - row * nCols;

    currentIndex++;

    return this->GetTile(row, col);
}

void TileIterator::GetNumberOfTiles(int& rows, int& cols) const
{
    rows = nRows;
    cols = nCols;
}

const TileContext& TileIterator::GetTile(int row, int col)
{
    if (row >= nRows || col >= nCols)
    {
        ClearTileContext();
        return *tileContext;
    }

    tileContext->minCoordinate = pxr::GfVec2i(xCoord[col], yCoord[row]);
    if (xCoord[col + 1] <= fullImageSize[0])
        tileContext->maxCoordinate[0] = xCoord[col + 1];
    else
        tileContext->maxCoordinate[0] = fullImageSize[0];

    if (yCoord[row + 1] <= fullImageSize[1])
        tileContext->maxCoordinate[1] = yCoord[row + 1];
    else
        tileContext->maxCoordinate[1] = fullImageSize[1];

    // X follows the framing's y-down convention directly, but Y is compensated (y0 = yCoord[row] − margin − H + tileSize)
    // for HdStorm's mirrored band selection with full-buffer data windows,
    // so the tile renders its intended band [yCoord[row] − margin, +tileSize)
    // with pixel (0,0) at full-image pixel (xCoord[col] − margin, yCoord[row] − margin)
    const float x0 = static_cast<float>(mTileMargin - xCoord[col]);
    const float y0 =
        static_cast<float>(yCoord[row] - mTileMargin - fullImageSize[1] + mTileSize[1]);
    tileContext->displayWindow = pxr::GfRange2f(
        pxr::GfVec2f(x0, y0), pxr::GfVec2f(x0 + fullImageSize[0], y0 + fullImageSize[1]));

    return *tileContext;
}

pxr::GfVec2i TileIterator::GetMaxTileSize() const
{
    return pxr::GfVec2i(mMaxTileSize, mMaxTileSize);
}

void TileIterator::ClearTileContext()
{
    if (tileContext == nullptr)
        return;

    tileContext->minCoordinate = pxr::GfVec2i(0, 0);
    tileContext->maxCoordinate = pxr::GfVec2i(0, 0);
    tileContext->displayWindow = pxr::GfRange2f();
}

void TileIterator::ConstructTile(
    const pxr::GfVec2i& imageResolution, unsigned int tileMargin, const pxr::GfVec2i& tileSize)
{
    int fullWidth  = imageResolution[0];
    int fullHeight = imageResolution[1];

    int realWidth  = tileSize[0] - 2 * tileMargin;
    int realHeight = tileSize[1] - 2 * tileMargin;

    nRows  = (fullHeight + realHeight - 1) / realHeight;
    nCols  = (fullWidth + realWidth - 1) / realWidth;
    nTiles = nRows * nCols;

    xCoord = new int[nCols + 1];
    yCoord = new int[nRows + 1];

    norXCoord = new float[nCols + 1];
    norYCoord = new float[nRows + 1];

    norXTileMargin = (float)mTileMargin / fullWidth;
    norYTileMargin = (float)mTileMargin / fullHeight;

    for (int k = 0; k <= nCols; ++k)
    {
        xCoord[k]    = k * realWidth;
        norXCoord[k] = float(xCoord[k]) / fullWidth;
    }

    for (int k = 0; k <= nRows; ++k)
    {
        yCoord[k]    = k * realHeight;
        norYCoord[k] = float(yCoord[k]) / fullHeight;
    }
}
