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

#include <benchmark/benchmark.h>

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

// Include paging system
#include <hvt/pageableBuffer/pageableBuffer.h>
#include <hvt/pageableBuffer/pageableBufferManager.h>
#include <hvt/pageableBuffer/pageableDataSource.h>
#include <hvt/pageableBuffer/pageableMemoryMonitor.h>
#include <hvt/pageableBuffer/pageableRetainedDataSource.h>
#include <hvt/pageableBuffer/pageableStrategies.h>

// USD types
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// =============================================================================
// Benchmark MemoryManager
// =============================================================================
//
// When ENABLE_MEMORY_TRACKER is defined, a custom benchmark::MemoryManager is
// registered that hooks into the OS allocator to report per-benchmark heap
// statistics (num_allocs, max_bytes_used, net_heap_growth).
// This is for verifying that the paging system actually reclaims memory.
// =============================================================================

#ifdef ENABLE_MEMORY_TRACKER

// --- Platform headers for memory introspection ---
#if defined(__APPLE__)
#include <malloc/malloc.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <cstdio>
#include <malloc.h>
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

/// Returns resident set size (RSS) of the current process in bytes.
static size_t GetProcessResidentBytes()
{
#if defined(__APPLE__)
    mach_task_basic_info_data_t info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
    {
        return info.resident_size;
    }
    return 0;

#elif defined(__linux__)
    // /proc/self/statm fields: size resident shared text lib data dt (in pages)
    size_t residentPages = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r"))
    {
        size_t ignored = 0;
        if (std::fscanf(f, "%zu %zu", &ignored, &residentPages) != 2)
            residentPages = 0;
        std::fclose(f);
    }
    return residentPages * static_cast<size_t>(sysconf(_SC_PAGESIZE));

#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc {};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return pmc.WorkingSetSize;
    }
    return 0;

#else
    return 0;
#endif
}

/// Returns total bytes currently allocated (in-use) on the heap.
static size_t GetHeapAllocatedBytes()
{
#if defined(__APPLE__)
    size_t totalInUse      = 0;
    unsigned int zoneCount = 0;
    vm_address_t* zones    = nullptr;
    if (malloc_get_all_zones(mach_task_self(), nullptr, &zones, &zoneCount) == KERN_SUCCESS)
    {
        for (unsigned int i = 0; i < zoneCount; ++i)
        {
            malloc_statistics_t stats {};
            malloc_zone_statistics(
                reinterpret_cast<malloc_zone_t*>(zones[i]), &stats);
            totalInUse += stats.size_in_use;
        }
    }
    return totalInUse;

#elif defined(__linux__)
    // mallinfo2 (glibc >= 2.33) uses size_t; mallinfo uses int and overflows >2 GiB.
#if defined(__GLIBC__) && defined(__GLIBC_MINOR__) \
    && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    struct mallinfo2 mi = mallinfo2();
    return mi.uordblks;
#else
    struct mallinfo mi = mallinfo();
    return static_cast<size_t>(mi.uordblks);
#endif

#elif defined(_WIN32)
    // Sum committed private bytes across all process heaps.
    DWORD numHeaps = GetProcessHeaps(0, nullptr);
    if (numHeaps == 0)
        return 0;

    std::vector<HANDLE> heaps(numHeaps);
    GetProcessHeaps(numHeaps, heaps.data());

    size_t totalInUse = 0;
    for (HANDLE heap : heaps)
    {
        if (!HeapLock(heap))
            continue;
        PROCESS_HEAP_ENTRY entry {};
        while (HeapWalk(heap, &entry))
        {
            if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
                totalInUse += entry.cbData;
        }
        HeapUnlock(heap);
    }
    return totalInUse;

#else
    return 0;
#endif
}

/// Cross-platform MemoryManager that reports per-benchmark heap and RSS
/// statistics through Google Benchmark's reporting infrastructure.
class PagingMemoryManager : public benchmark::MemoryManager
{
public:
    void Start() override
    {
        mStartHeapBytes     = GetHeapAllocatedBytes();
        mStartResidentBytes = GetProcessResidentBytes();
        mPeakHeapBytes      = mStartHeapBytes;
    }

    void Stop(Result& result) override
    {
        size_t endHeapBytes     = GetHeapAllocatedBytes();
        size_t endResidentBytes = GetProcessResidentBytes();

        mPeakHeapBytes = std::max(mPeakHeapBytes, endHeapBytes);

        result.max_bytes_used        = static_cast<int64_t>(mPeakHeapBytes);
        result.net_heap_growth       = static_cast<int64_t>(endHeapBytes) -
                                       static_cast<int64_t>(mStartHeapBytes);
        result.total_allocated_bytes = static_cast<int64_t>(endResidentBytes) -
                                       static_cast<int64_t>(mStartResidentBytes);
        result.num_allocs            = 0;
    }

private:
    size_t mStartHeapBytes     = 0;
    size_t mStartResidentBytes = 0;
    size_t mPeakHeapBytes      = 0;
};

#endif // ENABLE_MEMORY_TRACKER

// =============================================================================
// Memory Tracking Utilities
// =============================================================================

/// Tracks memory statistics for benchmarks
struct MemoryStats
{
    size_t sceneMemoryUsed    = 0;
    size_t rendererMemoryUsed = 0;
    size_t peakSceneMemory    = 0;
    size_t peakRendererMemory = 0;

    void Update(const std::unique_ptr<hvt::HdMemoryMonitor>& monitor)
    {
        sceneMemoryUsed    = monitor->GetUsedSceneMemory();
        rendererMemoryUsed = monitor->GetUsedRendererMemory();
        peakSceneMemory    = std::max(peakSceneMemory, sceneMemoryUsed);
        peakRendererMemory = std::max(peakRendererMemory, rendererMemoryUsed);
    }

    void Reset()
    {
        sceneMemoryUsed    = 0;
        rendererMemoryUsed = 0;
        peakSceneMemory    = 0;
        peakRendererMemory = 0;
    }
};

/// Helper to set memory counters on benchmark state
static void SetMemoryCounters(benchmark::State& state, const MemoryStats& stats)
{
    state.counters["SceneMemMB"] =
        benchmark::Counter(static_cast<double>(stats.sceneMemoryUsed) / hvt::ONE_MiB);
    state.counters["RendererMemMB"] =
        benchmark::Counter(static_cast<double>(stats.rendererMemoryUsed) / hvt::ONE_MiB);
    state.counters["PeakSceneMemMB"] =
        benchmark::Counter(static_cast<double>(stats.peakSceneMemory) / hvt::ONE_MiB);
    state.counters["PeakRendererMemMB"] =
        benchmark::Counter(static_cast<double>(stats.peakRendererMemory) / hvt::ONE_MiB);
}

// =============================================================================
// Test Data Generators
// =============================================================================

/// Generate test mesh points
static VtVec3fArray GeneratePoints(size_t count)
{
    VtVec3fArray points(count);
    for (size_t i = 0; i < count; ++i)
    {
        points[i] = GfVec3f(static_cast<float>(i), static_cast<float>(i * 2),
            static_cast<float>(i * 3));
    }
    return points;
}

/// Generate test float array
static VtFloatArray GenerateFloats(size_t count)
{
    VtFloatArray floats(count);
    for (size_t i = 0; i < count; ++i)
    {
        floats[i] = static_cast<float>(i) * 0.001f;
    }
    return floats;
}

/// Generate test int array
static VtIntArray GenerateIndices(size_t count)
{
    VtIntArray indices(count);
    for (size_t i = 0; i < count; ++i)
    {
        indices[i] = static_cast<int>(i % 10000);
    }
    return indices;
}

// =============================================================================
// Buffer Manager Creation Benchmarks
// =============================================================================

/// Benchmark: Buffer manager initialization
static void BM_BufferManagerInit(benchmark::State& state)
{
    for (auto _ : state)
    {
        hvt::DefaultBufferManager::InitializeDesc desc;
        desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_init";
        desc.sceneMemoryLimit    = static_cast<size_t>(state.range(0)) * hvt::ONE_MiB;
        desc.rendererMemoryLimit = static_cast<size_t>(state.range(0)) * hvt::ONE_MiB / 2;
        desc.numThreads          = 4;

        auto manager = std::make_unique<hvt::DefaultBufferManager>(desc);
        benchmark::DoNotOptimize(manager.get());
    }
}
BENCHMARK(BM_BufferManagerInit)->Arg(256)->Arg(512)->Arg(1024)->Arg(2048);

// =============================================================================
// Buffer Creation Benchmarks
// =============================================================================

/// Benchmark: Single buffer creation
static void BM_BufferCreation(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_create";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t bufferSize = static_cast<size_t>(state.range(0)) * hvt::ONE_MiB;
    int bufferIndex   = 0;

    for (auto _ : state)
    {
        auto path   = SdfPath("/Buffer" + std::to_string(bufferIndex++));
        auto buffer = bufferManager.CreateBuffer(path, bufferSize);
        benchmark::DoNotOptimize(buffer.get());
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(bufferSize));
}
BENCHMARK(BM_BufferCreation)->Arg(1)->Arg(10)->Arg(50)->Arg(100);

