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

#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/tf/errorMark.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

// Returns true when the tile is the empty tile returned for out-of-range queries.
bool IsEmptyTile(hvt::TileContext const& tile)
{
    return tile.minCoordinate == GfVec2i(0, 0) && tile.maxCoordinate == GfVec2i(0, 0) &&
        tile.displayWindow.IsEmpty();
}

// Expects Initialize to fail with a coding error.
void ExpectInitializeCodingError(hvt::TileIterator& iterator, GfVec2i const& imageResolution,
    int tileMargin, GfVec2i const& tileSize)
{
    TfErrorMark mark;
    EXPECT_FALSE(iterator.Initialize(imageResolution, tileMargin, tileSize));
    EXPECT_FALSE(mark.IsClean());
    mark.Clear();

    EXPECT_EQ(iterator.GetNumberOfTiles(), 0u);
    EXPECT_TRUE(iterator.IsDone());
}

} // namespace

// ===========================================================================
// Construction
// ===========================================================================

TEST(TestTileIterator, Constructor_DefaultLimits)
{
    hvt::TileIterator iterator;
    EXPECT_EQ(iterator.GetMaxTileSize(), GfVec2i(4096, 4096));
}

TEST(TestTileIterator, Constructor_CustomLimits)
{
    hvt::TileIterator iterator(8192);
    EXPECT_EQ(iterator.GetMaxTileSize(), GfVec2i(8192, 8192));
}

TEST(TestTileIterator, Constructor_NonPositiveSizeFallsBackToClassDefault)
{
    hvt::TileIterator iterator(-16);
    EXPECT_EQ(iterator.GetMaxTileSize(), GfVec2i(4096, 4096));
}

TEST(TestTileIterator, Constructor_IsEmptyBeforeInitialize)
{
    hvt::TileIterator iterator;

    EXPECT_EQ(iterator.GetNumberOfTiles(), 0u);
    EXPECT_TRUE(iterator.IsDone());
    EXPECT_TRUE(IsEmptyTile(iterator.Next()));
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(0, 0)));
    EXPECT_EQ(iterator.GetCurrentIndex(), 0u);
}

// ===========================================================================
// Initialize
// ===========================================================================

TEST(TestTileIterator, Initialize_RejectsTileSizeAboveConfiguredMax)
{
    hvt::TileIterator defaultIterator;
    EXPECT_FALSE(defaultIterator.Initialize(GfVec2i(8192, 8192), 0, GfVec2i(8192, 8192)));
    EXPECT_FALSE(defaultIterator.Initialize(GfVec2i(8192, 8192), 0, GfVec2i(4096, 8192)));
    EXPECT_TRUE(defaultIterator.IsDone());

    hvt::TileIterator customIterator(8192);
    EXPECT_TRUE(customIterator.Initialize(GfVec2i(8192, 8192), 0, GfVec2i(8192, 8192)));
}

TEST(TestTileIterator, Initialize_RejectsInvalidArguments)
{
    hvt::TileIterator iterator;

    // Non-positive image resolution.
    ExpectInitializeCodingError(iterator, GfVec2i(0, 100), 0, GfVec2i(64, 64));
    ExpectInitializeCodingError(iterator, GfVec2i(100, -1), 0, GfVec2i(64, 64));

    // Non-positive tile size.
    ExpectInitializeCodingError(iterator, GfVec2i(100, 100), 0, GfVec2i(0, 64));
    ExpectInitializeCodingError(iterator, GfVec2i(100, 100), 0, GfVec2i(64, -64));

    // Negative margin.
    ExpectInitializeCodingError(iterator, GfVec2i(100, 100), -1, GfVec2i(64, 64));

    // Margins leaving no valid pixel, on either axis.
    ExpectInitializeCodingError(iterator, GfVec2i(100, 100), 32, GfVec2i(64, 64));
    ExpectInitializeCodingError(iterator, GfVec2i(100, 100), 16, GfVec2i(64, 32));

    // The largest valid margin leaves one valid pixel per axis.
    EXPECT_TRUE(iterator.Initialize(GfVec2i(4, 2), 1, GfVec2i(3, 3)));
    EXPECT_EQ(iterator.GetNumberOfColumns(), 4u);
    EXPECT_EQ(iterator.GetNumberOfRows(), 2u);
}

TEST(TestTileIterator, Initialize_FailureEmptiesThePreviousTiling)
{
    hvt::TileIterator iterator;
    ASSERT_TRUE(iterator.Initialize(GfVec2i(100, 100), 0, GfVec2i(64, 64)));
    ASSERT_EQ(iterator.GetNumberOfTiles(), 4u);

    ExpectInitializeCodingError(iterator, GfVec2i(100, 100), 40, GfVec2i(64, 64));
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(0, 0)));
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(0, 0));
    EXPECT_EQ(iterator.GetTileMargin(), 0);
}

