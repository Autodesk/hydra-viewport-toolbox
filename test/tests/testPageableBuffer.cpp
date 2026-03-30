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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

// Include USD types for tests
#include <pxr/pxr.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>

// Include the appropriate test context declaration.
#include <RenderingFramework/TestContextCreator.h>

// Include paging system
#include <hvt/pageableBuffer/pageableBuffer.h>
#include <hvt/pageableBuffer/pageableBufferManager.h>
#include <hvt/pageableBuffer/pageableConcepts.h>
#include <hvt/pageableBuffer/pageableDataSource.h>
#include <hvt/pageableBuffer/pageableMemoryMonitor.h>
#include <hvt/pageableBuffer/pageableRetainedDataSource.h>
#include <hvt/pageableBuffer/pageableStrategies.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

PXR_NAMESPACE_USING_DIRECTIVE

// VtValue/valueRef requires == for data sources. However, <compare> requires MSVC C++20 or later.
// This should be removed when we upgrade to C++20.
#if defined(_MSC_VER) && (!defined(_MSVC_LANG) || _MSVC_LANG < 202002L)
#include <ostream>
#include <type_traits>
PXR_NAMESPACE_OPEN_SCOPE
template <typename T, std::enable_if_t<std::is_base_of<HdDataSourceBase, T>::value, int> = 0>
inline bool operator==(const T& lhs, const T& rhs) noexcept
{
    return &lhs == &rhs;
}
template <typename T, std::enable_if_t<std::is_base_of<HdDataSourceBase, T>::value, int> = 0>
inline bool operator!=(const T& lhs, const T& rhs) noexcept
{
    return !(lhs == rhs);
}
PXR_NAMESPACE_CLOSE_SCOPE

// To fix a consequent build error, we have to suppress GoogleTest printing for problematic USD types.
namespace testing {
namespace internal {
// Specialize UniversalPrinter to avoid calling PrintTo for problematic types
template<>
class UniversalPrinter<PXR_NS::HdDataSourceBase> {
public:
    static void Print(const PXR_NS::HdDataSourceBase&, ::std::ostream* os) {
        *os << "HdDataSourceBase@" << static_cast<const void*>(&os);
    }
};

template<>
class UniversalPrinter<PXR_NS::HdContainerDataSource> {
public:
    static void Print(const PXR_NS::HdContainerDataSource&, ::std::ostream* os) {
        *os << "HdContainerDataSource@" << static_cast<const void*>(&os);
    }
};

template<>
class UniversalPrinter<PXR_NS::HdSampledDataSource> {
public:
    static void Print(const PXR_NS::HdSampledDataSource&, ::std::ostream* os) {
        *os << "HdSampledDataSource@" << static_cast<const void*>(&os);
    }
};

template<>
class UniversalPrinter<PXR_NS::HdBlockDataSource> {
public:
    static void Print(const PXR_NS::HdBlockDataSource&, ::std::ostream* os) {
        *os << "HdBlockDataSource@" << static_cast<const void*>(&os);
    }
};

template<>
class UniversalPrinter<PXR_NS::HdVectorDataSource> {
public:
    static void Print(const PXR_NS::HdVectorDataSource&, ::std::ostream* os) {
        *os << "HdVectorDataSource@" << static_cast<const void*>(&os);
    }
};
}
}
#else
#include <compare>
#endif

#if !defined(NDEBUG)
//#define ENABLE_PAGE_ANALYSIS
#endif

TEST(TestPageableBuffer, BasicPageableBuffer)
{
    // Initialize buffer manager with test configuration
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_test_pages";
    desc.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 256 * hvt::ONE_MiB;
    desc.ageLimit            = 20;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create some buffers
    auto buffer1 = bufferManager.CreateBuffer(PXR_NS::SdfPath("/VertexBuffer1"), 50 * hvt::ONE_MiB);
    auto buffer2 = bufferManager.CreateBuffer(PXR_NS::SdfPath("/IndexBuffer1"), 30 * hvt::ONE_MiB);
    auto buffer3 =
        bufferManager.CreateBuffer(PXR_NS::SdfPath("/TextureBuffer1"), 100 * hvt::ONE_MiB);

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
#endif

    // Move buffers to scene memory
    EXPECT_TRUE(buffer1->SwapToSceneMemory());
    EXPECT_TRUE(buffer2->SwapToSceneMemory());
    EXPECT_TRUE(buffer3->SwapToSceneMemory());

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
#endif

    // Move buffers to renderer memory
    EXPECT_TRUE(buffer1->SwapToRendererMemory());
    EXPECT_TRUE(buffer2->SwapToRendererMemory());
    EXPECT_TRUE(buffer3->SwapToRendererMemory());

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
#endif

    // Simulate memory pressure by creating more buffers
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> extraBuffers;
    for (int i = 0; i < 10; ++i)
    {
        std::string bufferName = "/Extra" + std::to_string(i);
        auto buffer            = bufferManager.CreateBuffer(PXR_NS::SdfPath(bufferName), 80 * hvt::ONE_MiB);
        EXPECT_TRUE(buffer->PageToRendererMemory());
        extraBuffers.push_back(buffer);
    }

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
#endif

    // Trigger free crawl and check 90% of buffers
    bufferManager.FreeCrawl(90.0f);

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
    bufferManager.PrintCacheStats();
#endif
    GTEST_SUCCEED();
}

TEST(TestPageableBuffer, MemoryPressure)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory = std::filesystem::temp_directory_path() / "hvt_test_pages";
    // Set lower memory limits to trigger pressure quickly
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.ageLimit            = 20;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create buffers until we hit memory pressure
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
    for (int i = 0; i < 20; ++i)
    {
        std::string bufferName = "/Buffer" + std::to_string(i);
        auto buffer            = bufferManager.CreateBuffer(PXR_NS::SdfPath(bufferName), 30 * hvt::ONE_MiB);

        // Age some buffers
        if (i < 10)
        {
            buffer->UpdateFrameStamp(bufferManager.GetCurrentFrame() - 25); // Make them old
        }

        EXPECT_TRUE(buffer->SwapToRendererMemory());
        buffers.push_back(buffer);

        // Check pressure after each buffer
        float rendererPressure = bufferManager.GetMemoryMonitor()->GetRendererMemoryPressure();
        float scenePressure    = bufferManager.GetMemoryMonitor()->GetSceneMemoryPressure();
#ifdef ENABLE_PAGE_ANALYSIS
        std::cout << "Buffer " << i << " - Renderer: " << (rendererPressure * 100)
                  << "%, Scene: " << (scenePressure * 100) << "%\n";
#endif

        // Trigger paging when pressures get high
        if (rendererPressure > hvt::HdMemoryMonitor::RENDERER_PAGING_THRESHOLD ||
            scenePressure > hvt::HdMemoryMonitor::SCENE_PAGING_THRESHOLD)
        {
            bufferManager.FreeCrawl(50.0f);
        }

        bufferManager.AdvanceFrame();
    }

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
    bufferManager.PrintCacheStats();
#endif
    GTEST_SUCCEED();
}