/// Benchmark: Batch buffer creation
static void BM_BatchBufferCreation(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_batch";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t bufferCount = static_cast<size_t>(state.range(0));
    size_t bufferSize  = 10 * hvt::ONE_MiB;
    int batchIndex     = 0;

    for (auto _ : state)
    {
        std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
        buffers.reserve(bufferCount);

        for (size_t i = 0; i < bufferCount; ++i)
        {
            auto path = SdfPath("/Batch" + std::to_string(batchIndex) + "_Buffer" +
                                std::to_string(i));
            buffers.push_back(bufferManager.CreateBuffer(path, bufferSize));
        }

        benchmark::DoNotOptimize(buffers.data());
        memStats.Update(bufferManager.GetMemoryMonitor());
        ++batchIndex;
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(bufferCount));
}
BENCHMARK(BM_BatchBufferCreation)->Arg(10)->Arg(50)->Arg(100)->Arg(200);

// =============================================================================
// Paging Operation Benchmarks
// =============================================================================

/// Benchmark: Page to scene memory
static void BM_PageToSceneMemory(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_scene";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t bufferSize = static_cast<size_t>(state.range(0)) * hvt::ONE_MiB;

    // Pre-create buffers
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
    for (int i = 0; i < 100; ++i)
    {
        auto path = SdfPath("/Scene_Buffer" + std::to_string(i));
        buffers.push_back(bufferManager.CreateBuffer(path, bufferSize));
    }

    size_t idx = 0;
    for (auto _ : state)
    {
        auto& buffer = buffers[idx % buffers.size()];
        benchmark::DoNotOptimize(buffer->SwapToSceneMemory());
        memStats.Update(bufferManager.GetMemoryMonitor());
        ++idx;
    }

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(bufferSize));
}
BENCHMARK(BM_PageToSceneMemory)->Arg(1)->Arg(10)->Arg(50)->Arg(100);

/// Benchmark: Page to renderer memory
static void BM_PageToRendererMemory(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_renderer";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t bufferSize = static_cast<size_t>(state.range(0)) * hvt::ONE_MiB;

    // Pre-create buffers and move to scene memory
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
    for (int i = 0; i < 100; ++i)
    {
        auto path   = SdfPath("/Renderer_Buffer" + std::to_string(i));
        auto buffer = bufferManager.CreateBuffer(path, bufferSize);
        (void)buffer->SwapToSceneMemory();
        buffers.push_back(buffer);
    }

    size_t idx = 0;
    for (auto _ : state)
    {
        auto& buffer = buffers[idx % buffers.size()];
        benchmark::DoNotOptimize(buffer->SwapToRendererMemory());
        memStats.Update(bufferManager.GetMemoryMonitor());
        ++idx;
    }

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(bufferSize));
}
BENCHMARK(BM_PageToRendererMemory)->Arg(1)->Arg(10)->Arg(50)->Arg(100);

/// Benchmark: Swap to disk (scene memory to disk)
static void BM_PageToDisk(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_disk";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t bufferSize = static_cast<size_t>(state.range(0)) * hvt::ONE_MiB;

    // Pre-create buffers in scene memory
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
    for (int i = 0; i < 100; ++i)
    {
        auto path   = SdfPath("/Disk_Buffer" + std::to_string(i));
        auto buffer = bufferManager.CreateBuffer(path, bufferSize);
        (void)buffer->SwapToSceneMemory();
        buffers.push_back(buffer);
    }

    size_t idx = 0;
    for (auto _ : state)
    {
        auto& buffer = buffers[idx % buffers.size()];
        benchmark::DoNotOptimize(buffer->SwapSceneToDisk());
        // Re-page to scene for next iteration
        (void)buffer->SwapToSceneMemory();
        memStats.Update(bufferManager.GetMemoryMonitor());
        ++idx;
    }

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(bufferSize));
}
BENCHMARK(BM_PageToDisk)->Arg(1)->Arg(10)->Arg(50)->Arg(100);

// =============================================================================
// Async Operation Benchmarks
// =============================================================================

/// Benchmark: Async page to scene memory
static void BM_AsyncPageToSceneMemory(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_async_scene";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = static_cast<unsigned int>(state.range(1));

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t bufferSize = static_cast<size_t>(state.range(0)) * hvt::ONE_MiB;

    // Pre-create buffers
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
    for (int i = 0; i < 100; ++i)
    {
        auto path = SdfPath("/AsyncScene_Buffer" + std::to_string(i));
        buffers.push_back(bufferManager.CreateBuffer(path, bufferSize));
    }

    size_t idx = 0;
    for (auto _ : state)
    {
        auto& buffer = buffers[idx % buffers.size()];
        auto future  = bufferManager.PageToSceneMemoryAsync(buffer);
        benchmark::DoNotOptimize(future.get());
        memStats.Update(bufferManager.GetMemoryMonitor());
        ++idx;
    }

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(bufferSize));
}
BENCHMARK(BM_AsyncPageToSceneMemory)
    ->Args({ 10, 1 })
    ->Args({ 10, 2 })
    ->Args({ 10, 4 })
    ->Args({ 10, 8 })
    ->Args({ 50, 4 })
    ->Args({ 100, 4 });

/// Benchmark: Batch async operations
static void BM_BatchAsyncOperations(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_batch_async";
    desc.sceneMemoryLimit    = 4096 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 2048 * hvt::ONE_MiB;
    desc.numThreads          = 8;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t batchSize  = static_cast<size_t>(state.range(0));
    size_t bufferSize = 10 * hvt::ONE_MiB;

    // Pre-create buffers
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
    for (size_t i = 0; i < batchSize * 2; ++i)
    {
        auto path = SdfPath("/BatchAsync_Buffer" + std::to_string(i));
        buffers.push_back(bufferManager.CreateBuffer(path, bufferSize));
    }

    size_t batchIdx = 0;
    for (auto _ : state)
    {
        std::vector<std::future<bool>> futures;
        futures.reserve(batchSize);

        // Launch batch of async operations
        for (size_t i = 0; i < batchSize; ++i)
        {
            size_t bufIdx = (batchIdx * batchSize + i) % buffers.size();
            futures.push_back(bufferManager.PageToSceneMemoryAsync(buffers[bufIdx]));
        }

        // Wait for all to complete
        for (auto& future : futures)
        {
            benchmark::DoNotOptimize(future.get());
        }

        memStats.Update(bufferManager.GetMemoryMonitor());
        ++batchIdx;
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(batchSize));
}
BENCHMARK(BM_BatchAsyncOperations)->Arg(10)->Arg(25)->Arg(50)->Arg(100);

// =============================================================================
// Free Crawl Benchmarks
// =============================================================================

/// Benchmark: Synchronous free crawl
static void BM_FreeCrawlSync(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_freecrawl";
    desc.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 256 * hvt::ONE_MiB;
    desc.ageLimit            = 5;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    float crawlPercentage = static_cast<float>(state.range(0));

    // Create buffers with varying ages
    for (int i = 0; i < 50; ++i)
    {
        auto path   = SdfPath("/FreeCrawl_Buffer" + std::to_string(i));
        auto buffer = bufferManager.CreateBuffer(path, 20 * hvt::ONE_MiB);
        (void)buffer->SwapToRendererMemory();

        // Age some buffers
        if (i < 25)
        {
            buffer->UpdateFrameStamp(bufferManager.GetCurrentFrame() - 10);
        }
    }

    for (auto _ : state)
    {
        bufferManager.FreeCrawl(crawlPercentage);
        memStats.Update(bufferManager.GetMemoryMonitor());
        bufferManager.AdvanceFrame();
    }

    SetMemoryCounters(state, memStats);
}
BENCHMARK(BM_FreeCrawlSync)->Arg(25)->Arg(50)->Arg(75)->Arg(100);

/// Benchmark: Async free crawl
static void BM_FreeCrawlAsync(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_freecrawl_async";
    desc.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 256 * hvt::ONE_MiB;
    desc.ageLimit            = 5;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    float crawlPercentage = static_cast<float>(state.range(0));

    // Create buffers with varying ages
    for (int i = 0; i < 50; ++i)
    {
        auto path   = SdfPath("/FreeCrawlAsync_Buffer" + std::to_string(i));
        auto buffer = bufferManager.CreateBuffer(path, 20 * hvt::ONE_MiB);
        (void)buffer->SwapToRendererMemory();

        if (i < 25)
        {
            buffer->UpdateFrameStamp(bufferManager.GetCurrentFrame() - 10);
        }
    }

    for (auto _ : state)
    {
        auto futures = bufferManager.FreeCrawlAsync(crawlPercentage);

        // Wait for all operations
        for (auto& future : futures)
        {
            benchmark::DoNotOptimize(future.get());
        }

        memStats.Update(bufferManager.GetMemoryMonitor());
        bufferManager.AdvanceFrame();
    }

    SetMemoryCounters(state, memStats);
}
BENCHMARK(BM_FreeCrawlAsync)->Arg(25)->Arg(50)->Arg(75)->Arg(100);

// =============================================================================
// PageableValue Benchmarks
// =============================================================================

