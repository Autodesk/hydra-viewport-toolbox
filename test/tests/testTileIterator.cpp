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
#include <pxr/base/gf/range1f.h>

#include <gtest/gtest.h>

namespace
{

// A camera at the origin looking down -Z, used to exercise computeTileLayout.
pxr::GfCamera MakeTestCamera(pxr::GfCamera::Projection projection)
{
    pxr::GfCamera camera;
    camera.SetProjection(projection);
    camera.SetClippingRange(pxr::GfRange1f(1.0f, 100.0f));
    return camera;
}

} // namespace

// ===========================================================================
// Construction
// ===========================================================================

TEST(TestTileIterator, Constructor_DefaultLimits)
{
    TileIterator iterator;
    EXPECT_EQ(iterator.GetMaxTileSize(), pxr::GfVec2i(4096, 4096));
}

TEST(TestTileIterator, Constructor_CustomLimits)
{
    TileIterator iterator(1024, 8192);
    EXPECT_EQ(iterator.GetMaxTileSize(), pxr::GfVec2i(8192, 8192));
}

TEST(TestTileIterator, Constructor_NonPositiveSizesFallBackToClassDefaults)
{
    TileIterator iterator(0, -16);
    EXPECT_EQ(iterator.GetMaxTileSize(), pxr::GfVec2i(4096, 4096));

    // The default tile size falls back to DEFAULT_TILE_SIZE, observable through the layout
    // computed without a sample radius.
    const auto camera  = MakeTestCamera(pxr::GfCamera::Perspective);
    const auto frustum = camera.GetFrustum();
    const auto layout   = iterator.computeTileLayout(
        0.0f, frustum.ComputeProjectionMatrix(), frustum.ComputeViewMatrix(), 2048);
    EXPECT_EQ(layout.tileSize, pxr::GfVec2i(2048, 2048));
}

TEST(TestTileIterator, Constructor_DefaultTileSizeCappedToMaxTileSize)
{
    TileIterator iterator(8192, 4096);

    const auto camera  = MakeTestCamera(pxr::GfCamera::Perspective);
    const auto frustum = camera.GetFrustum();
    const auto layout  = iterator.computeTileLayout(
        0.0f, frustum.ComputeProjectionMatrix(), frustum.ComputeViewMatrix(), 2048);
    EXPECT_EQ(layout.tileSize, pxr::GfVec2i(4096, 4096));
}

// ===========================================================================
// computeTileLayout
// ===========================================================================

TEST(TestTileIterator, ComputeTileLayout_NoSampleRadiusReturnsDefaultTileSize)
{
    TileIterator iterator(512, 4096);

    const auto camera  = MakeTestCamera(pxr::GfCamera::Perspective);
    const auto frustum = camera.GetFrustum();
    const auto layout  = iterator.computeTileLayout(
        0.0f, frustum.ComputeProjectionMatrix(), frustum.ComputeViewMatrix(), 2048);

    EXPECT_EQ(layout.tileSize, pxr::GfVec2i(512, 512));
    EXPECT_EQ(layout.margin, 0);
}

TEST(TestTileIterator, ComputeTileLayout_MarginIsPositiveEvenAndCapped)
{
    TileIterator iterator(2048, 4096);

    const auto camera  = MakeTestCamera(pxr::GfCamera::Perspective);
    const auto frustum = camera.GetFrustum();
    const auto layout  = iterator.computeTileLayout(
        0.01f, frustum.ComputeProjectionMatrix(), frustum.ComputeViewMatrix(), 2048);

    EXPECT_GT(layout.margin, 0);
    EXPECT_EQ(layout.margin % 2, 0);
    EXPECT_LE(layout.margin, (4096 / 4) & ~1);
    EXPECT_EQ(layout.tileSize, pxr::GfVec2i(2048, 2048));
}

TEST(TestTileIterator, ComputeTileLayout_OrthographicCamera)
{
    TileIterator iterator(2048, 4096);

    const auto camera  = MakeTestCamera(pxr::GfCamera::Orthographic);
    const auto frustum = camera.GetFrustum();
    const auto layout  = iterator.computeTileLayout(
        0.01f, frustum.ComputeProjectionMatrix(), frustum.ComputeViewMatrix(), 2048);

    EXPECT_GT(layout.margin, 0);
    EXPECT_EQ(layout.margin % 2, 0);
    EXPECT_EQ(layout.tileSize, pxr::GfVec2i(2048, 2048));
}