TEST(TestPageableBuffer, AsyncOperations)
{
    // Create BufferManager with built-in ThreadPool
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_test_pages";
    desc.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 256 * hvt::ONE_MiB;
    desc.ageLimit            = 20;
    desc.numThreads          = 4;
    hvt::DefaultBufferManager bufferManager(desc);

    // Create some test buffers
    auto buffer1 = bufferManager.CreateBuffer(PXR_NS::SdfPath("/AsyncBuffer1"), 50 * hvt::ONE_MiB);
    auto buffer2 = bufferManager.CreateBuffer(PXR_NS::SdfPath("/AsyncBuffer2"), 30 * hvt::ONE_MiB);
    auto buffer3 = bufferManager.CreateBuffer(PXR_NS::SdfPath("/AsyncBuffer3"), 40 * hvt::ONE_MiB);

    // --- Testing Async Paging Operations ---

    // Start async operations and get futures
    auto future1 = bufferManager.PageToSceneMemoryAsync(buffer1);
    auto future2 = bufferManager.PageToRendererMemoryAsync(buffer2);
    auto future3 = bufferManager.PageToDiskAsync(buffer3);

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Pending operations: " << bufferManager.GetPendingOperations() << "\n\n";
#endif

    // Do other work while operations are running......
    for (int i = 0; i < 5; ++i)
    {
#ifdef ENABLE_PAGE_ANALYSIS
        std::cout << "  Work iteration " << (i + 1)
                  << ", pending: " << bufferManager.GetPendingOperations() << "\n";
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Wait for specific operations to complete
    EXPECT_TRUE(future1.get());
    EXPECT_TRUE(future2.get());
    EXPECT_TRUE(future3.get());

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
#endif

    // --- Testing Async Swapping Operations ---

    // Start more async operations
    auto swapFuture1 = bufferManager.SwapSceneToDiskAsync(buffer1);
    auto swapFuture2 = bufferManager.SwapRendererToDiskAsync(buffer2);

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Pending operations: " << bufferManager.GetPendingOperations() << "\n\n";
#endif

    // Wait for all operations to complete
    bufferManager.WaitForAllOperations();
    EXPECT_EQ(bufferManager.GetPendingOperations(), 0);
    EXPECT_TRUE(swapFuture1.get());
    EXPECT_TRUE(swapFuture2.get());

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
#endif

    // --- Testing Async Release Operations ---

    // Start async release operations (these return void futures)
    auto releaseFuture1 = bufferManager.ReleaseSceneBufferAsync(buffer1);
    auto releaseFuture2 = bufferManager.ReleaseRendererBufferAsync(buffer2);
    auto releaseFuture3 = bufferManager.ReleaseDiskPageAsync(buffer3);

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Pending operations: " << bufferManager.GetPendingOperations() << "\n";
#endif

    // Wait for release operations
    releaseFuture1.wait();
    releaseFuture2.wait();
    releaseFuture3.wait();
    EXPECT_EQ(bufferManager.GetPendingOperations(), 0);

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
    bufferManager.PrintCacheStats();
#endif
    GTEST_SUCCEED();
}

// INTEGRATED DEMO:
// Demonstrating how async operations are seamlessly integrated into FreeCrawl,
// and how users can call FreeCrawlAsync() to free buffers asynchronously.
// These are the suggested usages.
TEST(TestPageableBuffer, PagingStrategy)
{
    // Create BufferManager with default strategies
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_test_pages";
    desc.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 256 * hvt::ONE_MiB;
    desc.ageLimit            = 10;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create test buffers with different characteristics using factory
    auto path1 = PXR_NS::SdfPath("/SmallBuffer");
    auto path2 = PXR_NS::SdfPath("/MediumBuffer");
    auto path3 = PXR_NS::SdfPath("/LargeBuffer");
    auto path4 = PXR_NS::SdfPath("/HugeBuffer");
    auto path5 = PXR_NS::SdfPath("/DynamicBuffer");
    auto path6 = PXR_NS::SdfPath("/OldBuffer");
    auto path7 = PXR_NS::SdfPath("/VeryOldBuffer");
    auto buffer1 = bufferManager.CreateBuffer(
        path1, 20 * hvt::ONE_MiB, hvt::HdBufferUsage::Static);
    auto buffer2 = bufferManager.CreateBuffer(
        path2, 50 * hvt::ONE_MiB, hvt::HdBufferUsage::Static);
    auto buffer3 = bufferManager.CreateBuffer(
        path3, 100 * hvt::ONE_MiB, hvt::HdBufferUsage::Static);
    auto buffer4 = bufferManager.CreateBuffer(
        path4, 200 * hvt::ONE_MiB, hvt::HdBufferUsage::Static);
    auto buffer5 = bufferManager.CreateBuffer(
        path5, 75 * hvt::ONE_MiB, hvt::HdBufferUsage::Dynamic);
    auto buffer6 = bufferManager.CreateBuffer(
        path6, 30 * hvt::ONE_MiB, hvt::HdBufferUsage::Static);
    auto buffer7 = bufferManager.CreateBuffer(
        path7, 40 * hvt::ONE_MiB, hvt::HdBufferUsage::Static);

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
    bufferManager.PrintCacheStats();
#endif

    // Simulate the buffers by "rendering" 25 frames
    for (int i = 0; i < 25; ++i)
    {
        bufferManager.AdvanceFrame();
        // Update some buffer timestamps to create different ages
        if (i % 5 == 0)
        {
            buffer1->UpdateFrameStamp(bufferManager.GetCurrentFrame());
            buffer2->UpdateFrameStamp(bufferManager.GetCurrentFrame());
        }
    }

    // Some buffers should be now eligible for disposal based on age.
    EXPECT_EQ(bufferManager.GetCurrentFrame(), 25);

    // Create memory pressure by swapping buffers to different memory tiers.
    EXPECT_TRUE(buffer1->SwapToSceneMemory());
    EXPECT_TRUE(buffer2->SwapToSceneMemory());
    EXPECT_TRUE(buffer3->SwapToRendererMemory());
    EXPECT_TRUE(buffer4->SwapToRendererMemory());
    EXPECT_TRUE(buffer5->SwapToSceneMemory());
    EXPECT_TRUE(buffer6->SwapToRendererMemory());
    EXPECT_TRUE(buffer7->SwapToSceneMemory());

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
#endif

    // --- Scenario 1: Synchronous FreeCrawl ---
    // Advance 5 frames to make sure some buffers are old enough.
    bufferManager.AdvanceFrame(5);

#ifdef ENABLE_PAGE_ANALYSIS
    auto startTime = std::chrono::high_resolution_clock::now();
#endif
    // Check 50% of buffers synchronously.
    bufferManager.FreeCrawl(50.0f);

#ifdef ENABLE_PAGE_ANALYSIS
    auto endTime  = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
    std::cout << "...Synchronous FreeCrawl completed in " << duration.count() << "ns\n\n";

    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
    bufferManager.PrintCacheStats();
#endif

    // --- Scenario 2: Async FreeCrawl ---
    // Users call FreeCrawlAsync() and BufferManager will do the rest asynchronously:
    //   - Selects buffers using LRU strategy.
    //   - Applies Hybrid paging strategy.
    //   - Executes all operations in background.

    // Advance frames again to create more work
    for (int i = 0; i < 8; ++i)
    {
        bufferManager.AdvanceFrame();
    }

    // There should be no pending operations before FreeCrawl
    EXPECT_EQ(bufferManager.GetPendingOperations(), 0);
#ifdef ENABLE_PAGE_ANALYSIS
    startTime = std::chrono::high_resolution_clock::now();
#endif
    // Check 80% of buffers asynchronously.
    auto asyncFutures = bufferManager.FreeCrawlAsync(80.0f);
#ifdef ENABLE_PAGE_ANALYSIS
    endTime  = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
    std::cout << "...Async FreeCrawl completed in " << duration.count() << "ns\n";
    std::cout << "Pending async operations after FreeCrawl: "
              << bufferManager.GetPendingOperations() << "\n\n";
#endif

    // Wait for all async operations to complete
    size_t successCount = 0;
    for (auto& future : asyncFutures)
    {
        try
        {
            if (future.get())
            {
                ++successCount;
            }
        }
        catch (const std::exception& e)
        {
            GTEST_FAIL() << "Async operation failed: " << e.what();
        }
    }
    EXPECT_EQ(successCount, asyncFutures.size());
    EXPECT_EQ(0, bufferManager.GetPendingOperations());

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
    bufferManager.PrintCacheStats();
#endif

    // --- Demonstrating Aggressive Async FreeCrawl ---
    // For more intensive cleanup, users can increase the percentage.

    // Advance frames to create more aged buffers.
    bufferManager.AdvanceFrame(12);

#ifdef ENABLE_PAGE_ANALYSIS
    startTime = std::chrono::high_resolution_clock::now();
#endif
    // Check all buffers (100%).
    asyncFutures = bufferManager.FreeCrawlAsync(100.0f);
#ifdef ENABLE_PAGE_ANALYSIS
    endTime  = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
    std::cout << "...Aggressive async FreeCrawl completed in " << duration.count() << "ns\n\n";
#endif

    bufferManager.WaitForAllOperations();
    EXPECT_EQ(0, bufferManager.GetPendingOperations());
#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.GetMemoryMonitor()->PrintMemoryStats();
    bufferManager.PrintCacheStats();
#endif

    // Clean up
    bufferManager.RemoveBuffer(path1);
    bufferManager.RemoveBuffer(path2);
    bufferManager.RemoveBuffer(path3);
    bufferManager.RemoveBuffer(path4);
    bufferManager.RemoveBuffer(path5);
    bufferManager.RemoveBuffer(path6);
    bufferManager.RemoveBuffer(path7);
    buffer1.reset();
    buffer2.reset();
    buffer3.reset();
    buffer4.reset();
    buffer5.reset();
    buffer6.reset();
    buffer7.reset();

#ifdef ENABLE_PAGE_ANALYSIS
    bufferManager.PrintCacheStats();
#endif
    GTEST_SUCCEED();
}

// =============================================================================
// PageableDataSource Tests
// =============================================================================

/// Test: Basic usage of HdPageableValue for VtValue paging
TEST(TestPageableDataSource, BasicPageableValue)
{
    // Create buffer manager with test configuration
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_datasource_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.ageLimit            = 10;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create test data
    PXR_NS::VtVec3fArray points(10000); // 10000 points for a mesh
    for (size_t i = 0; i < points.size(); ++i)
    {
        points[i] = PXR_NS::GfVec3f(
            static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3));
    }
    PXR_NS::VtValue pointsValue(points);

    auto pageableValue = std::make_shared<hvt::HdPageableValue>(
        PXR_NS::SdfPath("/Mesh/points"), hvt::HdPageableValue::EstimateMemoryUsage(pointsValue),
        hvt::HdBufferUsage::Static, bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {}, pointsValue,
        PXR_NS::HdTokens->points);

    // Test 1: Data should be resident initially
    EXPECT_TRUE(pageableValue->IsDataResident());
    EXPECT_EQ(pageableValue->GetStatus(), hvt::HdPagingStatus::Resident);

    // Test 2: Get value (should not trigger paging since resident)
    bool pagedIn    = false;
    auto value      = pageableValue->GetValue(&pagedIn);
    EXPECT_FALSE(pagedIn);
    EXPECT_TRUE(value.IsHolding<PXR_NS::VtVec3fArray>());
    EXPECT_EQ(value.UncheckedGet<PXR_NS::VtVec3fArray>().size(), 10000);

    // Test 3: Page out to disk
    EXPECT_TRUE(pageableValue->SwapSceneToDisk());
    EXPECT_FALSE(pageableValue->IsDataResident());
    EXPECT_EQ(pageableValue->GetStatus(), hvt::HdPagingStatus::PagedOut);

    // Test 4: Get value (should trigger implicit page-in)
    pagedIn = false;
    value   = pageableValue->GetValue(&pagedIn);
    EXPECT_TRUE(pagedIn);
    EXPECT_TRUE(pageableValue->IsDataResident());
    EXPECT_EQ(value.UncheckedGet<PXR_NS::VtVec3fArray>().size(), 10000);

#ifdef ENABLE_PAGE_ANALYSIS
    // Verify metrics counters
    EXPECT_GE(pageableValue->GetAccessCount(), 2);
    EXPECT_GE(pageableValue->GetPageInCount(), 1);
    EXPECT_GE(pageableValue->GetPageOutCount(), 1);
#endif

    GTEST_SUCCEED();
}

