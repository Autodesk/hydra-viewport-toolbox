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
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/tf/diagnostic.h>

#include <algorithm>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

TileIterator::TileIterator(int defaultTileSize, int maxTileSize)
{
    // Non-positive sizes fall back to the class defaults.
    _defaultTileSize = defaultTileSize > 0 ? defaultTileSize : kDefaultTileSize;
    _maxTileSize     = maxTileSize > 0 ? maxTileSize : kMaxTileSize;

    // The default tile size cannot exceed the maximum tile size.
    _defaultTileSize = std::min(_defaultTileSize, _maxTileSize);
}

TileIterator::TileLayout TileIterator::ComputeTileLayout(float sampleRadius,
    GfMatrix4d const& exportProj, GfMatrix4d const& viewMatrix, int imageHeight) const
{
    // SSAO blur reach
    constexpr int nBlurReach = 8;

    TileLayout layout;
    layout.tileSize = GfVec2i(_defaultTileSize, _defaultTileSize);
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

    const int nMarginCap       = (_maxTileSize / 4) & ~1;
    const double desiredMargin = spiralReach + nBlurReach;
    int margin    = static_cast<int>(std::min(desiredMargin, static_cast<double>(nMarginCap)));
    margin        = std::min(margin + (margin & 1), nMarginCap);
    layout.margin = margin;

    layout.tileSize[0] = std::min(std::max(layout.tileSize[0], 4 * margin), _maxTileSize);
    layout.tileSize[1] = std::min(std::max(layout.tileSize[1], 4 * margin), _maxTileSize);

    return layout;
}

bool TileIterator::Initialize(
    GfVec2i const& imageResolution, int tileMargin, GfVec2i const& tileSize)
{
    _Reset();

    if (imageResolution[0] <= 0 || imageResolution[1] <= 0)
    {
        TF_CODING_ERROR("TileIterator: the image resolution (%d, %d) must be positive.",
            imageResolution[0], imageResolution[1]);
        return false;
    }

    if (tileSize[0] <= 0 || tileSize[1] <= 0)
    {
        TF_CODING_ERROR(
            "TileIterator: the tile size (%d, %d) must be positive.", tileSize[0], tileSize[1]);
        return false;
    }

    if (tileSize[0] > _maxTileSize || tileSize[1] > _maxTileSize)
        return false;

    const GfVec2i validTileSize(tileSize[0] - 2 * tileMargin, tileSize[1] - 2 * tileMargin);
    if (tileMargin < 0 || validTileSize[0] <= 0 || validTileSize[1] <= 0)
    {
        TF_CODING_ERROR(
            "TileIterator: the tile margin %d must not be negative and must "
            "leave valid pixels in a (%d, %d) tile.",
            tileMargin, tileSize[0], tileSize[1]);
        return false;
    }

    _imageSize     = imageResolution;
    _tileSize      = tileSize;
    _tileMargin    = tileMargin;
    _validTileSize = validTileSize;

    _numRows =
        static_cast<unsigned int>((_imageSize[1] + _validTileSize[1] - 1) / _validTileSize[1]);
    _numCols =
        static_cast<unsigned int>((_imageSize[0] + _validTileSize[0] - 1) / _validTileSize[0]);

    return true;
}

TileContext TileIterator::Next()
{
    if (IsDone())
        return {};

    const unsigned int row = _currentIndex / _numCols;
    const unsigned int col = _currentIndex % _numCols;

    ++_currentIndex;

    return GetTile(row, col);
}

TileContext TileIterator::GetTile(unsigned int row, unsigned int col) const
{
    if (row >= _numRows || col >= _numCols)
        return {};

    const int x = static_cast<int>(col) * _validTileSize[0];
    const int y = static_cast<int>(row) * _validTileSize[1];

    TileContext tile;
    tile.minCoordinate = GfVec2i(x, y);
    tile.maxCoordinate = GfVec2i(std::min(x + _validTileSize[0], _imageSize[0]),
        std::min(y + _validTileSize[1], _imageSize[1]));

    // Offsets the full-image display window so the tile pixel (margin, margin) is the full-image
    // pixel (x, y).
    const GfVec2f origin(static_cast<float>(_tileMargin - x), static_cast<float>(_tileMargin - y));
    tile.displayWindow = GfRange2f(origin,
        origin + GfVec2f(static_cast<float>(_imageSize[0]), static_cast<float>(_imageSize[1])));

    return tile;
}

void TileIterator::_Reset()
{
    _imageSize     = GfVec2i(0, 0);
    _tileSize      = GfVec2i(0, 0);
    _tileMargin    = 0;
    _validTileSize = GfVec2i(0, 0);
    _numRows       = 0;
    _numCols       = 0;
    _currentIndex  = 0;
}

} // namespace HVT_NS
