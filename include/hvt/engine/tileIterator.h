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

#include <memory>

struct HVT_API TileContext
{
    pxr::GfVec2i minCoordinate;
    pxr::GfVec2i maxCoordinate;
    pxr::GfRange2f displayWindow;
};

class HVT_API TileIterator
{
public:
    static const int DEFAULT_TILE_SIZE = 2048;
    static const int MAX_TILE_SIZE     = 4096;

    explicit TileIterator(int defaultTileSize = DEFAULT_TILE_SIZE, int maxTileSize = MAX_TILE_SIZE);
    ~TileIterator();

    bool Initialize(const pxr::GfVec2i& imageResolution,
        unsigned int tileMargin, const pxr::GfVec2i& tileSize);

    const TileContext& Next();
    bool IsDone() const { return currentIndex >= nTiles; }

    void GetNumberOfTiles(int& nRows, int& nCols) const;
    const TileContext& GetTile(int row, int col);

    int GetCurrentIndex() const { return currentIndex; }
    pxr::GfVec2i GetMaxTileSize() const;
    pxr::GfVec2i GetTileSize() const { return mTileSize; }
    int GetTileMargin() const { return mTileMargin; }

    struct TileLayout
    {
        pxr::GfVec2i tileSize;
        int margin = 0;
    };

    // Computes the tile size and margin for a tiled export, honoring the tile size limits
    // configured at construction.
    TileLayout computeTileLayout(float sampleRadius, const pxr::GfMatrix4d& exportProj,
        const pxr::GfMatrix4d& viewMatrix, int nImageHeight) const;

private:
    std::unique_ptr<TileContext> tileContext;

    pxr::GfVec2i mTileSize;

    int nRows;
    int nCols;
    int nTiles;
    int currentIndex;

    int* xCoord;
    int* yCoord;
    float* norXCoord;
    float* norYCoord;
    pxr::GfVec2i fullImageSize;

    int mTileMargin;
    float norXTileMargin;
    float norYTileMargin;

    int mDefaultTileSize;
    int mMaxTileSize;

    void ConstructTile(
        const pxr::GfVec2i& imageResolution, unsigned int tileMargin, const pxr::GfVec2i& tileSize);
    void ClearTileContext();
};