/// Test: HdPageableDataSourceManager with metrics
TEST(TestPageableDataSource, DataSourceManagerWithMetrics)
{
    // Create manager with custom configuration
    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory      = std::filesystem::temp_directory_path() / "hvt_manager_test";
    config.sceneMemoryLimit       = 128 * hvt::ONE_MiB;
    config.rendererMemoryLimit    = 64 * hvt::ONE_MiB;
    config.freeCrawlPercentage    = 20.0f;
    config.freeCrawlIntervalMs    = 50;
    config.enableBackgroundCleanup = false; // Disable for deterministic testing
    config.ageLimit               = 5;

    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);

    // Create test data
    PXR_NS::VtFloatArray normals(5000);
    for (size_t i = 0; i < normals.size(); ++i)
    {
        normals[i] = static_cast<float>(i) * 0.001f;
    }

    // Create buffer through manager
    auto buffer1 = manager->GetOrCreateBuffer(
        PXR_NS::SdfPath("/Mesh1/normals"), PXR_NS::VtValue(normals), PXR_NS::HdTokens->normals);

    auto buffer2 = manager->GetOrCreateBuffer(
        PXR_NS::SdfPath("/Mesh2/normals"), PXR_NS::VtValue(normals), PXR_NS::HdTokens->normals);

    // Verify buffers were created
    EXPECT_EQ(manager->GetTotalBufferCount(), 2);
    EXPECT_EQ(manager->GetResidentBufferCount(), 2);
    EXPECT_EQ(manager->GetPagedOutBufferCount(), 0);

    // Cast to HdPageableValue to access specific methods
    auto pageableValue1 = std::dynamic_pointer_cast<hvt::HdPageableValue>(buffer1);
    auto pageableValue2 = std::dynamic_pointer_cast<hvt::HdPageableValue>(buffer2);
    ASSERT_NE(pageableValue1, nullptr);
    ASSERT_NE(pageableValue2, nullptr);

    // Page out buffer1
    EXPECT_TRUE(pageableValue1->SwapSceneToDisk());
#ifdef ENABLE_PAGE_ANALYSIS
    EXPECT_EQ(pageableValue1->GetPageOutCount(), 1);
#endif

    // Check metrics after page out
    EXPECT_EQ(manager->GetPagedOutBufferCount(), 1);

    // Page back in
    auto value = pageableValue1->GetValue();
    EXPECT_TRUE(value.IsHolding<PXR_NS::VtFloatArray>());
#ifdef ENABLE_PAGE_ANALYSIS
    EXPECT_EQ(pageableValue1->GetPageInCount(), 1);
#endif

    // Check metrics
    EXPECT_GT(manager->GetTotalMemoryUsage(), 0);

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Total memory usage: " << manager->GetTotalMemoryUsage() << " bytes\n";
    std::cout << "Memory pressure: " << (manager->GetMemoryPressure() * 100) << "%\n";
    manager->PrintMemoryStatistics();
#endif

    GTEST_SUCCEED();
}