/// Benchmark: HdPageableValue creation with VtValue
static void BM_PageableValueCreation(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_pv_create";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t pointCount = static_cast<size_t>(state.range(0)) * 1000;

    // Pre-create test data
    auto points = GeneratePoints(pointCount);
    VtValue pointsValue(points);
    size_t estimatedSize = hvt::HdPageableValue::EstimateMemoryUsage(pointsValue);

    int idx = 0;
    for (auto _ : state)
    {
        auto pageableValue = std::make_shared<hvt::HdPageableValue>(
            SdfPath("/PV_points" + std::to_string(idx)), estimatedSize,
            hvt::HdBufferUsage::Static, bufferManager.GetPageFileManager(),
            bufferManager.GetMemoryMonitor(), [](const SdfPath&) {}, pointsValue,
            HdTokens->points);

        benchmark::DoNotOptimize(pageableValue.get());
        
        // Track metrics
        state.counters["Status"] = static_cast<double>(pageableValue->GetStatus());
        memStats.Update(bufferManager.GetMemoryMonitor());
        ++idx;
    }

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(pointCount * sizeof(GfVec3f)));
}
BENCHMARK(BM_PageableValueCreation)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

/// Benchmark: HdPageableValue GetValue (resident)
static void BM_PageableValueGetResident(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_pv_get";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    size_t pointCount = static_cast<size_t>(state.range(0)) * 1000;

    // Pre-create test data
    auto points = GeneratePoints(pointCount);
    VtValue pointsValue(points);
    size_t estimatedSize = hvt::HdPageableValue::EstimateMemoryUsage(pointsValue);

    auto pageableValue = std::make_shared<hvt::HdPageableValue>(SdfPath("/PV_resident"),
        estimatedSize, hvt::HdBufferUsage::Static, bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const SdfPath&) {}, pointsValue, HdTokens->points);

    for (auto _ : state)
    {
        auto value = pageableValue->GetValue();
        benchmark::DoNotOptimize(value);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(pointCount * sizeof(GfVec3f)));
}
BENCHMARK(BM_PageableValueGetResident)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

/// Benchmark: HdPageableValue implicit paging (page-in after page-out)
static void BM_PageableValueImplicitPaging(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_pv_implicit";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t pointCount = static_cast<size_t>(state.range(0)) * 1000;

    // Pre-create test data
    auto points = GeneratePoints(pointCount);
    VtValue pointsValue(points);
    size_t estimatedSize = hvt::HdPageableValue::EstimateMemoryUsage(pointsValue);

    auto pageableValue = std::make_shared<hvt::HdPageableValue>(SdfPath("/PV_implicit"),
        estimatedSize, hvt::HdBufferUsage::Static, bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const SdfPath&) {}, pointsValue, HdTokens->points);

    for (auto _ : state)
    {
        // Page out
        (void)pageableValue->SwapSceneToDisk();

        // Page in (implicit via GetValue)
        auto value = pageableValue->GetValue();
        benchmark::DoNotOptimize(value);
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(pointCount * sizeof(GfVec3f)) *
                            2); // Both page-out and page-in
}
BENCHMARK(BM_PageableValueImplicitPaging)->Arg(1)->Arg(10)->Arg(100);

// =============================================================================
// Serialization Benchmarks
// =============================================================================

/// Benchmark: Default serializer performance
static void BM_SerializerPerformance(benchmark::State& state)
{
    hvt::HdDefaultValueSerializer serializer;
    size_t dataSize = static_cast<size_t>(state.range(0)) * 1000;

    // Create test data based on type
    VtValue testValue;
    std::string typeDesc;

    switch (state.range(1))
    {
        case 0:
            testValue = VtValue(GeneratePoints(dataSize));
            typeDesc  = "Vec3f";
            break;
        case 1:
            testValue = VtValue(GenerateFloats(dataSize));
            typeDesc  = "Float";
            break;
        case 2:
            testValue = VtValue(GenerateIndices(dataSize));
            typeDesc  = "Int";
            break;
        default:
            testValue = VtValue(GeneratePoints(dataSize));
            typeDesc  = "Vec3f";
            break;
    }

    state.SetLabel(typeDesc);

    for (auto _ : state)
    {
        // Serialize
        auto serialized = serializer.Serialize(testValue);
        benchmark::DoNotOptimize(serialized.data());

        // Deserialize
        auto deserialized = serializer.Deserialize(serialized, TfToken(typeDesc));
        benchmark::DoNotOptimize(deserialized);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(testValue.GetArraySize()));
}
BENCHMARK(BM_SerializerPerformance)
    ->Args({ 10, 0 })  // Vec3f
    ->Args({ 10, 1 })  // Float
    ->Args({ 10, 2 })  // Int
    ->Args({ 100, 0 })
    ->Args({ 100, 1 })
    ->Args({ 100, 2 });

// =============================================================================
// Container Data Source Benchmarks
// =============================================================================

/// Benchmark: HdPageableContainerDataSource creation
static void BM_ContainerDataSourceCreation(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_container";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t attributeCount = static_cast<size_t>(state.range(0));
    size_t dataSize       = 10000;

    // Pre-create test data
    std::map<TfToken, VtValue> primData;
    auto points  = GeneratePoints(dataSize);
    auto normals = GeneratePoints(dataSize);
    auto indices = GenerateIndices(dataSize * 3);

    primData[HdTokens->points]  = VtValue(points);
    primData[HdTokens->normals] = VtValue(normals);
    primData[TfToken("indices")] = VtValue(indices);

    // Add extra attributes if needed
    for (size_t i = 3; i < attributeCount; ++i)
    {
        primData[TfToken("attr" + std::to_string(i))] = VtValue(GenerateFloats(dataSize));
    }

    int idx = 0;
    for (auto _ : state)
    {
        auto containerDs = hvt::HdPageableContainerDataSource::New(primData,
            SdfPath("/Container" + std::to_string(idx)),
            bufferManager.GetPageFileManager(), bufferManager.GetMemoryMonitor(),
            [](const SdfPath&) {});

        benchmark::DoNotOptimize(containerDs.get());
        memStats.Update(bufferManager.GetMemoryMonitor());
        ++idx;
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(attributeCount));
}
BENCHMARK(BM_ContainerDataSourceCreation)->Arg(3)->Arg(5)->Arg(10)->Arg(20);

/// Benchmark: HdPageableContainerDataSource Get
static void BM_ContainerDataSourceGet(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_container_get";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    size_t dataSize = static_cast<size_t>(state.range(0)) * 1000;

    // Pre-create test data
    std::map<TfToken, VtValue> primData;
    primData[HdTokens->points]  = VtValue(GeneratePoints(dataSize));
    primData[HdTokens->normals] = VtValue(GeneratePoints(dataSize));
    primData[TfToken("indices")] = VtValue(GenerateIndices(dataSize * 3));

    auto containerDs = hvt::HdPageableContainerDataSource::New(primData,
        SdfPath("/ContainerGet"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const SdfPath&) {});

    for (auto _ : state)
    {
        auto pointsDs  = containerDs->Get(HdTokens->points);
        auto normalsDs = containerDs->Get(HdTokens->normals);
        benchmark::DoNotOptimize(pointsDs.get());
        benchmark::DoNotOptimize(normalsDs.get());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 2);
}
BENCHMARK(BM_ContainerDataSourceGet)->Arg(1)->Arg(10)->Arg(100);

/// Benchmark: HdPageableContainerDataSource per-element paging
static void BM_ContainerDataSourcePaging(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_container_page";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    size_t dataSize = static_cast<size_t>(state.range(0)) * 1000;

    // Pre-create test data
    std::map<TfToken, VtValue> primData;
    primData[HdTokens->points]  = VtValue(GeneratePoints(dataSize));
    primData[HdTokens->normals] = VtValue(GeneratePoints(dataSize));
    primData[TfToken("indices")] = VtValue(GenerateIndices(dataSize * 3));

    auto containerDs = hvt::HdPageableContainerDataSource::New(primData,
        SdfPath("/ContainerPaging"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const SdfPath&) {});

    for (auto _ : state)
    {
        containerDs->PageOutElement(HdTokens->points);

        // Implicitly paging via Get
        auto pointsDs = containerDs->Get(HdTokens->points);
        benchmark::DoNotOptimize(pointsDs.get());
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(dataSize * sizeof(GfVec3f)) * 2);
}
BENCHMARK(BM_ContainerDataSourcePaging)->Arg(1)->Arg(10)->Arg(100);

// =============================================================================
// Sampled Data Source Benchmarks
// =============================================================================

