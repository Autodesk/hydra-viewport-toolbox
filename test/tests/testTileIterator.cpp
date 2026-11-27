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

#include <RenderingFramework/TestFlags.h>

#include <hvt/engine/taskManager.h>
#include <hvt/engine/tileIterator.h>
#include <hvt/engine/viewportEngine.h>

#include <hvt/tasks/screenSpaceKernelEffect.h>

#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/path.h>

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string>

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

// A minimal task reporting, through the ScreenSpaceKernelEffect interface, the margin held
// by its parameters i.e., an int value.
class FakeEffectTask : public HdTask, public hvt::ScreenSpaceKernelEffect
{
public:
    FakeEffectTask(HdSceneDelegate* /* delegate */, SdfPath const& uid) : HdTask(uid) {}

    void Sync(
        HdSceneDelegate* /* delegate */, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits) override
    {
        *dirtyBits = HdChangeTracker::Clean;
    }
    void Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */) override {}
    void Execute(HdTaskContext* /* ctx */) override {}

    int ComputeRequiredMargin(hvt::ScreenSpaceKernelEffect::MarginContext const& /* context */,
        VtValue const& params) const override
    {
        return params.IsHolding<int>() ? params.UncheckedGet<int>() : 0;
    }
};

// A minimal task that is not a post-processing effect, so it must be ignored.
class FakePlainTask : public HdTask
{
public:
    FakePlainTask(HdSceneDelegate* /* delegate */, SdfPath const& uid) : HdTask(uid) {}

    void Sync(
        HdSceneDelegate* /* delegate */, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits) override
    {
        *dirtyBits = HdChangeTracker::Clean;
    }
    void Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */) override {}
    void Execute(HdTaskContext* /* ctx */) override {}
};

// A task manager on a Storm render index, to test the task-driven Initialize.
struct TaskManagerFixture
{
    std::shared_ptr<TestHelpers::TestContext> testContext;
    hvt::RenderIndexProxyPtr renderIndexProxy;
    std::unique_ptr<hvt::TaskManager> taskManager;

    TaskManagerFixture()
    {
        testContext = TestHelpers::CreateTestContext();

        hvt::RendererDescriptor rendererDesc;
        rendererDesc.hgiDriver    = &testContext->_backend->hgiDriver();
        rendererDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndexProxy, rendererDesc);

        taskManager = std::make_unique<hvt::TaskManager>(
            SdfPath("/TestTileIterator"), renderIndexProxy->RenderIndex());
    }

    ~TaskManagerFixture() { taskManager = nullptr; }

    // Adds a fake effect task reporting the given margin.
    SdfPath AddEffect(std::string const& name, int margin)
    {
        return taskManager->AddTask<FakeEffectTask>(TfToken(name), margin, nullptr);
    }
};

} // namespace

// ===========================================================================
// Construction
// ===========================================================================

TEST(TestTileIterator, Constructor_AppliesTheConfiguredMaxTileSize)
{
    // The class default, a custom limit, and the fallback for a non-positive one.
    EXPECT_EQ(hvt::TileIterator().GetMaxTileSize(), GfVec2i(4096, 4096));
    EXPECT_EQ(hvt::TileIterator(8192).GetMaxTileSize(), GfVec2i(8192, 8192));
    EXPECT_EQ(hvt::TileIterator(-16).GetMaxTileSize(), GfVec2i(4096, 4096));
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
// Task-driven Initialize
// ===========================================================================

HVT_TEST(TestTileIterator, Initialize_SumsTheEffectMargins)
{
    TaskManagerFixture f;

    hvt::TileIterator iterator;

    // The image to tile is part of the view context.
    hvt::ScreenSpaceKernelEffect::MarginContext context;
    context.imageResolution = GfVec2i(100, 100);

    // Without effect tasks, the margin stays zero.
    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(64, 64), context));
    EXPECT_EQ(iterator.GetTileMargin(), 0);
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(64, 64));
    EXPECT_EQ(iterator.GetNumberOfTiles(), 4u);

    // An odd hint rounds down to an even tile size, and the smallest positive one rounds up,
    // keeping the 2x2 pixel quads of the effects aligned between tiles.
    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(255, 1), context));
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(254, 2));

    // An effect reads the output of the previous ones, so the margins add up: 3 + 10 = 13,
    // rounded up to 14. Tasks that are not effects are ignored, and a negative effect margin
    // counts as zero instead of rejecting the tiling, or reducing the other margins.
    f.taskManager->AddTask<FakePlainTask>(TfToken("plain"), 0, nullptr);
    f.AddEffect("effect1", 3);
    f.AddEffect("effect2", 10);
    f.AddEffect("negative", -5);
    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(64, 64), context));

    EXPECT_EQ(iterator.GetTileMargin(), 14);
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(64, 64));

    // 64 - 2 * 14 = 36 valid pixels per tile: ceil(100 / 36) = 3 tiles per axis.
    EXPECT_EQ(iterator.GetNumberOfRows(), 3u);
    EXPECT_EQ(iterator.GetNumberOfColumns(), 3u);
}