/// Test: HdPageableSampledDataSource for time-sampled data
TEST(TestPageableDataSource, SampledDataSource)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_sampled_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create time-sampled data (e.g., animated positions)
    std::map<PXR_NS::HdSampledDataSource::Time, PXR_NS::VtValue> samples;
    for (int frame = 0; frame < 5; ++frame)
    {
        PXR_NS::VtVec3fArray positions(1000);
        for (size_t i = 0; i < positions.size(); ++i)
        {
            float offset   = static_cast<float>(frame) * 0.1f;
            positions[i] = PXR_NS::GfVec3f(
                static_cast<float>(i) + offset,
                static_cast<float>(i) * 2.0f + offset,
                static_cast<float>(i) * 3.0f + offset);
        }
        samples[static_cast<float>(frame)] = PXR_NS::VtValue(positions);
    }

    // Create pageable sampled data source
    auto sampledDs = hvt::HdPageableSampledDataSource::New(samples,
        PXR_NS::SdfPath("/AnimatedMesh/points"), PXR_NS::HdTokens->points,
        bufferManager.GetPageFileManager(), bufferManager.GetMemoryMonitor(),
        [](const PXR_NS::SdfPath&) {});

    // Test HdSampledDataSource APIs
    auto value0 = sampledDs->GetValue(0.0f);
    EXPECT_TRUE(value0.IsHolding<PXR_NS::VtVec3fArray>());

    auto value2 = sampledDs->GetValue(2.0f);
    EXPECT_TRUE(value2.IsHolding<PXR_NS::VtVec3fArray>());

    std::vector<PXR_NS::HdSampledDataSource::Time> times;
    EXPECT_TRUE(sampledDs->GetContributingSampleTimesForInterval(1.0f, 3.5f, &times));
    EXPECT_GE(times.size(), 2); // Should include samples at 1, 2, 3

    auto allTimes = sampledDs->GetAllSampleTimes();
    EXPECT_EQ(allTimes.size(), 5);

    sampledDs->SetInterpolationMode(
        hvt::HdPageableSampledDataSource::InterpolationMode::Linear);
    EXPECT_EQ(sampledDs->GetInterpolationMode(),
        hvt::HdPageableSampledDataSource::InterpolationMode::Linear);

    // Get value between samples (should interpolate)
    auto valueInterp = sampledDs->GetValue(1.5f);
    EXPECT_TRUE(valueInterp.IsHolding<PXR_NS::VtVec3fArray>());

    GTEST_SUCCEED();
}

/// Test: HdPageableContainerDataSource for prim data
TEST(TestPageableDataSource, ContainerDataSource)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_container_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create container data with multiple attributes
    std::map<PXR_NS::TfToken, PXR_NS::VtValue> primData;

    // Points
    PXR_NS::VtVec3fArray points(1000);
    for (size_t i = 0; i < points.size(); ++i)
    {
        points[i] = PXR_NS::GfVec3f(
            static_cast<float>(i), static_cast<float>(i) * 2.0f, static_cast<float>(i) * 3.0f);
    }
    primData[PXR_NS::HdTokens->points] = PXR_NS::VtValue(points);

    // Normals
    PXR_NS::VtVec3fArray normals(1000);
    for (size_t i = 0; i < normals.size(); ++i)
    {
        normals[i] = PXR_NS::GfVec3f(0.0f, 1.0f, 0.0f);
    }
    primData[PXR_NS::HdTokens->normals] = PXR_NS::VtValue(normals);

    // Indices
    PXR_NS::VtIntArray indices(3000);
    for (size_t i = 0; i < indices.size(); ++i)
    {
        indices[i] = static_cast<int>(i % 1000);
    }
    primData[PXR_NS::TfToken("indices")] = PXR_NS::VtValue(indices);

    // Create pageable container
    auto containerDs = hvt::HdPageableContainerDataSource::New(primData,
        PXR_NS::SdfPath("/Mesh"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {});

    // Test HdContainerDataSource APIs
    auto names = containerDs->GetNames();
    EXPECT_EQ(names.size(), 3);

    auto pointsDs = containerDs->Get(PXR_NS::HdTokens->points);
    EXPECT_NE(pointsDs, nullptr);

    auto normalsDs = containerDs->Get(PXR_NS::HdTokens->normals);
    EXPECT_NE(normalsDs, nullptr);

    EXPECT_TRUE(containerDs->IsElementResident(PXR_NS::HdTokens->points));

    // Test paging APIs
    EXPECT_TRUE(containerDs->PageOutElement(PXR_NS::HdTokens->points));
    EXPECT_FALSE(containerDs->IsElementResident(PXR_NS::HdTokens->points));

    EXPECT_TRUE(containerDs->PageInElement(PXR_NS::HdTokens->points));
    EXPECT_TRUE(containerDs->IsElementResident(PXR_NS::HdTokens->points));

    // Test implicit paging via Get
    EXPECT_TRUE(containerDs->PageOutElement(PXR_NS::HdTokens->normals));
    auto normalsDs2 = containerDs->Get(PXR_NS::HdTokens->normals); // Should page in
    EXPECT_NE(normalsDs2, nullptr);
    EXPECT_TRUE(containerDs->IsElementResident(PXR_NS::HdTokens->normals));

    auto breakdown = containerDs->GetMemoryBreakdown();
    EXPECT_EQ(breakdown.size(), 3);

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Memory breakdown:\n";
    for (const auto& [token, entry] : breakdown)
    {
        std::cout << "  " << token << ": " << entry.size << " bytes\n";
    }
#endif

    GTEST_SUCCEED();
}

/// Test: HdPageableVectorDataSource for arrays of data
TEST(TestPageableDataSource, VectorDataSource)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_vector_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create vector of face vertex counts and indices
    std::vector<PXR_NS::VtValue> meshData;

    // Face vertex counts
    PXR_NS::VtIntArray faceVertexCounts(500);
    std::fill(faceVertexCounts.begin(), faceVertexCounts.end(), 4); // All quads
    meshData.push_back(PXR_NS::VtValue(faceVertexCounts));

    // Face vertex indices
    PXR_NS::VtIntArray faceVertexIndices(2000);
    for (size_t i = 0; i < faceVertexIndices.size(); ++i)
    {
        faceVertexIndices[i] = static_cast<int>(i % 1000);
    }
    meshData.push_back(PXR_NS::VtValue(faceVertexIndices));

    // Hole indices
    PXR_NS::VtIntArray holeIndices(10);
    for (size_t i = 0; i < holeIndices.size(); ++i)
    {
        holeIndices[i] = static_cast<int>(i * 50);
    }
    meshData.push_back(PXR_NS::VtValue(holeIndices));

    // Create pageable vector
    auto vectorDs = hvt::HdPageableVectorDataSource::New(meshData, PXR_NS::SdfPath("/Mesh/topology"),
        bufferManager.GetPageFileManager(), bufferManager.GetMemoryMonitor(),
        [](const PXR_NS::SdfPath&) {});

    // Test HdVectorDataSource APIs
    EXPECT_EQ(vectorDs->GetNumElements(), 3);

    auto elem0 = vectorDs->GetElement(0);
    EXPECT_NE(elem0, nullptr);

    auto elem1 = vectorDs->GetElement(1);
    EXPECT_NE(elem1, nullptr);

    auto elemInvalid = vectorDs->GetElement(100);
    EXPECT_EQ(elemInvalid, nullptr);

    // Test paging APIs
    EXPECT_TRUE(vectorDs->IsElementResident(0));
    EXPECT_TRUE(vectorDs->PageOutElement(1));
    EXPECT_FALSE(vectorDs->IsElementResident(1));

    // Implicit page-in via GetElement
    auto elem1Again = vectorDs->GetElement(1);
    EXPECT_NE(elem1Again, nullptr);
    EXPECT_TRUE(vectorDs->IsElementResident(1));

    auto breakdown = vectorDs->GetMemoryBreakdown();
    EXPECT_EQ(breakdown.size(), 3);

    GTEST_SUCCEED();
}