/// Benchmark: HdPageableSampledDataSource GetValue
static void BM_SampledDataSourceGetValue(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_sampled";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    size_t sampleCount = static_cast<size_t>(state.range(0));
    size_t dataSize    = 10000;

    // Pre-create test data
    std::map<HdSampledDataSource::Time, VtValue> samples;
    for (size_t i = 0; i < sampleCount; ++i)
    {
        samples[static_cast<float>(i)] = VtValue(GeneratePoints(dataSize));
    }

    auto sampledDs = hvt::HdPageableSampledDataSource::New(samples, SdfPath("/Sampled"),
        HdTokens->points, bufferManager.GetPageFileManager(), bufferManager.GetMemoryMonitor(),
        [](const SdfPath&) {});

    float time = 0.0f;
    for (auto _ : state)
    {
        auto value = sampledDs->GetValue(time);
        benchmark::DoNotOptimize(value);
        time = std::fmod(time + 0.5f, static_cast<float>(sampleCount));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_SampledDataSourceGetValue)->Arg(1)->Arg(5)->Arg(10)->Arg(24);

// =============================================================================
// Thread Contention Benchmarks
// =============================================================================

/// Benchmark: Concurrent buffer access
static void BM_ConcurrentBufferAccess(benchmark::State& state)
{
    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_concurrent";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 8;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    int threadCount = static_cast<int>(state.range(0));
    size_t dataSize = 10000;

    // Create shared pageable value
    auto points          = GeneratePoints(dataSize);
    VtValue pointsValue(points);
    size_t estimatedSize = hvt::HdPageableValue::EstimateMemoryUsage(pointsValue);

    auto pageableValue = std::make_shared<hvt::HdPageableValue>(SdfPath("/Concurrent"),
        estimatedSize, hvt::HdBufferUsage::Static, bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const SdfPath&) {}, pointsValue, HdTokens->points);

    for (auto _ : state)
    {
        std::vector<std::thread> threads;
        std::atomic<int> successCount { 0 };

        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back(
                [&pageableValue, &successCount, t]()
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        // Mix of operations
                        if (i % 10 == t % 10)
                        {
                            (void)pageableValue->SwapSceneToDisk();
                        }
                        else
                        {
                            auto value = pageableValue->GetValue();
                            if (!value.IsEmpty())
                            {
                                ++successCount;
                            }
                        }
                    }
                });
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        benchmark::DoNotOptimize(successCount.load());
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * threadCount * 100);
}
BENCHMARK(BM_ConcurrentBufferAccess)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

// =============================================================================
// Memory Pressure Benchmarks
// =============================================================================

/// Benchmark: Performance under memory pressure
static void BM_MemoryPressurePerformance(benchmark::State& state)
{
    size_t memoryLimitMB = static_cast<size_t>(state.range(0));

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_pressure";
    desc.sceneMemoryLimit    = memoryLimitMB * hvt::ONE_MiB;
    desc.rendererMemoryLimit = memoryLimitMB * hvt::ONE_MiB / 2;
    desc.ageLimit            = 5;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;

    // Create buffers to exceed memory limit (simulate pressure)
    std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
    size_t totalBufferSize = memoryLimitMB * 2 * hvt::ONE_MiB; // 2x the limit
    size_t bufferSize      = 20 * hvt::ONE_MiB;
    size_t bufferCount     = totalBufferSize / bufferSize;

    for (size_t i = 0; i < bufferCount; ++i)
    {
        auto path   = SdfPath("/Pressure_Buffer" + std::to_string(i));
        auto buffer = bufferManager.CreateBuffer(path, bufferSize);
        (void)buffer->SwapToSceneMemory();
        buffers.push_back(buffer);

        // Age older buffers
        if (i < bufferCount / 2)
        {
            buffer->UpdateFrameStamp(bufferManager.GetCurrentFrame() - 10);
        }
    }

    size_t idx = 0;
    for (auto _ : state)
    {
        // Access a buffer (may trigger paging)
        auto& buffer = buffers[idx % buffers.size()];
        (void)buffer->SwapToRendererMemory();

        // Advance frame to trigger potential cleanup
        bufferManager.AdvanceFrame();

        // Check if cleanup is needed
        float pressure = bufferManager.GetMemoryMonitor()->GetSceneMemoryPressure();
        if (pressure > 0.8f)
        {
            bufferManager.FreeCrawl(50.0f);
        }

        memStats.Update(bufferManager.GetMemoryMonitor());
        ++idx;
    }

    SetMemoryCounters(state, memStats);
    state.counters["MemoryPressure"] = benchmark::Counter(
        static_cast<double>(bufferManager.GetMemoryMonitor()->GetSceneMemoryPressure() * 100));
}
BENCHMARK(BM_MemoryPressurePerformance)->Arg(128)->Arg(256)->Arg(512);

// =============================================================================
// DataSourceManager Benchmarks
// =============================================================================

/// Benchmark: HdPageableDataSourceManager buffer management
static void BM_DataSourceManagerOperations(benchmark::State& state)
{
    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory       = std::filesystem::temp_directory_path() / "hvt_bench_dsm";
    config.sceneMemoryLimit        = 512 * hvt::ONE_MiB;
    config.rendererMemoryLimit     = 256 * hvt::ONE_MiB;
    config.enableBackgroundCleanup = false;
    config.numThreads              = 4;

    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);
    MemoryStats memStats;
    size_t dataSize = static_cast<size_t>(state.range(0)) * 1000;

    // Generate test data
    auto points = GeneratePoints(dataSize);

    int idx = 0;
    for (auto _ : state)
    {
        // Create buffer
        auto path   = SdfPath("/DSM_buffer" + std::to_string(idx));
        auto buffer = manager->GetOrCreateBuffer(path, VtValue(points), HdTokens->points);

        benchmark::DoNotOptimize(buffer.get());

        // Access the value
        auto pageableValue = std::dynamic_pointer_cast<hvt::HdPageableValue>(buffer);
        if (pageableValue)
        {
            auto value = pageableValue->GetValue();
            benchmark::DoNotOptimize(value);
        }

        memStats.Update(manager->GetMemoryMonitor());
        ++idx;
    }

    // Report manager metrics
    state.counters["ResidentBuffers"] = benchmark::Counter(
        static_cast<double>(manager->GetResidentBufferCount()));
    state.counters["PagedOutBuffers"] = benchmark::Counter(
        static_cast<double>(manager->GetPagedOutBufferCount()));
    state.counters["MemoryPressure"] = benchmark::Counter(
        manager->GetMemoryPressure() * 100);

    SetMemoryCounters(state, memStats);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(dataSize * sizeof(GfVec3f)));
}
BENCHMARK(BM_DataSourceManagerOperations)->Arg(1)->Arg(10)->Arg(100);

// =============================================================================
// End-to-End Workflow Benchmarks
// =============================================================================

/// Benchmark: Simulated rendering workflow
static void BM_RenderingWorkflow(benchmark::State& state)
{
    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory       = std::filesystem::temp_directory_path() / "hvt_bench_workflow";
    config.sceneMemoryLimit        = 512 * hvt::ONE_MiB;
    config.rendererMemoryLimit     = 256 * hvt::ONE_MiB;
    config.freeCrawlPercentage     = 20.0f;
    config.enableBackgroundCleanup = true;
    config.ageLimit                = 10;
    config.numThreads              = 4;

    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);
    MemoryStats memStats;
    size_t meshCount = static_cast<size_t>(state.range(0));

    // Pre-create mesh containers
    std::vector<hvt::HdPageableContainerDataSource::Handle> meshContainers;
    for (size_t i = 0; i < meshCount; ++i)
    {
        std::map<TfToken, VtValue> meshData;
        meshData[HdTokens->points]  = VtValue(GeneratePoints(5000));
        meshData[HdTokens->normals] = VtValue(GeneratePoints(5000));
        meshData[TfToken("indices")] = VtValue(GenerateIndices(15000));

        auto path = SdfPath("/Workflow_Mesh" + std::to_string(i));
        auto container =
            hvt::HdPageableContainerDataSource::New(meshData, path, manager->GetPageFileManager(),
                manager->GetMemoryMonitor(), [](const SdfPath&) {});

        meshContainers.push_back(container);
    }

    int frameCount = 0;
    for (auto _ : state)
    {
        // Simulate frame: access visible meshes
        size_t visibleStart = frameCount % meshCount;
        size_t visibleCount = std::min(meshCount / 2, meshCount - visibleStart);

        for (size_t i = 0; i < visibleCount; ++i)
        {
            size_t idx        = (visibleStart + i) % meshCount;
            auto& container = meshContainers[idx];

            // Access mesh data
            auto pointsDs = container->Get(HdTokens->points);
            benchmark::DoNotOptimize(pointsDs.get());
        }

        // Page out non-visible meshes (simulate culling)
        if (frameCount > 5)
        {
            size_t cullIdx = (visibleStart + meshCount - 1) % meshCount;
            meshContainers[cullIdx]->PageOutElement(HdTokens->points);
        }

        manager->AdvanceFrame();
        memStats.Update(manager->GetMemoryMonitor());
        ++frameCount;
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(meshCount / 2));
}
BENCHMARK(BM_RenderingWorkflow)->Arg(10)->Arg(50)->Arg(100);

// =============================================================================
// Advanced Multithreading Benchmarks
// =============================================================================

