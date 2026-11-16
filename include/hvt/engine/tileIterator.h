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
#pragma once

#include <hvt/api.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/vec2i.h>

namespace HVT_NS
{

/// Describes one tile of a tiled render. Coordinates are full-image pixels in the y-down
/// convention of CameraUtilFraming i.e., (0, 0) is the top-left corner of the image.
struct TileContext
{
    /// The first full-image pixel the tile contributes to the final image (inclusive).
    PXR_NS::GfVec2i minCoordinate { 0, 0 };
    /// The last full-image pixel the tile contributes to the final image (exclusive).
    PXR_NS::GfVec2i maxCoordinate { 0, 0 };
    /// The display window to give to CameraUtilFraming, with a data window covering the whole
    /// tile render buffer. The tile pixel (margin, margin) is then the full-image pixel
    /// minCoordinate.
    PXR_NS::GfRange2f displayWindow;
};

/// Splits an image too large for a single render buffer into tiles.
///
/// Each tile renders tileSize pixels. A band of margin pixels on every side of the tile only
/// gives screen-space effects (e.g. SSAO) valid neighbors at the tile borders; the rest of the
/// tile, [minCoordinate, maxCoordinate), is copied into the final image. Tiles are visited row by
/// row, starting at the top-left corner of the image.
///
/// \code
/// HVT_NS::TileIterator iterator;
/// if (iterator.Initialize(imageSize, margin, tileSize))
/// {
///     while (!iterator.IsDone())
///     {
///         HVT_NS::TileContext const tile = iterator.Next();
///         params.viewInfo.framing        = CameraUtilFraming(
///             tile.displayWindow, GfRect2i(GfVec2i(0, 0), tileSize[0], tileSize[1]));
///         // Renders the tile, reads it back, then copies its valid region.
///     }
/// }
/// \endcode
class HVT_API TileIterator
{
public:
    /// The default tile size, in pixels.
    static constexpr int kDefaultTileSize = 2048;
    /// The default maximum tile size, in pixels.
    static constexpr int kMaxTileSize = 4096;

    /// The tile size and margin computed by ComputeTileLayout.
    struct TileLayout
    {
        PXR_NS::GfVec2i tileSize { 0, 0 };
        int margin = 0;
    };

    /// Creates an iterator with the given tile size limits.
    /// \param defaultTileSize The tile size used by ComputeTileLayout, in pixels.
    /// \param maxTileSize The largest tile size Initialize accepts, in pixels.
    /// \note Non-positive sizes fall back to the class defaults, and the default tile size is
    /// clamped to the maximum one.
    explicit TileIterator(int defaultTileSize = kDefaultTileSize, int maxTileSize = kMaxTileSize);

    /// Splits the image into tiles and restarts the iteration.
    /// \param imageResolution The full image size, in pixels.
    /// \param tileMargin The margin on every side of a tile, in pixels.
    /// \param tileSize The tile render buffer size including the margins, in pixels.
    /// \return False, leaving the iterator empty, when the tile size exceeds the maximum tile
    /// size, or when the arguments are invalid (a coding error is then posted).
    bool Initialize(
        PXR_NS::GfVec2i const& imageResolution, int tileMargin, PXR_NS::GfVec2i const& tileSize);

    /// Returns the next tile and advances the iteration, or an empty tile once done.
    TileContext Next();

    /// Returns true once every tile was returned by Next.
    bool IsDone() const { return _currentIndex >= GetNumberOfTiles(); }

    /// Returns the tile at the given row and column, or an empty tile when out of range.
    TileContext GetTile(unsigned int row, unsigned int col) const;

    /// Returns the number of tile rows.
    unsigned int GetNumberOfRows() const { return _numRows; }
    /// Returns the number of tile columns.
    unsigned int GetNumberOfColumns() const { return _numCols; }
    /// Returns the total number of tiles.
    unsigned int GetNumberOfTiles() const { return _numRows * _numCols; }

    /// Returns the index of the tile the next call to Next returns.
    unsigned int GetCurrentIndex() const { return _currentIndex; }

    /// Returns the largest tile size Initialize accepts.
    PXR_NS::GfVec2i GetMaxTileSize() const { return PXR_NS::GfVec2i(_maxTileSize, _maxTileSize); }
    /// Returns the tile size given to Initialize.
    PXR_NS::GfVec2i GetTileSize() const { return _tileSize; }
    /// Returns the tile margin given to Initialize.
    int GetTileMargin() const { return _tileMargin; }

    /// Computes a tile size and margin wide enough for the SSAO task to render without seams at
    /// the tile borders, honoring the tile size limits configured at construction.
    ///
    /// The margin covers the reach of the SSAO raw pass (see ssao.glslfx), plus 8 pixels for its
    /// blur passes. The reach is computed at the near plane, which is the worst case for a
    /// perspective camera. The margin does not account for any other screen-space effect.
    ///
    /// \param sampleRadius The SSAO sample radius, in world units (SSAOTaskParams::ao).
    /// \param exportProj The projection matrix of the full image, not of a single tile.
    /// \param viewMatrix The view matrix of the camera.
    /// \param imageHeight The full image height, in pixels.
    /// \return The default tile size and a zero margin when sampleRadius is not positive.
    /// Otherwise an even margin, capped to a quarter of the maximum tile size, and a tile size
    /// grown to at least four times the margin, capped to the maximum tile size.
    TileLayout ComputeTileLayout(float sampleRadius, PXR_NS::GfMatrix4d const& exportProj,
        PXR_NS::GfMatrix4d const& viewMatrix, int imageHeight) const;

private:
    void _Reset();

    int _defaultTileSize { kDefaultTileSize };
    int _maxTileSize { kMaxTileSize };

    PXR_NS::GfVec2i _imageSize { 0, 0 };
    PXR_NS::GfVec2i _tileSize { 0, 0 };
    int _tileMargin { 0 };

    /// The tile size without the margins i.e., the pixels a tile contributes to the image.
    PXR_NS::GfVec2i _validTileSize { 0, 0 };

    unsigned int _numRows { 0 };
    unsigned int _numCols { 0 };
    unsigned int _currentIndex { 0 };
};

} // namespace HVT_NS
