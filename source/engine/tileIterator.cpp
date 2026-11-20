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

#include <hvt/engine/taskManager.h>
#include <hvt/tasks/kernelPostProcessingEffect.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/path.h>

#include <algorithm>
#include <limits>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

TileIterator::TileIterator(int maxTileSize)
{
    // Non-positive sizes fall back to the class defaults.
    _maxTileSize = maxTileSize > 0 ? maxTileSize : kMaxTileSize;
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

bool TileIterator::Initialize(GfVec2i const& imageResolution, TaskManager& taskManager,
    GfVec2i const& tileSizeHint, KernelPostProcessingEffect::MarginContext const& view)
{
    // Ask each post-processing effect for the margin it needs, with its parameters as stored in
    // the task manager. An effect reads the output of the previous ones, so their reaches add up.
    SdfPathVector taskPaths;
    taskManager.GetTaskPaths(TaskFlagsBits::kExecutableBit, /*ignoreDisabled=*/true, taskPaths);

    int requiredMargin = 0;
    for (SdfPath const& taskPath : taskPaths)
    {
        HdTaskSharedPtr const task = taskManager.GetTask(taskPath);
        if (KernelPostProcessingEffect const* effect =
                dynamic_cast<KernelPostProcessingEffect const*>(task.get()))
        {
            const VtValue params = taskManager.GetTaskValue(taskPath, HdTokens->params);
            const int margin     = std::max(0, effect->GetMargin(view, params));
            // Saturates instead of overflowing on huge margins.
            requiredMargin =
                std::min(requiredMargin, std::numeric_limits<int>::max() - margin) + margin;
        }
    }

    // Cap the margin so a tile keeps at least half its pixels valid.
    const int marginCap = (_maxTileSize / 4) & ~1;
    if (requiredMargin > marginCap)
    {
        TF_WARN(
            "TileIterator: the post-processing effects need a %d pixel tile margin, capped to "
            "%d pixels by the maximum tile size %d; the effects may show seams at the tile "
            "borders.",
            requiredMargin, marginCap, _maxTileSize);
    }

    // An even margin keeps the tile pixel parity for effects working on 2x2 pixel quads (SSAO).
    int tileMargin = std::min(requiredMargin, marginCap);
    tileMargin     = std::min(tileMargin + (tileMargin & 1), marginCap);

    // Grow the tile size to give the margin four times its size in valid pixels. The hint is
    // clamped to the maximum tile size, instead of being rejected.
    GfVec2i tileSize = tileSizeHint;
    tileSize[0]      = std::min(std::max(tileSize[0], 4 * tileMargin), _maxTileSize);
    tileSize[1]      = std::min(std::max(tileSize[1], 4 * tileMargin), _maxTileSize);

    return Initialize(imageResolution, tileMargin, tileSize);
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