/// Benchmark: Producer-Consumer.
/// N producers create buffers, M consumers read them.
static void BM_ProducerConsumer(benchmark::State& state)
{
    int producerCount = static_cast<int>(state.range(0));
    int consumerCount = static_cast<int>(state.range(1));

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_prodcons";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = 8;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    constexpr int kItemsPerProducer = 50;

    for (auto _ : state)
    {
        std::mutex queueMutex;
        std::condition_variable queueCv;
        std::queue<std::shared_ptr<hvt::HdPageableValue>> readyQueue;
        std::atomic<bool> producersDone { false };
        std::atomic<int> consumedCount { 0 };

        // Producers
        std::vector<std::thread> producers;
        for (int p = 0; p < producerCount; ++p)
        {
            producers.emplace_back(
                [&, p]()
                {
                    auto points = GeneratePoints(5000);
                    VtValue pointsValue(points);
                    size_t estimatedSize = hvt::HdPageableValue::EstimateMemoryUsage(pointsValue);

                    for (int i = 0; i < kItemsPerProducer; ++i)
                    {
                        auto path = SdfPath(TfStringPrintf("/PC_P%d_B%d", p, i));
                        auto pv = std::make_shared<hvt::HdPageableValue>(path, estimatedSize,
                            hvt::HdBufferUsage::Static, bufferManager.GetPageFileManager(),
                            bufferManager.GetMemoryMonitor(), [](const SdfPath&) {}, pointsValue,
                            HdTokens->points);

                        {
                            std::lock_guard<std::mutex> lk(queueMutex);
                            readyQueue.push(pv);
                        }
                        queueCv.notify_one();
                    }
                });
        }

        // Consumers
        std::vector<std::thread> consumers;
        for (int c = 0; c < consumerCount; ++c)
        {
            consumers.emplace_back(
                [&]()
                {
                    while (true)
                    {
                        std::shared_ptr<hvt::HdPageableValue> item;
                        {
                            std::unique_lock<std::mutex> lk(queueMutex);
                            queueCv.wait(lk,
                                [&] { return !readyQueue.empty() || producersDone.load(); });
                            if (readyQueue.empty() && producersDone.load())
                            {
                                return;
                            }
                            if (readyQueue.empty())
                            {
                                continue;
                            }
                            item = readyQueue.front();
                            readyQueue.pop();
                        }
                        auto val = item->GetValue();
                        benchmark::DoNotOptimize(val);
                        ++consumedCount;
                    }
                });
        }

        for (auto& t : producers)
        {
            t.join();
        }
        producersDone = true;
        queueCv.notify_all();
        for (auto& t : consumers)
        {
            t.join();
        }

        benchmark::DoNotOptimize(consumedCount.load());
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * producerCount *
                            kItemsPerProducer);
}
BENCHMARK(BM_ProducerConsumer)
    ->Args({ 1, 1 })
    ->Args({ 2, 2 })
    ->Args({ 4, 2 })
    ->Args({ 4, 4 })
    ->Args({ 2, 8 });

/// Benchmark: Read-Write contention on a shared container
static void BM_ReadWriteContention(benchmark::State& state)
{
    int readerCount = static_cast<int>(state.range(0));
    int writerCount = static_cast<int>(state.range(1));

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_rwcont";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;

    std::map<TfToken, VtValue> meshData;
    meshData[HdTokens->points]  = VtValue(GeneratePoints(10000));
    meshData[HdTokens->normals] = VtValue(GeneratePoints(10000));
    meshData[TfToken("indices")] = VtValue(GenerateIndices(30000));

    auto container = hvt::HdPageableContainerDataSource::New(meshData,
        SdfPath("/RWContention"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const SdfPath&) {});

    constexpr int kOpsPerThread = 200;

    for (auto _ : state)
    {
        std::atomic<int> readSuccess { 0 };
        std::atomic<int> writeSuccess { 0 };
        std::vector<std::thread> threads;

        // Mix of operations
        for (int r = 0; r < readerCount; ++r)
        {
            threads.emplace_back(
                [&]()
                {
                    for (int i = 0; i < kOpsPerThread; ++i)
                    {
                        auto ds = container->Get(HdTokens->points);
                        if (ds)
                        {
                            ++readSuccess;
                        }
                    }
                });
        }

        for (int w = 0; w < writerCount; ++w)
        {
            threads.emplace_back(
                [&]()
                {
                    for (int i = 0; i < kOpsPerThread; ++i)
                    {
                        if (i % 2 == 0)
                        {
                            if (container->PageOutElement(HdTokens->normals))
                            {
                                ++writeSuccess;
                            }
                        }
                        else
                        {
                            if (container->PageInElement(HdTokens->normals))
                            {
                                ++writeSuccess;
                            }
                        }
                    }
                });
        }

        for (auto& t : threads)
        {
            t.join();
        }

        benchmark::DoNotOptimize(readSuccess.load());
        benchmark::DoNotOptimize(writeSuccess.load());
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            (readerCount + writerCount) * kOpsPerThread);
}
BENCHMARK(BM_ReadWriteContention)
    ->Args({ 4, 1 })
    ->Args({ 4, 2 })
    ->Args({ 8, 2 })
    ->Args({ 8, 4 });

/// Benchmark: Mixed workload.
/// Create, read, page-out, page-in across threads.
static void BM_MixedWorkload(benchmark::State& state)
{
    int threadCount = static_cast<int>(state.range(0));

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_mixed";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = 8;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;
    constexpr int kOpsPerThread = 100;

    for (auto _ : state)
    {
        std::vector<std::thread> threads;
        std::atomic<int> totalOps { 0 };

        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back(
                [&, t]()
                {
                    auto points = GeneratePoints(2000);
                    VtValue pointsValue(points);

                    std::map<TfToken, VtValue> meshData;
                    meshData[HdTokens->points] = pointsValue;

                    auto container = hvt::HdPageableContainerDataSource::New(meshData,
                        SdfPath(TfStringPrintf("/Mixed_T%d", t)),
                        bufferManager.GetPageFileManager(), bufferManager.GetMemoryMonitor(),
                        [](const SdfPath&) {});

                    for (int i = 0; i < kOpsPerThread; ++i)
                    {
                        int op = i % 4;
                        switch (op)
                        {
                        case 0: // Read
                        {
                            auto ds = container->Get(HdTokens->points);
                            benchmark::DoNotOptimize(ds);
                            break;
                        }
                        case 1: // Page out
                            container->PageOutElement(HdTokens->points);
                            break;
                        case 2: // Page in (via Get)
                        {
                            auto ds = container->Get(HdTokens->points);
                            benchmark::DoNotOptimize(ds);
                            break;
                        }
                        case 3: // Swap whole container
                            container->SwapSceneToDisk();
                            container->SwapToSceneMemory();
                            break;
                        }
                        ++totalOps;
                    }
                });
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        benchmark::DoNotOptimize(totalOps.load());
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * threadCount *
                            kOpsPerThread);
}
BENCHMARK(BM_MixedWorkload)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

/// Benchmark: Pipeline parallelism.
/// Stage 1 loads; Stage 2 processes; Stage 3 pages out.
static void BM_PipelineParallelism(benchmark::State& state)
{
    int pipelineDepth = static_cast<int>(state.range(0));

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_pipeline";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = 8;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;

    for (auto _ : state)
    {
        // Stage queues
        std::mutex loadedMutex, processedMutex;
        std::condition_variable loadedCv, processedCv;
        std::queue<hvt::HdPageableContainerDataSource::Handle> loadedQueue;
        std::queue<hvt::HdPageableContainerDataSource::Handle> processedQueue;
        std::atomic<bool> loadDone { false };
        std::atomic<bool> processDone { false };
        std::atomic<int> pagedOutCount { 0 };

        // Stage 1: Load data
        std::thread loader(
            [&]()
            {
                for (int i = 0; i < pipelineDepth; ++i)
                {
                    std::map<TfToken, VtValue> meshData;
                    meshData[HdTokens->points] = VtValue(GeneratePoints(3000));
                    meshData[HdTokens->normals] = VtValue(GeneratePoints(3000));

                    auto container = hvt::HdPageableContainerDataSource::New(meshData,
                        SdfPath(TfStringPrintf("/Pipeline_M%d", i)),
                        bufferManager.GetPageFileManager(), bufferManager.GetMemoryMonitor(),
                        [](const SdfPath&) {});

                    {
                        std::lock_guard<std::mutex> lk(loadedMutex);
                        loadedQueue.push(container);
                    }
                    loadedCv.notify_one();
                }
                loadDone = true;
                loadedCv.notify_all();
            });

        // Stage 2: Process (read + validate)
        std::thread processor(
            [&]()
            {
                while (true)
                {
                    hvt::HdPageableContainerDataSource::Handle item;
                    {
                        std::unique_lock<std::mutex> lk(loadedMutex);
                        loadedCv.wait(lk,
                            [&] { return !loadedQueue.empty() || loadDone.load(); });
                        if (loadedQueue.empty() && loadDone.load())
                        {
                            break;
                        }
                        if (loadedQueue.empty())
                        {
                            continue;
                        }
                        item = loadedQueue.front();
                        loadedQueue.pop();
                    }

                    auto pts = item->Get(HdTokens->points);
                    benchmark::DoNotOptimize(pts);

                    {
                        std::lock_guard<std::mutex> lk(processedMutex);
                        processedQueue.push(item);
                    }
                    processedCv.notify_one();
                }
                processDone = true;
                processedCv.notify_all();
            });

        // Stage 3: Page out (cleanup)
        std::thread pager(
            [&]()
            {
                while (true)
                {
                    hvt::HdPageableContainerDataSource::Handle item;
                    {
                        std::unique_lock<std::mutex> lk(processedMutex);
                        processedCv.wait(lk,
                            [&] { return !processedQueue.empty() || processDone.load(); });
                        if (processedQueue.empty() && processDone.load())
                        {
                            break;
                        }
                        if (processedQueue.empty())
                        {
                            continue;
                        }
                        item = processedQueue.front();
                        processedQueue.pop();
                    }

                    item->SwapSceneToDisk();
                    ++pagedOutCount;
                }
            });

        loader.join();
        processor.join();
        pager.join();

        benchmark::DoNotOptimize(pagedOutCount.load());
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * pipelineDepth);
}
BENCHMARK(BM_PipelineParallelism)->Arg(10)->Arg(50)->Arg(100);

