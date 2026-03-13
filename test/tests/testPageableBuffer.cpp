// Copyright 2025 Autodesk, Inc.
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

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

// Include the appropriate test context declaration.
#include <RenderingFramework/TestContextCreator.h>

// Include paging system
#include <hvt/pageableBuffer/pageableBuffer.h>
#include <hvt/pageableBuffer/pageableBufferManager.h>
#include <hvt/pageableBuffer/pageableDataSource.h>
#include <hvt/pageableBuffer/pageableMemoryMonitor.h>

#include <gtest/gtest.h>

#include <filesystem>

#define ENABLE_PAGE_ANALYSIS

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
    std::vector<std::shared_ptr<hvt::HdPageableBufferBase>> extraBuffers;
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
    std::vector<std::shared_ptr<hvt::HdPageableBufferBase>> buffers;
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

/// TODO... implement DataSource and background free crawl.
TEST(TestPageableBuffer, DataSourceAndBackgroundFreeCrawl)
{
    // TODO...
    GTEST_SUCCEED();
}