TEST(TestTileIterator, Initialize_TwiceRestartsWithTheNewTiling)
{
    hvt::TileIterator iterator;
    ASSERT_TRUE(iterator.Initialize(GfVec2i(100, 100), 0, GfVec2i(64, 64)));
    iterator.Next();
    iterator.Next();
    ASSERT_EQ(iterator.GetCurrentIndex(), 2u);

    ASSERT_TRUE(iterator.Initialize(GfVec2i(300, 100), 8, GfVec2i(64, 64)));
    EXPECT_EQ(iterator.GetCurrentIndex(), 0u);
    EXPECT_FALSE(iterator.IsDone());
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(64, 64));
    EXPECT_EQ(iterator.GetTileMargin(), 8);

    // 48 valid pixels per tile: ceil(300 / 48) = 7 columns, ceil(100 / 48) = 3 rows.
    EXPECT_EQ(iterator.GetNumberOfColumns(), 7u);
    EXPECT_EQ(iterator.GetNumberOfRows(), 3u);
    EXPECT_EQ(iterator.GetNumberOfTiles(), 21u);

    const auto lastTile = iterator.GetTile(2, 6);
    EXPECT_EQ(lastTile.minCoordinate, GfVec2i(288, 96));
    EXPECT_EQ(lastTile.maxCoordinate, GfVec2i(300, 100));
}

// ===========================================================================
// Tile iteration
// ===========================================================================

TEST(TestTileIterator, Next_VisitsEveryTileRowByRow)
{
    hvt::TileIterator iterator;
    ASSERT_TRUE(iterator.Initialize(GfVec2i(100, 100), 0, GfVec2i(64, 64)));

    EXPECT_EQ(iterator.GetNumberOfRows(), 2u);
    EXPECT_EQ(iterator.GetNumberOfColumns(), 2u);

    const GfVec2i expectedMins[] = { { 0, 0 }, { 64, 0 }, { 0, 64 }, { 64, 64 } };
    for (unsigned int index = 0; index < 4; ++index)
    {
        ASSERT_FALSE(iterator.IsDone());
        EXPECT_EQ(iterator.GetCurrentIndex(), index);
        EXPECT_EQ(iterator.Next().minCoordinate, expectedMins[index]);
    }

    EXPECT_TRUE(iterator.IsDone());
    EXPECT_TRUE(IsEmptyTile(iterator.Next()));
    EXPECT_EQ(iterator.GetCurrentIndex(), 4u);
}

TEST(TestTileIterator, GetTile_AppliesMarginToDisplayWindow)
{
    hvt::TileIterator iterator;
    ASSERT_TRUE(iterator.Initialize(GfVec2i(100, 100), 8, GfVec2i(64, 64)));

    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(64, 64));
    EXPECT_EQ(iterator.GetTileMargin(), 8);

    // 64 - 2 * 8 = 48 usable pixels per tile: ceil(100 / 48) = 3 tiles per axis.
    EXPECT_EQ(iterator.GetNumberOfRows(), 3u);
    EXPECT_EQ(iterator.GetNumberOfColumns(), 3u);

    // The tile pixel (8, 8) is the full-image pixel (0, 0), so the display window starts at
    // (margin - x, margin - y) = (8, 8), in the y-down convention of CameraUtilFraming.
    const auto tile = iterator.GetTile(0, 0);
    EXPECT_EQ(tile.minCoordinate, GfVec2i(0, 0));
    EXPECT_EQ(tile.maxCoordinate, GfVec2i(48, 48));
    EXPECT_EQ(tile.displayWindow.GetMin(), GfVec2f(8.0f, 8.0f));
    EXPECT_EQ(tile.displayWindow.GetMax(), GfVec2f(108.0f, 108.0f));

    // A different row and column gives a different offset on each axis: (8 - 96, 8 - 48).
    const auto tileRow1 = iterator.GetTile(1, 2);
    EXPECT_EQ(tileRow1.minCoordinate, GfVec2i(96, 48));
    EXPECT_EQ(tileRow1.maxCoordinate, GfVec2i(100, 96));
    EXPECT_EQ(tileRow1.displayWindow.GetMin(), GfVec2f(-88.0f, -40.0f));
    EXPECT_EQ(tileRow1.displayWindow.GetMax(), GfVec2f(12.0f, 60.0f));
}

TEST(TestTileIterator, GetTile_ClampsTheLastRowAndColumnToTheImage)
{
    hvt::TileIterator iterator;
    ASSERT_TRUE(iterator.Initialize(GfVec2i(100, 100), 8, GfVec2i(64, 64)));

    const auto tile = iterator.GetTile(2, 2);
    EXPECT_EQ(tile.minCoordinate, GfVec2i(96, 96));
    EXPECT_EQ(tile.maxCoordinate, GfVec2i(100, 100));
    EXPECT_EQ(tile.displayWindow.GetMin(), GfVec2f(-88.0f, -88.0f));
    EXPECT_EQ(tile.displayWindow.GetMax(), GfVec2f(12.0f, 12.0f));
}

TEST(TestTileIterator, GetTile_OutOfRangeReturnsAnEmptyTile)
{
    hvt::TileIterator iterator;
    ASSERT_TRUE(iterator.Initialize(GfVec2i(100, 100), 8, GfVec2i(64, 64)));

    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(3, 0)));
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(0, 3)));

    // A negative index converts to a large unsigned one, which is out of range too.
    const int negativeIndex = -1;
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(static_cast<unsigned int>(negativeIndex), 0)));
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(0, static_cast<unsigned int>(negativeIndex))));
}