/// Benchmark: Packed concurrent container.
static void BM_PackedConcurrentContainer(benchmark::State& state)
{
    int threadCount = static_cast<int>(state.range(0));

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_packed_conc";
    desc.sceneMemoryLimit    = 1024 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 512 * hvt::ONE_MiB;
    desc.numThreads          = 8;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;

    // Pre-create test data
    std::map<TfToken, VtValue> meshData;
    meshData[HdTokens->points]   = VtValue(GeneratePoints(5000));
    meshData[HdTokens->normals]  = VtValue(GeneratePoints(5000));
    meshData[TfToken("indices")] = VtValue(GenerateIndices(15000));

    auto container = hvt::HdPageableContainerDataSource::New(meshData,
        SdfPath("/PackedConc"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const SdfPath&) {});

    constexpr int kOpsPerThread = 100;

    // Multiple threads do SwapSceneToDisk, SwapToSceneMemory, and Get
    // on a shared container simultaneously.
    for (auto _ : state)
    {
        std::atomic<int> readSuccess { 0 };
        std::vector<std::thread> threads;

        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back(
#if defined(_MSC_VER)
                [&container, &readSuccess, t, kOpsPerThread]()
#else
                [&container, &readSuccess, t]()
#endif
                {
                    for (int i = 0; i < kOpsPerThread; ++i)
                    {
                        // Mix of operations
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
                            auto ds = container->Get(HdTokens->points);
                            if (ds)
                            {
                                ++readSuccess;
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

        benchmark::DoNotOptimize(readSuccess.load());
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * threadCount *
                            kOpsPerThread);
}
BENCHMARK(BM_PackedConcurrentContainer)->Arg(2)->Arg(4)->Arg(6)->Arg(8);

/// Benchmark: Background cleanup under memory pressure.
static void BM_BackgroundCleanupUnderPressure(benchmark::State& state)
{
    size_t bufferCount = static_cast<size_t>(state.range(0));

    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory =
        std::filesystem::temp_directory_path() /
        ("hvt_bench_bg_pressure_" + std::to_string(bufferCount));
    config.sceneMemoryLimit        = 2 * hvt::ONE_MiB;
    config.rendererMemoryLimit     = 1 * hvt::ONE_MiB;
    config.freeCrawlPercentage     = 50.0f;
    config.freeCrawlIntervalMs     = 50;
    config.enableBackgroundCleanup = true;
    config.ageLimit                = 2;

    MemoryStats memStats;
    int iteration = 0;

    // Low memory limits with many buffers forces the background cleanup thread to
    // actively page out data.
    for (auto _ : state)
    {
        state.PauseTiming();
        auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);
        std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
        for (size_t i = 0; i < bufferCount; ++i)
        {
            VtFloatArray data(100000); // ~400KB each -> 10 buffers = 4MB >> 2MB limit
            std::fill(data.begin(), data.end(), static_cast<float>(i));

            auto path = SdfPath(TfStringPrintf("/BGP_Iter%d_B%zu", iteration, i));
            auto buffer = manager->GetOrCreateBuffer(path, VtValue(data), TfToken("float[]"));
            buffers.push_back(buffer);
            manager->AdvanceFrame();
        }
        state.ResumeTiming();

        // Let background cleanup work while we access buffers
        for (size_t i = 0; i < bufferCount; ++i)
        {
            auto pv = std::dynamic_pointer_cast<hvt::HdPageableValue>(buffers[i]);
            if (pv)
            {
                auto val = pv->GetValue();
                benchmark::DoNotOptimize(val);
            }
            manager->AdvanceFrame();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        memStats.Update(manager->GetMemoryMonitor());

        state.PauseTiming();
        state.counters["ResidentBuffers"] =
            benchmark::Counter(static_cast<double>(manager->GetResidentBufferCount()));
        state.counters["PagedOutBuffers"] =
            benchmark::Counter(static_cast<double>(manager->GetPagedOutBufferCount()));
        state.ResumeTiming();
        ++iteration;
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(bufferCount));
}
BENCHMARK(BM_BackgroundCleanupUnderPressure)->Arg(10)->Arg(20)->Arg(30);

/// Benchmark: Async swap round-trip.
/// Batch of SwapSceneToDiskAsync then batch of SwapToSceneMemoryAsync using HdPageableValue.
static void BM_AsyncSwapOperations(benchmark::State& state)
{
    int numThreads  = static_cast<int>(state.range(0));
    int batchSize   = static_cast<int>(state.range(1));
    size_t dataSize = 5000;

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory =
        std::filesystem::temp_directory_path() /
        ("hvt_bench_async_swap_" + std::to_string(numThreads) + "_" + std::to_string(batchSize));
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = static_cast<unsigned int>(numThreads);

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;

    // Pre-create test data
    auto points    = GeneratePoints(dataSize);
    VtValue ptsVal(points);
    size_t estSize = hvt::HdPageableValue::EstimateMemoryUsage(ptsVal);

    std::vector<std::shared_ptr<hvt::HdPageableValue>> buffers;
    for (int i = 0; i < batchSize; ++i)
    {
        auto pv = std::make_shared<hvt::HdPageableValue>(
            SdfPath(TfStringPrintf("/AsyncSwap_B%d", i)), estSize,
            hvt::HdBufferUsage::Static, bufferManager.GetPageFileManager(),
            bufferManager.GetMemoryMonitor(), [](const SdfPath&) {}, ptsVal,
            HdTokens->points);
        buffers.push_back(pv);
    }

    for (auto _ : state)
    {
        // Batch swap to disk
        std::vector<std::future<bool>> toDiskFuts;
        toDiskFuts.reserve(batchSize);
        for (auto& pv : buffers)
        {
            toDiskFuts.push_back(bufferManager.SwapSceneToDiskAsync(pv));
        }
        for (auto& f : toDiskFuts)
        {
            benchmark::DoNotOptimize(f.get());
        }

        // Batch swap back to scene
        std::vector<std::future<bool>> toSceneFuts;
        toSceneFuts.reserve(batchSize);
        for (auto& pv : buffers)
        {
            toSceneFuts.push_back(bufferManager.SwapToSceneMemoryAsync(pv));
        }
        for (auto& f : toSceneFuts)
        {
            benchmark::DoNotOptimize(f.get());
        }

        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * batchSize * 2);
}
BENCHMARK(BM_AsyncSwapOperations)
    ->Args({ 2, 10 })
    ->Args({ 4, 10 })
    ->Args({ 8, 10 })
    ->Args({ 4, 20 });

/// Benchmark: Async batch release operations (using HdPageableValue).
static void BM_AsyncReleaseOperations(benchmark::State& state)
{
    size_t batchSize = static_cast<size_t>(state.range(0));

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_async_release";
    desc.sceneMemoryLimit    = 4096 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 2048 * hvt::ONE_MiB;
    desc.numThreads          = 8;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;

    auto floats  = GenerateFloats(5000);
    VtValue fVal(floats);
    size_t estSize = hvt::HdPageableValue::EstimateMemoryUsage(fVal);

    int batchIdx = 0;
    for (auto _ : state)
    {
        state.PauseTiming();
        std::vector<std::shared_ptr<hvt::HdPageableValue>> buffers;
        for (size_t i = 0; i < batchSize; ++i)
        {
            auto pv = std::make_shared<hvt::HdPageableValue>(
                SdfPath(TfStringPrintf("/AsyncRel_Batch%d_B%zu", batchIdx, i)), estSize,
                hvt::HdBufferUsage::Static, bufferManager.GetPageFileManager(),
                bufferManager.GetMemoryMonitor(), [](const SdfPath&) {}, fVal,
                TfToken("float[]"));
            (void)pv->SwapSceneToDisk();
            (void)pv->SwapToSceneMemory();
            buffers.push_back(pv);
        }
        state.ResumeTiming();

        std::vector<std::future<void>> futures;
        futures.reserve(batchSize * 2);
        for (auto& buf : buffers)
        {
            futures.push_back(bufferManager.ReleaseSceneBufferAsync(buf));
            futures.push_back(bufferManager.ReleaseDiskPageAsync(buf));
        }
        for (auto& f : futures)
        {
            f.get();
        }

        memStats.Update(bufferManager.GetMemoryMonitor());
        ++batchIdx;
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(batchSize));
}
BENCHMARK(BM_AsyncReleaseOperations)->Arg(10)->Arg(25)->Arg(50);

/// Benchmark: Concurrent DataSourceManager access.
static void BM_ConcurrentDataSourceManager(benchmark::State& state)
{
    int threadCount = static_cast<int>(state.range(0));

    hvt::HdPageableDataSourceManager::Config config;
    config.pageFileDirectory       = std::filesystem::temp_directory_path() / "hvt_bench_conc_dsm";
    config.sceneMemoryLimit        = 512 * hvt::ONE_MiB;
    config.rendererMemoryLimit     = 256 * hvt::ONE_MiB;
    config.enableBackgroundCleanup = false;
    config.numThreads              = 8;

    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(config);
    MemoryStats memStats;
    constexpr int kItemsPerThread = 20;

    for (auto _ : state)
    {
        std::atomic<int> opsCompleted { 0 };
        std::vector<std::thread> threads;

        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back(
#if defined(_MSC_VER)
                [&manager, &opsCompleted, t, kItemsPerThread]()
#else
                [&manager, &opsCompleted, t]()
#endif
                {
                    auto points = GeneratePoints(2000);
                    VtValue ptsVal(points);

                    // Multiple threads doing GetOrCreateBuffer and GetValue on a shared manager.
                    for (int i = 0; i < kItemsPerThread; ++i)
                    {
                        auto path = SdfPath(TfStringPrintf("/ConcDSM_T%d_B%d", t, i));
                        auto buf  = manager->GetOrCreateBuffer(path, ptsVal, HdTokens->points);
                        auto pv   = std::dynamic_pointer_cast<hvt::HdPageableValue>(buf);
                        if (pv)
                        {
                            auto val = pv->GetValue();
                            benchmark::DoNotOptimize(val);

                            if (i % 3 == 0)
                            {
                                pv->SwapSceneToDisk();
                            }
                        }
                        ++opsCompleted;
                    }
                });
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        benchmark::DoNotOptimize(opsCompleted.load());
        memStats.Update(manager->GetMemoryMonitor());
    }

    state.counters["ResidentBuffers"] =
        benchmark::Counter(static_cast<double>(manager->GetResidentBufferCount()));
    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * threadCount *
                            kItemsPerThread);
}
BENCHMARK(BM_ConcurrentDataSourceManager)->Arg(2)->Arg(4)->Arg(8);

// =============================================================================
// Strategy & Comparison Benchmarks
// =============================================================================

/// Benchmark: Compare different BufferManager strategy combinations
static void BM_StrategyComparison(benchmark::State& state)
{
    int strategyIdx = static_cast<int>(state.range(0));
    size_t bufferCount = 50;
    size_t bufferSize = 20 * hvt::ONE_MiB;

    std::string strategyLabel;

    // Prepare workload for various strategies.
    auto runWorkload = [&](auto& bufferManager)
    {
        std::vector<std::shared_ptr<hvt::HdPageableBufferCore>> buffers;
        for (size_t i = 0; i < bufferCount; ++i)
        {
            auto path = SdfPath("/Strategy_B" + std::to_string(i));
            auto buffer = bufferManager.CreateBuffer(path, bufferSize);
            (void)buffer->SwapToSceneMemory();
            buffers.push_back(buffer);

            if (i < bufferCount / 2)
            {
                buffer->UpdateFrameStamp(bufferManager.GetCurrentFrame() - 10);
            }
        }

        for (auto _ : state)
        {
            bufferManager.FreeCrawl(50.0f);
            bufferManager.AdvanceFrame();

            for (size_t i = 0; i < 10; ++i)
            {
                auto& buf = buffers[i % buffers.size()];
                (void)buf->SwapToSceneMemory();
            }
        }

        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 10);
    };

    auto makeDesc = [&](const std::string& subdir)
    {
        hvt::DefaultBufferManager::InitializeDesc desc;
        desc.pageFileDirectory   = std::filesystem::temp_directory_path() / subdir;
        desc.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
        desc.rendererMemoryLimit = 256 * hvt::ONE_MiB;
        desc.ageLimit            = 5;
        desc.numThreads          = 4;
        return desc;
    };

    // Run workload for various strategies according to the test index.
    switch (strategyIdx)
    {
    case 0:
    {
        strategyLabel = "AgeBased+OldestFirst";
        auto desc = makeDesc("hvt_bench_strat0");
        hvt::DefaultBufferManager mgr(desc);
        runWorkload(mgr);
        break;
    }
    case 1:
    {
        strategyLabel = "PressureBased+LRU";
        using Mgr = hvt::PressureBasedLRUBufferManager;
        Mgr::InitializeDesc d;
        d.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_strat1";
        d.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
        d.rendererMemoryLimit = 256 * hvt::ONE_MiB;
        d.ageLimit            = 5;
        d.numThreads          = 4;
        Mgr mgr(d);
        runWorkload(mgr);
        break;
    }
    case 2:
    {
        strategyLabel = "Hybrid+LargestFirst";
        using Mgr = hvt::HdPageableBufferManager<
            hvt::HdPagingStrategies::HybridStrategy,
            hvt::HdPagingStrategies::LargestFirstSelectionStrategy>;
        Mgr::InitializeDesc d;
        d.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_strat2";
        d.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
        d.rendererMemoryLimit = 256 * hvt::ONE_MiB;
        d.ageLimit            = 5;
        d.numThreads          = 4;
        Mgr mgr(d);
        runWorkload(mgr);
        break;
    }
    case 3:
    {
        strategyLabel = "Conservative+FIFO";
        using Mgr = hvt::ConservativeFIFOBufferManager;
        Mgr::InitializeDesc d;
        d.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_strat3";
        d.sceneMemoryLimit    = 512 * hvt::ONE_MiB;
        d.rendererMemoryLimit = 256 * hvt::ONE_MiB;
        d.ageLimit            = 5;
        d.numThreads          = 4;
        Mgr mgr(d);
        runWorkload(mgr);
        break;
    }
    default:
        break;
    }

    state.SetLabel(strategyLabel);
}
BENCHMARK(BM_StrategyComparison)->Arg(0)->Arg(1)->Arg(2)->Arg(3);

/// Benchmark: Packed container page-out/in vs individual element paging
static void BM_PackedVsPerElement(benchmark::State& state)
{
    bool usePacked = (state.range(0) == 1);
    size_t elementCount = static_cast<size_t>(state.range(1));

    hvt::DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = std::filesystem::temp_directory_path() / "hvt_bench_packed";
    desc.sceneMemoryLimit    = 2048 * hvt::ONE_MiB;
    desc.rendererMemoryLimit = 1024 * hvt::ONE_MiB;
    desc.numThreads          = 4;

    hvt::DefaultBufferManager bufferManager(desc);
    MemoryStats memStats;

    // Pre-create test data
    std::map<TfToken, VtValue> meshData;
    for (size_t i = 0; i < elementCount; ++i)
    {
        meshData[TfToken("attr_" + std::to_string(i))] = VtValue(GeneratePoints(5000));
    }

    auto container = hvt::HdPageableContainerDataSource::New(meshData,
        SdfPath("/Packed"), bufferManager.GetPageFileManager(),
        bufferManager.GetMemoryMonitor(), [](const SdfPath&) {});

    state.SetLabel(usePacked ? "Packed" : "PerElement");

    for (auto _ : state)
    {
        if (usePacked)
        {
            container->SwapSceneToDisk();
            container->SwapToSceneMemory();
        }
        else
        {
            // Per-element paging
            for (const auto& [token, value] : meshData)
            {
                container->PageOutElement(token);
            }
            for (const auto& [token, value] : meshData)
            {
                container->PageInElement(token);
            }
        }
        memStats.Update(bufferManager.GetMemoryMonitor());
    }

    SetMemoryCounters(state, memStats);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(elementCount));
}
BENCHMARK(BM_PackedVsPerElement)
    ->Args({ 1, 3 })   // Packed, 3 elements
    ->Args({ 0, 3 })   // Per-element, 3 elements
    ->Args({ 1, 10 })  // Packed, 10 elements
    ->Args({ 0, 10 })  // Per-element, 10 elements
    ->Args({ 1, 20 })  // Packed, 20 elements
    ->Args({ 0, 20 }); // Per-element, 20 elements

/// Benchmark: Mass page out 1M × 8 KB HdPageableValues
static void BM_MassPageOut(benchmark::State& state)
{
    const size_t kCount = static_cast<size_t>(state.range(0));
    const size_t kElemBytes = 8192; // 8 KB per value
    const size_t kFloatsPerElem = kElemBytes / sizeof(float);

    auto tempDir = std::filesystem::temp_directory_path() / "hvt_mass_pageout";
    std::filesystem::create_directories(tempDir);

    hvt::HdPageableDataSourceManager::Config cfg;
    cfg.pageFileDirectory = tempDir;
    cfg.sceneMemoryLimit = static_cast<size_t>(64) * hvt::ONE_GiB;
    cfg.enableBackgroundCleanup = false;
    auto manager = std::make_shared<hvt::HdPageableDataSourceManager>(cfg);

    // Pre-build the template VtValue once
    VtFloatArray templateArr(kFloatsPerElem);
    for (size_t j = 0; j < kFloatsPerElem; ++j)
        templateArr[j] = static_cast<float>(j);
    VtValue templateVal(templateArr);

    for (auto _ : state)
    {
        state.PauseTiming();
        // Create all values
        std::vector<std::shared_ptr<hvt::HdPageableValue>> values;
        values.reserve(kCount);
        for (size_t i = 0; i < kCount; ++i)
        {
            SdfPath path(TfStringPrintf("/mass_v%zu", i));
            values.push_back(std::make_shared<hvt::HdPageableValue>(
                path, kElemBytes, hvt::HdBufferUsage::Static,
                manager->GetPageFileManager(), manager->GetMemoryMonitor(),
                [](const SdfPath&) {}, // No-op destruction callback
                templateVal, TfToken("points")));
        }
        state.ResumeTiming();

        // Page out all values
        size_t paged = 0;
        for (auto& v : values)
        {
            if (v->SwapSceneToDisk(true))
                ++paged;
        }
        benchmark::DoNotOptimize(paged);

        state.PauseTiming();
        values.clear();
        state.ResumeTiming();
    }

    state.counters["values"] = benchmark::Counter(
        static_cast<double>(kCount), benchmark::Counter::kDefaults);
    state.counters["totalMiB"] = benchmark::Counter(
        static_cast<double>(kCount * kElemBytes) / (1024.0 * 1024.0));

    std::filesystem::remove_all(tempDir);
}
BENCHMARK(BM_MassPageOut)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);