// A sample radius large enough to saturate the margin cap for every limit combination below.
TEST(TestTileIterator, ComputeTileLayout_LargeSampleRadiusSaturatesMarginCap)
{
    const auto camera  = MakeTestCamera(pxr::GfCamera::Perspective);
    const auto frustum = camera.GetFrustum();
    const auto proj    = frustum.ComputeProjectionMatrix();
    const auto view    = frustum.ComputeViewMatrix();

    struct Expectation
    {
        int defaultSize;
        int maxSize;
        int expectedMargin;
        int expectedTileSize;
    };
    const Expectation expectations[] = {
        { 2048, 4096, 1024, 4096 },
        { 2048, 8192, 2048, 8192 },
        { 2048, 2048, 512, 2048 },
        { 1024, 4096, 1024, 4096 },
        // Non-power-of-two maximum exercises the even-margin masking of the cap.
        { 2048, 4100, 1024, 4096 },
    };

    for (const auto& expectation : expectations)
    {
        TileIterator iterator(expectation.defaultSize, expectation.maxSize);
        const auto layout = iterator.computeTileLayout(1000.0f, proj, view, 2048);
        EXPECT_EQ(layout.margin, expectation.expectedMargin);
        EXPECT_EQ(layout.tileSize, pxr::GfVec2i(expectation.expectedTileSize, expectation.expectedTileSize));
    }
}

// ===========================================================================
// Initialize
// ===========================================================================

TEST(TestTileIterator, Initialize_RejectsTileSizeAboveConfiguredMax)
{
    TileIterator defaultIterator;
    EXPECT_FALSE(defaultIterator.Initialize(pxr::GfVec2i(8192, 8192), 0, pxr::GfVec2i(8192, 8192)));
    EXPECT_FALSE(defaultIterator.Initialize(pxr::GfVec2i(8192, 8192), 0, pxr::GfVec2i(4096, 8192)));

    TileIterator customIterator(2048, 8192);
    EXPECT_TRUE(customIterator.Initialize(pxr::GfVec2i(8192, 8192), 0, pxr::GfVec2i(8192, 8192)));
}

TEST(TestTileIterator, Initialize_AcceptsLayoutFromComputeTileLayout)
{
    // A layout computed with custom limits must pass the validation of the same iterator, which
    // is the reason the limits are instance state.
    TileIterator iterator(2048, 8192);

    const auto camera  = MakeTestCamera(pxr::GfCamera::Perspective);
    const auto frustum = camera.GetFrustum();
    const auto layout  = iterator.computeTileLayout(
        1000.0f, frustum.ComputeProjectionMatrix(), frustum.ComputeViewMatrix(), 2048);

    EXPECT_TRUE(iterator.Initialize(pxr::GfVec2i(16384, 16384), layout.margin, layout.tileSize));
}

// ===========================================================================
// Tile iteration
// ===========================================================================

TEST(TestTileIterator, Next_VisitsEveryTile)
{
    TileIterator iterator;
    ASSERT_TRUE(iterator.Initialize(pxr::GfVec2i(100, 100), 0, pxr::GfVec2i(64, 64)));

    int rows = -1, cols = -1;
    iterator.GetNumberOfTiles(rows, cols);
    EXPECT_EQ(rows, 2);
    EXPECT_EQ(cols, 2);

    const auto& firstTile = iterator.Next();
    EXPECT_EQ(firstTile.minCoordinate, pxr::GfVec2i(0, 0));
    EXPECT_EQ(firstTile.maxCoordinate, pxr::GfVec2i(64, 64));
    EXPECT_EQ(iterator.GetCurrentIndex(), 1);

    int visited = 1;
    while (!iterator.IsDone())
    {
        iterator.Next();
        ++visited;
    }
    EXPECT_EQ(visited, 4);
}

TEST(TestTileIterator, GetTile_AppliesMarginToDisplayWindow)
{
    TileIterator iterator;
    ASSERT_TRUE(iterator.Initialize(pxr::GfVec2i(100, 100), 8, pxr::GfVec2i(64, 64)));

    EXPECT_EQ(iterator.GetTileSize(), pxr::GfVec2i(64, 64));
    EXPECT_EQ(iterator.GetTileMargin(), 8);

    // 64 - 2 * 8 = 48 usable pixels per tile: ceil(100 / 48) = 3 tiles per axis.
    int rows = 0, cols = 0;
    iterator.GetNumberOfTiles(rows, cols);
    EXPECT_EQ(rows, 3);
    EXPECT_EQ(cols, 3);

    const auto& tile = iterator.GetTile(0, 0);
    EXPECT_EQ(tile.minCoordinate, pxr::GfVec2i(0, 0));
    EXPECT_EQ(tile.maxCoordinate, pxr::GfVec2i(48, 48));
    // The tile render target pixel (0, 0) is the full image pixel (-8, -8).
    EXPECT_EQ(tile.displayWindow.GetMin(), pxr::GfVec2f(8.0f, 8.0f));
    EXPECT_EQ(tile.displayWindow.GetMax(), pxr::GfVec2f(108.0f, 108.0f));

    // Out-of-range queries return a cleared context.
    const auto& invalid = iterator.GetTile(3, 0);
    EXPECT_EQ(invalid.minCoordinate, pxr::GfVec2i(0, 0));
    EXPECT_EQ(invalid.maxCoordinate, pxr::GfVec2i(0, 0));
}