/// Test: Utility functions for creating pageable data sources
TEST(TestPageableDataSource, UtilityFunctions)
{
    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(
        std::filesystem::temp_directory_path() / "hvt_utils_test",
        256 * hvt::ONE_MiB,
        128 * hvt::ONE_MiB);

    // Test CreateFromValue
    PXR_NS::VtVec3fArray testPoints(100);
    auto sampledDs = hvt::HdPageableDataSourceUtils::CreateFromValue(
        PXR_NS::VtValue(testPoints), PXR_NS::SdfPath("/Test/points"), PXR_NS::HdTokens->points,
        manager);
    EXPECT_NE(sampledDs, nullptr);

    // Test CreateContainer
    std::map<PXR_NS::TfToken, PXR_NS::VtValue> containerData;
    containerData[PXR_NS::HdTokens->points]  = PXR_NS::VtValue(testPoints);
    containerData[PXR_NS::HdTokens->normals] = PXR_NS::VtValue(testPoints);

    auto containerDs = hvt::HdPageableDataSourceUtils::CreateContainer(
        containerData, PXR_NS::SdfPath("/Test"), manager);
    EXPECT_NE(containerDs, nullptr);

    // Test CreateVector
    std::vector<PXR_NS::VtValue> vectorData;
    vectorData.push_back(PXR_NS::VtValue(PXR_NS::VtIntArray { 1, 2, 3 }));
    vectorData.push_back(PXR_NS::VtValue(PXR_NS::VtIntArray { 4, 5, 6 }));

    auto vectorDs = hvt::HdPageableDataSourceUtils::CreateVector(
        vectorData, PXR_NS::SdfPath("/Test/array"), manager);
    EXPECT_NE(vectorDs, nullptr);

    // Test CreateTimeSampled
    std::map<PXR_NS::HdSampledDataSource::Time, PXR_NS::VtValue> timeSamples;
    timeSamples[0.0f] = PXR_NS::VtValue(testPoints);
    timeSamples[1.0f] = PXR_NS::VtValue(testPoints);

    auto timeSampledDs = hvt::HdPageableDataSourceUtils::CreateTimeSampled(
        timeSamples, PXR_NS::SdfPath("/Test/animated"), PXR_NS::HdTokens->points, manager);
    EXPECT_NE(timeSampledDs, nullptr);

    // Test CreateBlock
    auto blockDs = hvt::HdPageableDataSourceUtils::CreateBlock(
        PXR_NS::VtValue(), PXR_NS::SdfPath("/Test/block"), manager);
    EXPECT_NE(blockDs, nullptr);

    GTEST_SUCCEED();
}

/// Test: Custom serializer for extended type support
TEST(TestPageableDataSource, CustomSerializer)
{
    // Create custom serializer that adds logging
    class LoggingSerializer : public hvt::HdDefaultValueSerializer
    {
    public:
        std::vector<uint8_t> Serialize(const PXR_NS::VtValue& value) const override
        {
            ++serializeCount;
#ifdef ENABLE_PAGE_ANALYSIS
            std::cout << "Serializing: " << value.GetTypeName() << "\n";
#endif
            return hvt::HdDefaultValueSerializer::Serialize(value);
        }

        PXR_NS::VtValue Deserialize(
            const std::vector<uint8_t>& data, const PXR_NS::TfToken& typeHint) const override
        {
            ++deserializeCount;
#ifdef ENABLE_PAGE_ANALYSIS
            std::cout << "Deserializing type hint: " << typeHint << "\n";
#endif
            return hvt::HdDefaultValueSerializer::Deserialize(data, typeHint);
        }

        mutable std::atomic<int> serializeCount { 0 };
        mutable std::atomic<int> deserializeCount { 0 };
    };

    auto customSerializer = std::make_shared<LoggingSerializer>();

    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory       = std::filesystem::temp_directory_path() / "hvt_custom_ser_test";
    config.sceneMemoryLimit        = 128 * hvt::ONE_MiB;
    config.rendererMemoryLimit     = 64 * hvt::ONE_MiB;
    config.enableBackgroundCleanup = false;

    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);

    // Set custom serializer
    manager->SetSerializer(customSerializer);
    EXPECT_EQ(manager->GetSerializer(), customSerializer);

    // Create buffer that will use custom serializer
    PXR_NS::VtFloatArray testData(1000);
    auto buffer = manager->GetOrCreateBuffer(
        PXR_NS::SdfPath("/CustomTest/data"), PXR_NS::VtValue(testData), PXR_NS::TfToken("float[]"));

    auto pageableValue = std::dynamic_pointer_cast<hvt::HdPageableValue>(buffer);
    ASSERT_NE(pageableValue, nullptr);

    // Page out and back in to trigger serialization
    EXPECT_TRUE(pageableValue->SwapSceneToDisk());
#ifdef ENABLE_PAGE_ANALYSIS
    EXPECT_EQ(pageableValue->GetPageOutCount(), 1);
#endif

    auto value = pageableValue->GetValue();
#ifdef ENABLE_PAGE_ANALYSIS
    EXPECT_EQ(pageableValue->GetPageInCount(), 1);
#endif

    // Verify custom serializer was used
    EXPECT_GE(customSerializer->serializeCount.load(), 1);
    EXPECT_GE(customSerializer->deserializeCount.load(), 1);

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Serialize calls: " << customSerializer->serializeCount.load() << "\n";
    std::cout << "Deserialize calls: " << customSerializer->deserializeCount.load() << "\n";
#endif

    GTEST_SUCCEED();
}

/// Test: Integration with memory pressure and background cleanup
TEST(TestPageableDataSource, MemoryPressureIntegration)
{
    // Create manager with low memory limits to trigger pressure quickly
    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory       = std::filesystem::temp_directory_path() / "hvt_pressure_test";
    config.sceneMemoryLimit        = 32 * hvt::ONE_MiB; // Low limit
    config.rendererMemoryLimit     = 16 * hvt::ONE_MiB;
    config.freeCrawlPercentage     = 50.0f;
    config.freeCrawlIntervalMs     = 100;
    config.enableBackgroundCleanup = true; // Enable for this test
    config.ageLimit                = 3;

    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);

    // Create many buffers to exceed memory limit
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
    for (int i = 0; i < 10; ++i)
    {
        PXR_NS::VtFloatArray data(500000); // ~2MB each
        std::fill(data.begin(), data.end(), static_cast<float>(i));

        auto path   = PXR_NS::SdfPath("/PressureTest/buffer" + std::to_string(i));
        auto buffer = manager->GetOrCreateBuffer(
            path, PXR_NS::VtValue(data), PXR_NS::TfToken("float[]"));
        buffers.push_back(buffer);

        // Simulate frame advancement to age buffers
        manager->AdvanceFrame();

#ifdef ENABLE_PAGE_ANALYSIS
        std::cout << "Created buffer " << i << ", memory pressure: "
                  << (manager->GetMemoryPressure() * 100) << "%\n";
#endif
    }

    // Wait a bit for background cleanup to kick in
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Check metrics
#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Total buffers: " << manager->GetTotalBufferCount() << "\n";
    std::cout << "Resident buffers: " << manager->GetResidentBufferCount() << "\n";
    std::cout << "Paged-out buffers: " << manager->GetPagedOutBufferCount() << "\n";
    std::cout << "Total memory usage: " << manager->GetTotalMemoryUsage() << " bytes\n";
    std::cout << "Memory pressure: " << (manager->GetMemoryPressure() * 100) << "%\n";
    manager->PrintMemoryStatistics();