// =============================================================================
// Memory Verification Benchmarks (guarded by ENABLE_MEMORY_TRACKER)
// =============================================================================
// These benchmarks specifically verify that the paging system reclaims memory
// by sampling actual heap usage before/after paging operations.
// =============================================================================

#ifdef ENABLE_MEMORY_TRACKER

/// Benchmark: verifies that SwapSceneToDisk actually releases heap memory.
///   Arg(0): number of buffers to allocate
///   Arg(1): per-buffer size in KiB
static void BM_PagingMemoryReclamation(benchmark::State& state)
{
    const size_t numBuffers    = static_cast<size_t>(state.range(0));
    const size_t bufferSizeKiB = static_cast<size_t>(state.range(1));
    const size_t bufferSize    = bufferSizeKiB * hvt::ONE_KiB;

    auto tempDir = std::filesystem::temp_directory_path() / "hvt_bench_memreclaim";
    std::filesystem::create_directories(tempDir);

    using ManagerType = hvt::DefaultBufferManager;
    ManagerType::InitializeDesc desc;
    desc.pageFileDirectory   = tempDir;
    desc.sceneMemoryLimit    = numBuffers * bufferSize * 4;
    desc.rendererMemoryLimit = numBuffers * bufferSize * 2;
    desc.numThreads          = 2;

    ManagerType manager(desc);
    auto* monitor = manager.GetMemoryMonitor().get();

    for (auto _ : state)
    {
        state.PauseTiming();

        std::vector<std::shared_ptr<hvt::HdPageableValue>> values;
        values.reserve(numBuffers);

        VtFloatArray payload(bufferSize / sizeof(float), 3.14f);
        VtValue val(payload);

        // Create buffers that will increase the heap usage.
        for (size_t i = 0; i < numBuffers; ++i)
        {
            auto path = SdfPath(TfStringPrintf("/MemRec_V%zu", i));
            auto pv   = std::make_shared<hvt::HdPageableValue>(
                path, bufferSize, hvt::HdBufferUsage::Static,
                manager.GetPageFileManager(), manager.GetMemoryMonitor(),
                [](const SdfPath&) {}, val, TfToken("float[]"));
            values.push_back(pv);
        }

        size_t heapBeforePaging = GetHeapAllocatedBytes();
        size_t sceneMemBefore   = monitor->GetUsedSceneMemory();

        state.ResumeTiming();

        // --- Phase 1: page all buffers to disk ---
        for (auto& pv : values)
        {
            pv->SwapSceneToDisk();
        }

        state.PauseTiming();

        size_t heapAfterPageOut = GetHeapAllocatedBytes();
        size_t sceneMemAfter   = monitor->GetUsedSceneMemory();

        int64_t heapDelta  = static_cast<int64_t>(heapAfterPageOut) -
                             static_cast<int64_t>(heapBeforePaging);
        int64_t sceneDelta = static_cast<int64_t>(sceneMemAfter) -
                             static_cast<int64_t>(sceneMemBefore);

        state.counters["HeapDeltaKiB"] = benchmark::Counter(
            static_cast<double>(heapDelta) / 1024.0);
        state.counters["SceneDeltaKiB"] = benchmark::Counter(
            static_cast<double>(sceneDelta) / 1024.0);
        state.counters["HeapBeforeMiB"] = benchmark::Counter(
            static_cast<double>(heapBeforePaging) / hvt::ONE_MiB);
        state.counters["HeapAfterMiB"] = benchmark::Counter(
            static_cast<double>(heapAfterPageOut) / hvt::ONE_MiB);
        state.counters["SceneBeforeKiB"] = benchmark::Counter(
            static_cast<double>(sceneMemBefore) / 1024.0);
        state.counters["SceneAfterKiB"] = benchmark::Counter(
            static_cast<double>(sceneMemAfter) / 1024.0);

        // --- Phase 2: page all buffers back in ---
        state.ResumeTiming();

        for (auto& pv : values)
        {
            pv->SwapToSceneMemory();
        }

        state.PauseTiming();

        size_t heapAfterPageIn = GetHeapAllocatedBytes();

        state.counters["HeapAfterPageInMiB"] = benchmark::Counter(
            static_cast<double>(heapAfterPageIn) / hvt::ONE_MiB);

        // Cleanup
        values.clear();

        state.ResumeTiming();
    }

    std::filesystem::remove_all(tempDir);
}
BENCHMARK(BM_PagingMemoryReclamation)
    ->Args({ 50, 64 })    // 50 x 64 KiB = ~3.2 MiB
    ->Args({ 100, 64 })   // 100 x 64 KiB = ~6.4 MiB
    ->Args({ 50, 256 })   // 50 x 256 KiB = ~12.8 MiB
    ->Args({ 100, 256 })  // 100 x 256 KiB = ~25.6 MiB
    ->Iterations(3)
    ->Unit(benchmark::kMillisecond);

