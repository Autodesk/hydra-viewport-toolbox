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

#include <hvt/tasks/screenSpaceKernelEffect.h>

#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/vec2i.h>

namespace HVT_NS
{

class TaskManager;

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
    /// The default maximum tile size, in pixels.
    static constexpr int kMaxTileSize = 4096;

    /// Creates an iterator with the given maximum tile size.
    /// \param maxTileSize The largest tile size Initialize accepts, in pixels.
    /// \note A non-positive size falls back to the class default.
    explicit TileIterator(int maxTileSize = kMaxTileSize);

    /// Splits the image into tiles and restarts the iteration.
    /// \param imageResolution The full image size, in pixels.
    /// \param tileMargin The margin on every side of a tile, in pixels.
    /// \param tileSize The tile render buffer size including the margins, in pixels.
    /// \return False, leaving the iterator empty, when the tile size exceeds the maximum tile
    /// size, or when the arguments are invalid (a coding error is then posted).
    bool Initialize(
        PXR_NS::GfVec2i const& imageResolution, int tileMargin, PXR_NS::GfVec2i const& tileSize);

    /// Splits the image into tiles and restarts the iteration, sizing the margin and the tiles for
    /// the post-processing effects of the given task manager.
    /// \param imageResolution The full image size, in pixels.
    /// \param taskManager The task manager holding the effects, and their parameters.
    /// \param tileSizeHint The desired tile render buffer size, in pixels.
    /// \param view The view the tiles will be rendered with, given to each effect.
    /// \return False, leaving the iterator empty, when the arguments are invalid (a coding error
    /// is then posted).
    /// \note The task commit functions are not run, so values they set are not seen before the
    /// first render.
    bool Initialize(PXR_NS::GfVec2i const& imageResolution, TaskManager& taskManager,
        PXR_NS::GfVec2i const& tileSizeHint, ScreenSpaceKernelEffect::MarginContext const& view);

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

private:
    void _Reset();

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