#endif

    GTEST_SUCCEED();
}

/// Test: Thread safety with concurrent access
TEST(TestPageableDataSource, ConcurrentAccess)
{
    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory       = std::filesystem::temp_directory_path() / "hvt_concurrent_test";
    config.sceneMemoryLimit        = 256 * hvt::ONE_MiB;
    config.rendererMemoryLimit     = 128 * hvt::ONE_MiB;
    config.enableBackgroundCleanup = false;
    config.numThreads              = 4;

    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);

    // Create shared buffer
    PXR_NS::VtVec3fArray testData(10000);
    for (size_t i = 0; i < testData.size(); ++i)
    {
        testData[i] = PXR_NS::GfVec3f(
            static_cast<float>(i), static_cast<float>(i), static_cast<float>(i));
    }

    auto buffer = manager->GetOrCreateBuffer(
        PXR_NS::SdfPath("/ConcurrentTest/data"), PXR_NS::VtValue(testData), PXR_NS::HdTokens->points);

    auto pageableValue = std::dynamic_pointer_cast<hvt::HdPageableValue>(buffer);
    ASSERT_NE(pageableValue, nullptr);

    // Launch multiple threads accessing the same buffer
    std::atomic<int> successCount { 0 };
    std::atomic<int> failCount { 0 };
    std::vector<std::thread> threads;

    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back(
            [&pageableValue, &successCount, &failCount, t]()
            {
                for (int i = 0; i < 100; ++i)
                {
                    // Alternate between reading and paging operations
                    if (i % 10 == t % 10)
                    {
                        // Try to page out
                        pageableValue->SwapSceneToDisk();
                    }
                    else
                    {
                        // Read value
                        auto value = pageableValue->GetValue();
                        if (!value.IsEmpty())
                        {
                            ++successCount;
                        }
                        else
                        {
                            ++failCount;
                        }
                    }
                }
            });
    }

    // Wait for all threads to complete
    for (auto& thread : threads)
    {
        thread.join();
    }

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Successful reads: " << successCount.load() << "\n";
    std::cout << "Failed reads: " << failCount.load() << "\n";
#endif

    // Most reads should succeed due to implicit paging
    EXPECT_GT(successCount.load(), 0);

    GTEST_SUCCEED();
}

/// Test: Retained container PageIn/PageOut element operations
TEST(TestPageableDataSource, RetainedContainerPageInOut)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_retained_container_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create retained container test data
    PXR_NS::VtVec3fArray points(500);
    PXR_NS::VtVec3fArray normals(500);
    for (size_t i = 0; i < 500; ++i)
    {
        points[i]  = PXR_NS::GfVec3f(static_cast<float>(i), 0.f, 0.f);
        normals[i] = PXR_NS::GfVec3f(0.f, 1.f, 0.f);
    }

    std::map<PXR_NS::TfToken, PXR_NS::VtValue> data;
    data[PXR_NS::HdTokens->points]  = PXR_NS::VtValue(points);
    data[PXR_NS::HdTokens->normals] = PXR_NS::VtValue(normals);

    auto retained = hvt::HdPageableRetainedContainerDataSource::New(data,
        PXR_NS::SdfPath("/RetainedMesh"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {});

    // Test paging APIs
    EXPECT_TRUE(retained->IsElementResident(PXR_NS::HdTokens->points));
    EXPECT_TRUE(retained->IsElementResident(PXR_NS::HdTokens->normals));

    EXPECT_TRUE(retained->PageOutElement(PXR_NS::HdTokens->points));
    EXPECT_FALSE(retained->IsElementResident(PXR_NS::HdTokens->points));
    EXPECT_TRUE(retained->IsElementResident(PXR_NS::HdTokens->normals));

    EXPECT_TRUE(retained->PageInElement(PXR_NS::HdTokens->points));
    EXPECT_TRUE(retained->IsElementResident(PXR_NS::HdTokens->points));

    // Implicit paging via Get
    EXPECT_TRUE(retained->PageOutElement(PXR_NS::HdTokens->normals));
    EXPECT_FALSE(retained->IsElementResident(PXR_NS::HdTokens->normals));
    EXPECT_TRUE(retained->IsElementResident(PXR_NS::HdTokens->points));

    auto ds = retained->Get(PXR_NS::HdTokens->normals);
    EXPECT_NE(ds, nullptr);

    GTEST_SUCCEED();
}

/// Test: Retained small vector PageIn/PageOut element operations
TEST(TestPageableDataSource, RetainedVectorPageInOut)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_retained_vector_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create retained vector test data
    PXR_NS::VtIntArray countsArr(100);
    PXR_NS::VtIntArray indicesArr(400);
    std::fill(countsArr.begin(), countsArr.end(), 4);
    for (size_t i = 0; i < indicesArr.size(); ++i)
        indicesArr[i] = static_cast<int>(i % 100);

    std::vector<PXR_NS::VtValue> elems;
    elems.push_back(PXR_NS::VtValue(countsArr));
    elems.push_back(PXR_NS::VtValue(indicesArr));

    auto retained = hvt::HdPageableRetainedSmallVectorDataSource::New(elems,
        PXR_NS::SdfPath("/RetainedVec"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {});

    // Test paging APIs
    EXPECT_EQ(retained->GetNumElements(), 2);
    EXPECT_TRUE(retained->IsElementResident(0));
    EXPECT_TRUE(retained->IsElementResident(1));

    EXPECT_TRUE(retained->PageOutElement(0));
    EXPECT_FALSE(retained->IsElementResident(0));

    // Implicit paging via GetElement
    auto elem = retained->GetElement(0);
    EXPECT_NE(elem, nullptr);
    EXPECT_TRUE(retained->IsElementResident(0));

    GTEST_SUCCEED();
}

/// Test: Packed disk storage round-trip for container
TEST(TestPageableDataSource, PackedContainerDiskRoundTrip)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_packed_container_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create container test data
    PXR_NS::VtVec3fArray points(2000);
    PXR_NS::VtVec3fArray normals(2000);
    PXR_NS::VtIntArray indices(6000);
    for (size_t i = 0; i < 2000; ++i)
    {
        points[i]  = PXR_NS::GfVec3f(static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3));
        normals[i] = PXR_NS::GfVec3f(0.f, 1.f, 0.f);
    }
    for (size_t i = 0; i < 6000; ++i)
        indices[i] = static_cast<int>(i % 2000);

    std::map<PXR_NS::TfToken, PXR_NS::VtValue> data;
    data[PXR_NS::HdTokens->points]  = PXR_NS::VtValue(points);
    data[PXR_NS::HdTokens->normals] = PXR_NS::VtValue(normals);
    data[PXR_NS::TfToken("indices")] = PXR_NS::VtValue(indices);

    auto container = hvt::HdPageableContainerDataSource::New(data,
        PXR_NS::SdfPath("/PackedMesh"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {});

    EXPECT_EQ(container->GetNames().size(), 3);

    // Swap all to disk (packed)
    EXPECT_TRUE(container->SwapSceneToDisk());

    // All elements should be non-resident now
    EXPECT_FALSE(container->IsElementResident(PXR_NS::HdTokens->points));
    EXPECT_FALSE(container->IsElementResident(PXR_NS::HdTokens->normals));
    EXPECT_FALSE(container->IsElementResident(PXR_NS::TfToken("indices")));

    // Swap back from disk (packed)
    EXPECT_TRUE(container->SwapToSceneMemory());

    // All elements should be resident again
    EXPECT_TRUE(container->IsElementResident(PXR_NS::HdTokens->points));
    EXPECT_TRUE(container->IsElementResident(PXR_NS::HdTokens->normals));
    EXPECT_TRUE(container->IsElementResident(PXR_NS::TfToken("indices")));

    // Verify data integrity after round-trip via HdSampledDataSource interface
    auto pointsDs = PXR_NS::HdSampledDataSource::Cast(
        container->Get(PXR_NS::HdTokens->points));
    ASSERT_NE(pointsDs, nullptr);
    auto restoredPoints = pointsDs->GetValue(0.0f);
    EXPECT_TRUE(restoredPoints.IsHolding<PXR_NS::VtVec3fArray>());
    auto& pArr = restoredPoints.UncheckedGet<PXR_NS::VtVec3fArray>();
    EXPECT_EQ(pArr.size(), 2000);
    EXPECT_EQ(pArr[0], PXR_NS::GfVec3f(0.f, 0.f, 0.f));
    EXPECT_EQ(pArr[1], PXR_NS::GfVec3f(1.f, 2.f, 3.f));

    auto indicesDs = PXR_NS::HdSampledDataSource::Cast(
        container->Get(PXR_NS::TfToken("indices")));
    ASSERT_NE(indicesDs, nullptr);
    auto restoredIndices = indicesDs->GetValue(0.0f);
    EXPECT_TRUE(restoredIndices.IsHolding<PXR_NS::VtIntArray>());
    EXPECT_EQ(restoredIndices.UncheckedGet<PXR_NS::VtIntArray>().size(), 6000);

    GTEST_SUCCEED();
}