HVT_TEST(TestTileIterator, Initialize_CapsTheTaskMarginAndGrowsTheTileSize)
{
    TaskManagerFixture f;
    const SdfPath effect = f.AddEffect("effect", 100000);

    // The margin cap is a quarter of the maximum tile size, rounded down to an even value.
    hvt::TileIterator iterator(256);

    hvt::ScreenSpaceKernelEffect::MarginContext context;
    context.imageResolution = GfVec2i(1000, 1000);

    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(100, 100), context));

    // The margin is capped at (256 / 4) & ~1 = 64, and the tile size grows to 4 * 64 = 256.
    EXPECT_EQ(iterator.GetTileMargin(), 64);
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(256, 256));

    // A smaller margin still grows the tile size to four times the margin.
    ASSERT_TRUE(f.taskManager->SetTaskValue(effect, HdTokens->params, VtValue(30)));
    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(100, 100), context));
    EXPECT_EQ(iterator.GetTileMargin(), 30);
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(120, 120));

    // An odd hint rounds down to an even tile size, keeping the 2x2 pixel quads of the effects
    // aligned between tiles.
    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(255, 100), context));
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(254, 120));

    // The sum of huge margins must not overflow into a negative, or small, margin: it saturates
    // at the same cap.
    f.AddEffect("huge1", std::numeric_limits<int>::max());
    f.AddEffect("huge2", std::numeric_limits<int>::max());
    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(100, 100), context));
    EXPECT_EQ(iterator.GetTileMargin(), 64);

    // A hint larger than the maximum tile size is clamped instead of being rejected.
    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(1000, 1000), context));
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(256, 256));
}

HVT_TEST(TestTileIterator, Initialize_RejectsInvalidTaskDrivenArguments)
{
    TaskManagerFixture f;
    const SdfPath effect = f.AddEffect("effect", 8);

    hvt::TileIterator iterator;
    hvt::ScreenSpaceKernelEffect::MarginContext context;

    TfErrorMark mark;

    // The image resolution comes from the view context, so an empty one is invalid.
    EXPECT_FALSE(iterator.Initialize(*f.taskManager, GfVec2i(64, 64), context));
    EXPECT_FALSE(mark.IsClean());
    mark.Clear();

    // A non-positive hint grows to four times the effect margin, so it only stays invalid when
    // no effect asks for a margin.
    context.imageResolution = GfVec2i(100, 100);
    ASSERT_TRUE(iterator.Initialize(*f.taskManager, GfVec2i(0, 64), context));
    EXPECT_EQ(iterator.GetTileSize(), GfVec2i(32, 64));

    // A disabled effect is ignored, so the non-positive hint stays invalid.
    f.taskManager->EnableTask(effect, false);
    EXPECT_FALSE(iterator.Initialize(*f.taskManager, GfVec2i(0, 64), context));
    EXPECT_FALSE(mark.IsClean());
    mark.Clear();

    EXPECT_EQ(iterator.GetNumberOfTiles(), 0u);
    EXPECT_TRUE(iterator.IsDone());
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

TEST(TestTileIterator, GetTile_ReturnsTheTileAtTheCoordinates)
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

    // The last row and column are clamped to the image.
    const auto lastTile = iterator.GetTile(2, 2);
    EXPECT_EQ(lastTile.minCoordinate, GfVec2i(96, 96));
    EXPECT_EQ(lastTile.maxCoordinate, GfVec2i(100, 100));
    EXPECT_EQ(lastTile.displayWindow.GetMin(), GfVec2f(-88.0f, -88.0f));
    EXPECT_EQ(lastTile.displayWindow.GetMax(), GfVec2f(12.0f, 12.0f));

    // Out-of-range coordinates return the empty tile. A negative index converts to a large
    // unsigned one, which is out of range too.
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(3, 0)));
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(0, 3)));
    const int negativeIndex = -1;
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(static_cast<unsigned int>(negativeIndex), 0)));
    EXPECT_TRUE(IsEmptyTile(iterator.GetTile(0, static_cast<unsigned int>(negativeIndex))));
}