/// Benchmark: measures heap impact of the paging lifecycle.
///   Allocate -> fill -> page out -> verify drop -> page in -> verify restore.
/// This helps detect memory leaks in the paging pipeline.
static void BM_PagingLifecycleHeap(benchmark::State& state)
{
    const size_t numBuffers = 80;
    const size_t elemSize   = 128 * hvt::ONE_KiB;

    auto tempDir = std::filesystem::temp_directory_path() / "hvt_bench_lifecycle";
    std::filesystem::create_directories(tempDir);

    using ManagerType = hvt::DefaultBufferManager;
    ManagerType::InitializeDesc desc;
    desc.pageFileDirectory   = tempDir;
    desc.sceneMemoryLimit    = numBuffers * elemSize * 4;
    desc.rendererMemoryLimit = numBuffers * elemSize * 2;
    desc.numThreads          = 2;

    ManagerType manager(desc);

    for (auto _ : state)
    {
        size_t heapBaseline = GetHeapAllocatedBytes();

        // Create buffers that will increase the heap usage.
        std::vector<std::shared_ptr<hvt::HdPageableValue>> values;
        values.reserve(numBuffers);
        VtFloatArray arr(elemSize / sizeof(float), 1.0f);
        VtValue val(arr);

        for (size_t i = 0; i < numBuffers; ++i)
        {
            auto path = SdfPath(TfStringPrintf("/Lifecycle_V%zu", i));
            values.push_back(std::make_shared<hvt::HdPageableValue>(
                path, elemSize, hvt::HdBufferUsage::Static,
                manager.GetPageFileManager(), manager.GetMemoryMonitor(),
                [](const SdfPath&) {}, val, TfToken("float[]")));
        }

        size_t heapAfterAlloc = GetHeapAllocatedBytes();

        // Page out all buffers to disk and verify the heap usage.
        for (auto& pv : values)
            pv->SwapSceneToDisk();

        size_t heapAfterPageOut = GetHeapAllocatedBytes();

        // Page in all buffers to scene memory and double check the heap usage.
        for (auto& pv : values)
            pv->SwapToSceneMemory();

        size_t heapAfterPageIn = GetHeapAllocatedBytes();

        // Clean up.
        values.clear();

        size_t heapAfterFree = GetHeapAllocatedBytes();

        auto signedDelta = [](size_t a, size_t b) -> double {
            return static_cast<double>(static_cast<int64_t>(a) - static_cast<int64_t>(b));
        };

        state.counters["AllocGrowthMiB"] = benchmark::Counter(
            signedDelta(heapAfterAlloc, heapBaseline) / hvt::ONE_MiB);
        state.counters["PageOutDropMiB"] = benchmark::Counter(
            signedDelta(heapAfterAlloc, heapAfterPageOut) / hvt::ONE_MiB);
        state.counters["PageInGrowthMiB"] = benchmark::Counter(
            signedDelta(heapAfterPageIn, heapAfterPageOut) / hvt::ONE_MiB);
        state.counters["LeakMiB"] = benchmark::Counter(
            signedDelta(heapAfterFree, heapBaseline) / hvt::ONE_MiB);
    }

    std::filesystem::remove_all(tempDir);
}
BENCHMARK(BM_PagingLifecycleHeap)
    ->Iterations(5)
    ->Unit(benchmark::kMillisecond);

#endif // ENABLE_MEMORY_TRACKER

// =============================================================================
// Main
// =============================================================================

#ifdef ENABLE_MEMORY_TRACKER

// Note: a default benchmark::main is provided when ENABLE_MEMORY_TRACKER is not defined
int main(int argc, char** argv)
{
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
    {
        return 1;
    }

    PagingMemoryManager memoryManager;
    benchmark::RegisterMemoryManager(&memoryManager);

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    benchmark::RegisterMemoryManager(nullptr);
    return 0;
}

#endif // ENABLE_MEMORY_TRACKER