/// Test: Packed disk storage round-trip for vector
TEST(TestPageableDataSource, PackedVectorDiskRoundTrip)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_packed_vector_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create vector test data
    PXR_NS::VtIntArray arr1(1000);
    PXR_NS::VtFloatArray arr2(500);
    for (size_t i = 0; i < arr1.size(); ++i)
        arr1[i] = static_cast<int>(i);
    for (size_t i = 0; i < arr2.size(); ++i)
        arr2[i] = static_cast<float>(i) * 0.5f;

    std::vector<PXR_NS::VtValue> elems;
    elems.push_back(PXR_NS::VtValue(arr1));
    elems.push_back(PXR_NS::VtValue(arr2));

    auto vectorDs = hvt::HdPageableVectorDataSource::New(elems,
        PXR_NS::SdfPath("/PackedVec"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {});

    EXPECT_EQ(vectorDs->GetNumElements(), 2);

    // Swap to disk (packed)
    EXPECT_TRUE(vectorDs->SwapSceneToDisk());
    EXPECT_FALSE(vectorDs->IsElementResident(0));
    EXPECT_FALSE(vectorDs->IsElementResident(1));

    // Swap back to scene memory (packed)
    EXPECT_TRUE(vectorDs->SwapToSceneMemory());
    EXPECT_TRUE(vectorDs->IsElementResident(0));
    EXPECT_TRUE(vectorDs->IsElementResident(1));

    // Verify data integrity after round-trip via HdVectorDataSource interface
    auto elem0 = PXR_NS::HdSampledDataSource::Cast(vectorDs->GetElement(0));
    ASSERT_NE(elem0, nullptr);
    auto val = elem0->GetValue(0.0f);
    EXPECT_TRUE(val.IsHolding<PXR_NS::VtIntArray>());
    EXPECT_EQ(val.UncheckedGet<PXR_NS::VtIntArray>().size(), 1000);

    GTEST_SUCCEED();
}

/// Test: Observability metrics on composite data sources
TEST(TestPageableDataSource, ObservabilityMetrics)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_observability_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create container test data
    PXR_NS::VtVec3fArray pts(100);
    PXR_NS::VtVec3fArray nrm(100);
    std::map<PXR_NS::TfToken, PXR_NS::VtValue> data;
    data[PXR_NS::HdTokens->points]  = PXR_NS::VtValue(pts);
    data[PXR_NS::HdTokens->normals] = PXR_NS::VtValue(nrm);

    auto container = hvt::HdPageableContainerDataSource::New(data,
        PXR_NS::SdfPath("/MetricsMesh"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {});

#ifdef ENABLE_PAGE_ANALYSIS
    EXPECT_EQ(container->GetAccessCount(), 0);
    EXPECT_EQ(container->GetPageInCount(), 0);
    EXPECT_EQ(container->GetPageOutCount(), 0);
#endif

    container->Get(PXR_NS::HdTokens->points);
#ifdef ENABLE_PAGE_ANALYSIS
    EXPECT_GE(container->GetAccessCount(), 1);
#endif

    container->PageOutElement(PXR_NS::HdTokens->points);
#ifdef ENABLE_PAGE_ANALYSIS
    EXPECT_GE(container->GetPageOutCount(), 1);
#endif

    container->PageInElement(PXR_NS::HdTokens->points);
#ifdef ENABLE_PAGE_ANALYSIS
    EXPECT_GE(container->GetPageInCount(), 1);
#endif

    container->SwapSceneToDisk();
    container->SwapToSceneMemory();

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Container access count: " << container->GetAccessCount() << "\n";
    std::cout << "Container page-in count: " << container->GetPageInCount() << "\n";
    std::cout << "Container page-out count: " << container->GetPageOutCount() << "\n";
#endif

    GTEST_SUCCEED();
}

/// Test: Templatized key types (std::string key)
TEST(TestPageableBuffer, StringKeyManager)
{
    // Create string key buffer manager
    using StringBufferManager = hvt::HdPageableBufferManager<
        hvt::HdPagingStrategies::HybridStrategy,
        hvt::HdPagingStrategies::LRUSelectionStrategy,
        std::string, hvt::StringKeyHash>;

    StringBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_string_key_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.ageLimit            = 10;

    StringBufferManager mgr(desc);

    // Create buffers with string keys
    auto buf1 = mgr.CreateBuffer("vertex_buffer_0", 10 * hvt::ONE_MiB);
    auto buf2 = mgr.CreateBuffer("index_buffer_0", 5 * hvt::ONE_MiB);
    EXPECT_NE(buf1, nullptr);
    EXPECT_NE(buf2, nullptr);
    EXPECT_EQ(mgr.GetBufferCount(), 2);

    auto found = mgr.FindBuffer("vertex_buffer_0");
    EXPECT_EQ(found, buf1);

    auto notFound = mgr.FindBuffer("nonexistent");
    EXPECT_EQ(notFound, nullptr);

    mgr.RemoveBuffer("index_buffer_0");
    EXPECT_EQ(mgr.GetBufferCount(), 1);

    // Create duplicate buffer with same key. It should return the existing buffer.
    auto duplicate = mgr.CreateBuffer("vertex_buffer_0", 20 * hvt::ONE_MiB);
    EXPECT_EQ(duplicate, buf1);
    EXPECT_EQ(mgr.GetBufferCount(), 1);

    GTEST_SUCCEED();
}

/// Test: Combined process, access triggers timestamp update for AgeBasedStrategy
TEST(TestPageableDataSource, CombinedProcessTimestamp)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_combined_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.ageLimit            = 5;
    desc.numThreads          = 2;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create test data
    PXR_NS::VtFloatArray data(1000);
    std::fill(data.begin(), data.end(), 42.0f);
    PXR_NS::VtValue val(data);

    auto pv = std::make_shared<hvt::HdPageableValue>(
        PXR_NS::SdfPath("/CombinedTest/data"),
        hvt::HdPageableValue::EstimateMemoryUsage(val),
        hvt::HdBufferUsage::Static,
        bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(),
        [](const PXR_NS::SdfPath&) {},
        val, PXR_NS::TfToken("float[]"));

    // Page out
    EXPECT_TRUE(pv->SwapSceneToDisk());
    EXPECT_FALSE(pv->IsDataResident());

    // Advance frames to age the buffer
    for (int i = 0; i < 10; ++i)
        bufferManager.AdvanceFrame();

    EXPECT_TRUE(pv->IsOverAge(bufferManager.GetCurrentFrame(), desc.ageLimit));

    // Access triggers page-in and frame stamp update
    auto retrieved = pv->GetValue();
    EXPECT_TRUE(pv->IsDataResident());
    EXPECT_TRUE(retrieved.IsHolding<PXR_NS::VtFloatArray>());
    EXPECT_EQ(retrieved.UncheckedGet<PXR_NS::VtFloatArray>().size(), 1000);

    GTEST_SUCCEED();
}

/// Test: Status consistency under concurrent packed operations
TEST(TestPageableDataSource, PackedConcurrentConsistency)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_packed_concurrent_test";
    desc.sceneMemoryLimit    = 256 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 128 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);

    // Create test data
    PXR_NS::VtVec3fArray pts(500);
    PXR_NS::VtVec3fArray nrm(500);
    for (size_t i = 0; i < 500; ++i)
    {
        pts[i] = PXR_NS::GfVec3f(static_cast<float>(i), 0.f, 0.f);
        nrm[i] = PXR_NS::GfVec3f(0.f, 1.f, 0.f);
    }

    std::map<PXR_NS::TfToken, PXR_NS::VtValue> data;
    data[PXR_NS::HdTokens->points]  = PXR_NS::VtValue(pts);
    data[PXR_NS::HdTokens->normals] = PXR_NS::VtValue(nrm);

    auto container = hvt::HdPageableContainerDataSource::New(data,
        PXR_NS::SdfPath("/ConcurrentPacked"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {});

    // Create threads for concurrent access
    std::atomic<int> readSuccess { 0 };
    std::vector<std::thread> threads;

    for (int t = 0; t < 6; ++t)
    {
        threads.emplace_back(
            [&container, &readSuccess, t]()
            {
                for (int i = 0; i < 50; ++i)
                {
                    if (i % 7 == t % 7)
                    {
                        container->SwapSceneToDisk();
                    }
                    else if (i % 7 == (t + 3) % 7)
                    {
                        container->SwapToSceneMemory();
                    }
                    else
                    {
                        auto ds = container->Get(PXR_NS::HdTokens->points);
                        if (ds)
                            ++readSuccess;
                    }
                }
            });
    }

    // Wait for all threads to complete
    for (auto& th : threads)
        th.join();

    EXPECT_GT(readSuccess.load(), 0);

#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "Concurrent packed read successes: " << readSuccess.load() << "\n";
    std::cout << "Access count: " << container->GetAccessCount() << "\n";
    std::cout << "Page-in count: " << container->GetPageInCount() << "\n";
    std::cout << "Page-out count: " << container->GetPageOutCount() << "\n";
#endif

    GTEST_SUCCEED();
}

/// Test: Integration demo showing typical usage pattern
TEST(TestPageableDataSource, IntegrationDemo)
{
    // Step 1: Create manager with production-like settings
    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory       = std::filesystem::temp_directory_path() / "hvt_integration_demo";
    config.sceneMemoryLimit        = 512 * hvt::ONE_MiB;
    config.rendererMemoryLimit     = 256 * hvt::ONE_MiB;
    config.freeCrawlPercentage     = 20.0f;
    config.freeCrawlIntervalMs     = 200;
    config.enableBackgroundCleanup = true;
    config.ageLimit                = 10;
    config.numThreads              = 4;

    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);

    // Step 2: Simulate loading mesh data
    struct MeshData
    {
        PXR_NS::SdfPath primPath;
        PXR_NS::VtVec3fArray points;
        PXR_NS::VtVec3fArray normals;
        PXR_NS::VtIntArray indices;
    };

    // Create meshes
    std::vector<MeshData> meshes;
    for (int i = 0; i < 5; ++i)
    {
        MeshData mesh;
        mesh.primPath = PXR_NS::SdfPath("/World/Mesh" + std::to_string(i));

        // Generate mesh data
        mesh.points.resize(5000);
        mesh.normals.resize(5000);
        mesh.indices.resize(15000);

        for (size_t j = 0; j < mesh.points.size(); ++j)
        {
            float offset     = static_cast<float>(i) * 10.0f;
            mesh.points[j] = PXR_NS::GfVec3f(
                static_cast<float>(j) + offset, static_cast<float>(j) * 2.0f, static_cast<float>(j) * 3.0f);
            mesh.normals[j] = PXR_NS::GfVec3f(0.0f, 1.0f, 0.0f);
        }
        for (size_t j = 0; j < mesh.indices.size(); ++j)
        {
            mesh.indices[j] = static_cast<int>(j % 5000);
        }

        meshes.push_back(mesh);
    }

    // Step 3: Create pageable containers for each mesh
    std::vector<hvt::HdPageableContainerDataSource::Handle> meshContainers;
    for (const auto& mesh : meshes)
    {
        std::map<PXR_NS::TfToken, PXR_NS::VtValue> meshDataMap;
        meshDataMap[PXR_NS::HdTokens->points]  = PXR_NS::VtValue(mesh.points);
        meshDataMap[PXR_NS::HdTokens->normals] = PXR_NS::VtValue(mesh.normals);
        meshDataMap[PXR_NS::TfToken("indices")] = PXR_NS::VtValue(mesh.indices);

        auto container = hvt::HdPageableContainerDataSource::New(meshDataMap, mesh.primPath,
            manager->GetPageFileManager(), manager->GetMemoryMonitor(), [](const PXR_NS::SdfPath&) {});

        meshContainers.push_back(container);
    }

    // Step 4: Simulate render loop with frame advancement
    for (int frame = 0; frame < 20; ++frame)
    {
        manager->AdvanceFrame();

#ifdef ENABLE_PAGE_ANALYSIS
        if (frame % 5 == 0)
        {
            std::cout << "\n--- Frame " << frame << " ---\n";
            std::cout << "Total memory: " << manager->GetTotalMemoryUsage() << " bytes\n";
            std::cout << "Memory pressure: " << (manager->GetMemoryPressure() * 100) << "%\n";
            std::cout << "Resident: " << manager->GetResidentBufferCount() << ", Paged: " << manager->GetPagedOutBufferCount() << "\n";
        }
#endif

        // Simulate accessing different meshes at different frames
        int meshIndex = frame % static_cast<int>(meshContainers.size());
        auto& container = meshContainers[meshIndex];

        // Access mesh data (triggers implicit paging if needed)
        auto pointsDs = container->Get(PXR_NS::HdTokens->points);
        EXPECT_NE(pointsDs, nullptr);

        // Page out older meshes to manage memory
        if (frame > 5)
        {
            int oldMeshIndex = (frame - 5) % static_cast<int>(meshContainers.size());
            meshContainers[oldMeshIndex]->PageOutElement(PXR_NS::HdTokens->points);
        }

        // Small delay to simulate frame time
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Step 5: Final statistics
#ifdef ENABLE_PAGE_ANALYSIS
    std::cout << "\n=== Final Statistics ===\n";
    std::cout << "Total buffers: " << manager->GetTotalBufferCount() << "\n";
    std::cout << "Resident: " << manager->GetResidentBufferCount() << "\n";
    std::cout << "Paged out: " << manager->GetPagedOutBufferCount() << "\n";
    std::cout << "Total memory: " << manager->GetTotalMemoryUsage() << " bytes\n";
    manager->PrintMemoryStatistics();
#endif

    GTEST_SUCCEED();
}
