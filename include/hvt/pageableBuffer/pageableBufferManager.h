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
#pragma once

#include <hvt/api.h>
#include <hvt/pageableBuffer/pageFileManager.h>
#include <hvt/pageableBuffer/pageableBuffer.h>
#include <hvt/pageableBuffer/pageableConcepts.h>
#include <hvt/pageableBuffer/pageableMemoryMonitor.h>
#include <hvt/pageableBuffer/pageableStrategies.h>

#include <pxr/pxr.h>
#include <pxr/base/tf/callContext.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/sdf/path.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

#include <tbb/concurrent_unordered_map.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

namespace HVT_NS
{

class HdPageableBufferBase;

template <
#if defined(__cpp_concepts)
    HdPagingConcepts::PagingStrategyLike PagingStrategyType,
    HdPagingConcepts::BufferSelectionStrategyLike BufferSelectionStrategyType
#else
    typename PagingStrategyType,
    typename BufferSelectionStrategyType
#endif
    >
class HVT_API HdPageableBufferManager
{
public:
#if !defined(__cpp_concepts)
    using BufferSelectionStrategyIterator = tbb::concurrent_unordered_map<PXR_NS::SdfPath,
        std::shared_ptr<HdPageableBufferBase>, PXR_NS::SdfPath::Hash>::iterator;
    static_assert(HdPagingConcepts::PagingStrategyLikeValue<PagingStrategyType>,
        "PagingStrategyType does not meet the requirements of a paging strategy");
    static_assert(HdPagingConcepts::BufferSelectionStrategyLikeValue<BufferSelectionStrategyType,
                      BufferSelectionStrategyIterator>,
        "BufferSelectionStrategyType does not meet the requirements of a buffer selection "
        "strategy");
#endif

    struct InitializeDesc
    {
        std::filesystem::path pageFileDirectory =
            std::filesystem::temp_directory_path() / "hvt_temp_pages";
        int ageLimit               = 20; ///< frame count
        size_t sceneMemoryLimit    = static_cast<size_t>(2) * ONE_GiB;
        size_t rendererMemoryLimit = static_cast<size_t>(1) * ONE_GiB;
        size_t numThreads          = 0; ///< 0 means disable async operations
    };
    // Constructor and destructor are now public for direct instantiation
    HdPageableBufferManager(InitializeDesc desc) :
        mAgeLimit(desc.ageLimit),
        mPageFileManager(
            std::unique_ptr<HdPageFileManager>(new HdPageFileManager(desc.pageFileDirectory))),
        mMemoryMonitor(std::unique_ptr<HdMemoryMonitor>(
            new HdMemoryMonitor(desc.sceneMemoryLimit, desc.rendererMemoryLimit)))
    {
        if (desc.numThreads > 0)
        {
            mTaskArena = std::make_unique<tbb::task_arena>(desc.numThreads);
            mTaskArena->execute([this]() { mTaskGroup = std::make_unique<tbb::task_group>(); });
        }
    }

    ~HdPageableBufferManager()
    {
        // Wait for all pending tasks to complete before destruction
        WaitForAllOperations();

        // Ensure all resources are released before destructing the manager.
        if (mTaskGroup && mTaskArena)
        {
            mTaskArena->execute([this]() 
            {
                mTaskGroup.reset();
            });
        }
        mTaskArena.reset();
        mBuffers.clear();
    }

    // Frame stamp management
    void AdvanceFrame(uint advanceCount = 1) noexcept { mCurrentFrame += advanceCount; }
    [[nodiscard]] constexpr uint GetCurrentFrame() const noexcept { return mCurrentFrame; }

    // Strategy access (no runtime changing allowed)
    [[nodiscard]] constexpr PagingStrategyType GetPagingStrategy() const noexcept
    {
        return mPagingStrategy;
    }
    [[nodiscard]] constexpr BufferSelectionStrategyType GetBufferSelectionStrategy() const noexcept
    {
        return mBufferSelectionStrategy;
    }
    constexpr int GetAgeLimit() const noexcept { return mAgeLimit; }

    // Accessors to internal managers
    [[nodiscard]] std::unique_ptr<HdPageFileManager>& GetPageFileManager()
    {
        return mPageFileManager;
    }
    [[nodiscard]] std::unique_ptr<HdMemoryMonitor>& GetMemoryMonitor() { return mMemoryMonitor; }

    // Buffer operations //////////////////////////////////////////////////////

    // Buffer lifecycle management
    [[nodiscard]] std::shared_ptr<HdPageableBufferBase> CreateBuffer(
        const PXR_NS::SdfPath& path, size_t size = 0, HdBufferUsage usage = HdBufferUsage::Static);
    bool AddBuffer(const PXR_NS::SdfPath& path, std::shared_ptr<HdPageableBufferBase> buffer);
    void RemoveBuffer(const PXR_NS::SdfPath& path);
    [[nodiscard]] std::shared_ptr<HdPageableBufferBase> FindBuffer(const PXR_NS::SdfPath& path);

    // Paging trigger
    static constexpr size_t kMinimalCheckCount = 10;
    void FreeCrawl(float percentage = 10.0f);

    // Async buffer operations
    [[nodiscard]] std::future<bool> PageToSceneMemoryAsync(
        std::shared_ptr<HdPageableBufferBase> buffer, bool force = false);
    [[nodiscard]] std::future<bool> PageToRendererMemoryAsync(
        std::shared_ptr<HdPageableBufferBase> buffer, bool force = false);
    [[nodiscard]] std::future<bool> PageToDiskAsync(
        std::shared_ptr<HdPageableBufferBase> buffer, bool force = false);

    [[nodiscard]] std::future<bool> SwapSceneToDiskAsync(
        std::shared_ptr<HdPageableBufferBase> buffer, bool force = false);
    [[nodiscard]] std::future<bool> SwapRendererToDiskAsync(
        std::shared_ptr<HdPageableBufferBase> buffer, bool force = false);
    [[nodiscard]] std::future<bool> SwapToSceneMemoryAsync(
        std::shared_ptr<HdPageableBufferBase> buffer, bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::RendererBuffer) |
            static_cast<int>(HdBufferState::DiskBuffer)));
    [[nodiscard]] std::future<bool> SwapToRendererMemoryAsync(
        std::shared_ptr<HdPageableBufferBase> buffer, bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::SceneBuffer) |
            static_cast<int>(HdBufferState::DiskBuffer)));

    [[nodiscard]] std::future<void> ReleaseSceneBufferAsync(
        std::shared_ptr<HdPageableBufferBase> buffer) noexcept;
    [[nodiscard]] std::future<void> ReleaseRendererBufferAsync(
        std::shared_ptr<HdPageableBufferBase> buffer) noexcept;
    [[nodiscard]] std::future<void> ReleaseDiskPageAsync(
        std::shared_ptr<HdPageableBufferBase> buffer) noexcept;

    // Async paging trigger.
    std::vector<std::future<bool>> FreeCrawlAsync(float percentage = 10.0f);

    // Async operation status
    size_t GetPendingOperations() const;
    void WaitForAllOperations();

    // Statistics
    // NOTE: These APIs may severely slow down the system and should be used for development only.
    [[nodiscard]] size_t GetBufferCount() const;
    void PrintCacheStats() const;

private:
    // Disable copy and move
    HdPageableBufferManager(const HdPageableBufferManager&) = delete;
    HdPageableBufferManager(HdPageableBufferManager&&)      = delete;

    // HdPageableBufferBase destruction callback
    void OnBufferDestroyed(const PXR_NS::SdfPath& path);

    // Helper method: dispose old buffer using configurable strategy
    bool DisposeOldBuffer(HdPageableBufferBase& buffer, int currentFrame, int ageLimit,
        float scenePressure, float rendererPressure);

    // Execute paging decision on buffer (synchronous)
    bool ExecutePagingDecision(HdPageableBufferBase& buffer, const HdPagingDecision& decision);

    // Execute paging decision on buffer (asynchronous)
    std::future<bool> ExecutePagingDecisionAsync(
        std::shared_ptr<HdPageableBufferBase> buffer, const HdPagingDecision& decision);

    // Helper method for creating tasks with trackable future
    template <typename Callable>
    std::future<std::invoke_result_t<Callable>> SubmitTask(Callable&& task);

    tbb::concurrent_unordered_map<PXR_NS::SdfPath, std::shared_ptr<HdPageableBufferBase>,
        PXR_NS::SdfPath::Hash>
        mBuffers;

    std::atomic<uint> mCurrentFrame { 0 };
    const int mAgeLimit { 20 }; // TODO: move to strategies???

    // Compile-time strategy instances (no runtime changing)
    PagingStrategyType mPagingStrategy {};
    BufferSelectionStrategyType mBufferSelectionStrategy {};

    std::unique_ptr<HdPageFileManager> mPageFileManager;
    std::unique_ptr<HdMemoryMonitor> mMemoryMonitor;

    // Members for async buffer operations
    std::unique_ptr<tbb::task_arena> mTaskArena;
    std::unique_ptr<tbb::task_group> mTaskGroup;
    std::atomic<size_t> mPendingTaskCount { 0 };
};

// Template Methods Implementations ///////////////////////////////////////////

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::shared_ptr<HdPageableBufferBase> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::CreateBuffer(const PXR_NS::SdfPath& path, size_t size,
    HdBufferUsage usage)
{
    // Check if buffer with this path already exists
    if (const auto it = mBuffers.find(path); it != mBuffers.end())
    {
        using namespace PXR_NS;
        TF_WARN("HdPageableBufferBase '%s' already exists, returning existing buffer\n",
            path.GetText());
        return it->second;
    }

    // Create destruction callback that will remove buffer from the list.
    auto destructionCallback = [this](const PXR_NS::SdfPath& path)
    { this->OnBufferDestroyed(path); };

    // Create new buffer and insert into the managed list.
    auto buffer = std::shared_ptr<HdPageableBufferBase>(new HdPageableBufferBase(
        path, size, usage, this->mPageFileManager, this->mMemoryMonitor, destructionCallback));
    {
        mBuffers.emplace(path, buffer);
    }
    return buffer;
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
bool HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::AddBuffer(
    const PXR_NS::SdfPath& path, std::shared_ptr<HdPageableBufferBase> buffer)
{
    return mBuffers.emplace(path, buffer).second;
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
void HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::OnBufferDestroyed(
    const PXR_NS::SdfPath& path)
{
    this->RemoveBuffer(path);
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
void HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::RemoveBuffer(
    const PXR_NS::SdfPath& path)
{
    if (auto it = mBuffers.find(path); it != mBuffers.end())
    {
        mBuffers.unsafe_erase(it);
    }
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::shared_ptr<HdPageableBufferBase> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::FindBuffer(const PXR_NS::SdfPath& path)
{
    if (const auto it = mBuffers.find(path); it != mBuffers.end())
    {
        return it->second;
    }
    return nullptr;
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
bool HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::DisposeOldBuffer(
    HdPageableBufferBase& buffer, int currentFrame, int ageLimit, float scenePressure,
    float rendererPressure)
{
    // Create paging context
    HdPagingContext context;
    context.bufferAge        = currentFrame - buffer.FrameStamp();
    context.ageLimit         = ageLimit;
    context.scenePressure    = scenePressure;
    context.rendererPressure = rendererPressure;
    context.isOverAge        = buffer.IsOverAge(currentFrame, ageLimit);
    context.bufferUsage      = buffer.Usage();
    context.bufferState      = buffer.GetBufferState();

    // Use configured strategy
    HdPagingDecision decision = mPagingStrategy(buffer, context);
    return ExecutePagingDecision(buffer, decision);
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
bool HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::ExecutePagingDecision(HdPageableBufferBase& buffer,
    const HdPagingDecision& decision)
{
    if (!decision.shouldPage)
    {
        return false;
    }

    bool disposed = false;

    switch (decision.action)
    {
    case HdPagingDecision::Action::SwapSceneToDisk:
        disposed = buffer.SwapSceneToDisk(decision.forceOperation);
        break;

    case HdPagingDecision::Action::SwapRendererToDisk:
        disposed = buffer.SwapRendererToDisk(decision.forceOperation);
        break;

    case HdPagingDecision::Action::SwapToSceneMemory:
        disposed = buffer.SwapToSceneMemory(decision.forceOperation);
        break;

    case HdPagingDecision::Action::ReleaseRendererBuffer:
        buffer.ReleaseRendererBuffer();
        disposed = true;
        break;

    case HdPagingDecision::Action::None:
    default:
        break;
    }

    return disposed;
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<bool> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::ExecutePagingDecisionAsync(std::shared_ptr<HdPageableBufferBase>
                                                                 buffer,
    const HdPagingDecision& decision)
{
    if (!mTaskArena || !mTaskGroup)
    {
        // Return a invalid future
        return {};
    }
    if (!decision.shouldPage)
    {
        // Return a future that immediately resolves to false
        std::promise<bool> promise;
        promise.set_value(false);
        return promise.get_future();
    }

    switch (decision.action)
    {
    case HdPagingDecision::Action::SwapSceneToDisk:
        return SwapSceneToDiskAsync(buffer, decision.forceOperation);

    case HdPagingDecision::Action::SwapRendererToDisk:
        return SwapRendererToDiskAsync(buffer, decision.forceOperation);

    case HdPagingDecision::Action::SwapToSceneMemory:
        return SwapToSceneMemoryAsync(buffer, decision.forceOperation);

    case HdPagingDecision::Action::ReleaseRendererBuffer:
        return SubmitTask(
            [buffer]() -> bool
            {
                buffer->ReleaseRendererBuffer();
                return true;
            });

    case HdPagingDecision::Action::None:
    default:
        // Return a future that immediately resolves to false
        std::promise<bool> promise;
        promise.set_value(false);
        return promise.get_future();
    }
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
void HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::FreeCrawl(
    float percentage)
{
    float scenePressure    = mMemoryMonitor->GetSceneMemoryPressure();
    float rendererPressure = mMemoryMonitor->GetRendererMemoryPressure();

    // Only crawl if we're under memory pressure
    if (scenePressure < HdMemoryMonitor::LOW_MEMORY_THRESHOLD &&
        rendererPressure < HdMemoryMonitor::LOW_MEMORY_THRESHOLD)
    {
        return;
    }

    // Calculate number of non-null buffers to check
    auto numToCheck = static_cast<size_t>(mBuffers.size() * (percentage / 100.0f));
    numToCheck      = std::max(numToCheck, kMinimalCheckCount);
    numToCheck      = std::min(numToCheck, mBuffers.size());

    for (auto it = mBuffers.begin(); it != mBuffers.end();)
    {
        if (!it->second)
        {
            it = mBuffers.unsafe_erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Create selection context
    HdSelectionContext selectionContext;
    selectionContext.currentFrame   = mCurrentFrame;
    selectionContext.requestedCount = numToCheck;

    // Use configurable buffer selection strategy
    std::vector<std::shared_ptr<HdPageableBufferBase>> selectedBuffers =
        mBufferSelectionStrategy(mBuffers.begin(), mBuffers.end(), selectionContext);

    for (auto& buffer : selectedBuffers)
    {
        if (buffer)
        {
            buffer->UpdateFrameStamp(mCurrentFrame);
            DisposeOldBuffer(*buffer, mCurrentFrame, mAgeLimit, scenePressure, rendererPressure);
        }
    }
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::vector<std::future<bool>> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::FreeCrawlAsync(float percentage)
{
    if (!mTaskArena || !mTaskGroup)
    {
        return {};
    }

    std::vector<std::future<bool>> futures;

    float scenePressure    = mMemoryMonitor->GetSceneMemoryPressure();
    float rendererPressure = mMemoryMonitor->GetRendererMemoryPressure();

    // Only crawl if we're under memory pressure
    if (scenePressure < HdMemoryMonitor::LOW_MEMORY_THRESHOLD &&
        rendererPressure < HdMemoryMonitor::LOW_MEMORY_THRESHOLD)
    {
        return futures;
    }
    // Calculate number of buffers to check
    auto numToCheck = static_cast<size_t>(mBuffers.size() * (percentage / 100.0f));
    numToCheck      = std::max(numToCheck, kMinimalCheckCount);
    numToCheck      = std::min(numToCheck, mBuffers.size());

    // Remove null buffers first
    for (auto it = mBuffers.begin(); it != mBuffers.end();)
    {
        if (!it->second)
        {
            it = mBuffers.unsafe_erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Create selection context
    HdSelectionContext selectionContext;
    selectionContext.currentFrame   = mCurrentFrame;
    selectionContext.requestedCount = numToCheck;

    // Use configurable buffer selection strategy
    std::vector<std::shared_ptr<HdPageableBufferBase>> selectedBuffers =
        mBufferSelectionStrategy(mBuffers.begin(), mBuffers.end(), selectionContext);

    // Start async operations for each selected buffer
    for (auto& buffer : selectedBuffers)
    {
        if (!buffer)
        {
            continue;
        }

        buffer->UpdateFrameStamp(mCurrentFrame);

        // Check if buffer should be disposed based on age and pressure
        int age = mCurrentFrame - buffer->FrameStamp();
        if (age >= mAgeLimit)
        {
            // Create paging context and get decision
            HdPagingContext context;
            context.ageLimit         = mAgeLimit;
            context.scenePressure    = scenePressure;
            context.rendererPressure = rendererPressure;

            HdPagingDecision decision = mPagingStrategy(*buffer, context);

            // Start async operation
            if (decision.shouldPage)
            {
                futures.push_back(ExecutePagingDecisionAsync(buffer, decision));
            }
        }
    }
    return futures;
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
size_t HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::GetBufferCount()
    const
{
#ifdef _DEBUG
    const auto nonEmptyBuffer = std::count_if(
        mBuffers.begin(), mBuffers.end(), [](const auto& buffer) { return buffer != nullptr; });
    if (nonEmptyBuffer != mBuffers.size())
    {
        using namespace PXR_NS;
        TF_STATUS("HdPageableBufferManager::GetBufferCount find %zu empty buffers.\n",
            mBuffers.size() - nonEmptyBuffer);
    }
#endif
    return mBuffers.size();
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
void HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::PrintCacheStats()
    const
{
    using namespace PXR_NS;
    size_t sceneBuffers    = 0;
    size_t rendererBuffers = 0;
    size_t diskBuffers     = 0;
    for (auto const& it : mBuffers)
    {
        if (!it.second)
            continue;

        if (it.second->HasSceneBuffer())
            ++sceneBuffers;
        if (it.second->HasRendererBuffer())
            ++rendererBuffers;
        if (it.second->HasDiskBuffer())
            ++diskBuffers;
    }

    TF_STATUS(
        "\n=== Cache Statistics ===\n"
        "Total Buffers: %zu\n"
        "Scene Buffers: %zu\n"
        "Renderer Buffers: %zu\n"
        "Disk Buffers: %zu\n"
        "Current Frame: %u\n"
        "Age Limit: %d frames\n"
        "========================\n",
        mBuffers.size(), sceneBuffers, rendererBuffers, diskBuffers, mCurrentFrame.load(),
        mAgeLimit);
}

// Async Buffer Operations ////////////////////////////////////////////////////

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
size_t HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::GetPendingOperations() const
{
    return (!mTaskArena || !mTaskGroup) ? 0 : mPendingTaskCount.load();
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
void HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::WaitForAllOperations()
{
    if (!mTaskArena || !mTaskGroup)
    {
        return;
    }

    mTaskArena->execute([this]()
    {
        mTaskGroup->wait();
    });

    // Reset pending count after all tasks complete
    mPendingTaskCount.store(0);
}

// Helper method for submitting tasks with TBB task_group and future support ///
template <typename PagingStrategyType, typename BufferSelectionStrategyType>
template <typename Callable>
std::future<std::invoke_result_t<Callable>> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::SubmitTask(Callable&& task)
{
    using ResultType = std::invoke_result_t<Callable>;

    if (!mTaskArena || !mTaskGroup)
    {
        // Return an invalid future if async operations are not initialized
        return std::future<ResultType>();
    }

    // Create a packaged_task to get a future
    auto packagedTask =
        std::make_shared<std::packaged_task<ResultType()>>(std::forward<Callable>(task));
    auto future = packagedTask->get_future();

    // Submit task
    mPendingTaskCount.fetch_add(1);
    mTaskArena->execute([this, packagedTask]()
        {
            mTaskGroup->run(
                [this, packagedTask]()
                {
                    (*packagedTask)();
                    mPendingTaskCount.fetch_sub(1);
                });
        });

    return future;
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<bool> HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::
    PageToSceneMemoryAsync(std::shared_ptr<HdPageableBufferBase> buffer, bool force)
{
    return SubmitTask([buffer, force]() -> bool { return buffer->PageToSceneMemory(force); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<bool> HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::
    PageToRendererMemoryAsync(std::shared_ptr<HdPageableBufferBase> buffer, bool force)
{
    return SubmitTask([buffer, force]() -> bool { return buffer->PageToRendererMemory(force); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<bool> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::PageToDiskAsync(std::shared_ptr<HdPageableBufferBase> buffer,
    bool force)
{
    return SubmitTask([buffer, force]() -> bool { return buffer->PageToDisk(force); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<bool> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::SwapSceneToDiskAsync(std::shared_ptr<HdPageableBufferBase> buffer,
    bool force)
{
    return SubmitTask([buffer, force]() -> bool { return buffer->SwapSceneToDisk(force); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<bool> HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::
    SwapRendererToDiskAsync(std::shared_ptr<HdPageableBufferBase> buffer, bool force)
{
    return SubmitTask([buffer, force]() -> bool { return buffer->SwapRendererToDisk(force); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<bool> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::SwapToSceneMemoryAsync(std::shared_ptr<HdPageableBufferBase>
                                                             buffer,
    bool force, HdBufferState releaseBuffer)
{
    return SubmitTask([buffer, force, releaseBuffer]() -> bool
        { return buffer->SwapToSceneMemory(force, releaseBuffer); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<bool> HdPageableBufferManager<PagingStrategyType,
    BufferSelectionStrategyType>::SwapToRendererMemoryAsync(std::shared_ptr<HdPageableBufferBase>
                                                                buffer,
    bool force, HdBufferState releaseBuffer)
{
    return SubmitTask([buffer, force, releaseBuffer]() -> bool
        { return buffer->SwapToRendererMemory(force, releaseBuffer); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<void> HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::
    ReleaseSceneBufferAsync(std::shared_ptr<HdPageableBufferBase> buffer) noexcept
{
    return SubmitTask([buffer]() -> void { buffer->ReleaseSceneBuffer(); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<void> HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::
    ReleaseRendererBufferAsync(std::shared_ptr<HdPageableBufferBase> buffer) noexcept
{
    return SubmitTask([buffer]() -> void { buffer->ReleaseRendererBuffer(); });
}

template <typename PagingStrategyType, typename BufferSelectionStrategyType>
std::future<void> HdPageableBufferManager<PagingStrategyType, BufferSelectionStrategyType>::
    ReleaseDiskPageAsync(std::shared_ptr<HdPageableBufferBase> buffer) noexcept
{
    return SubmitTask([buffer]() -> void { buffer->ReleaseDiskPage(); });
}

// Built-in BufferManager Aliases /////////////////////////////////////////////

// Default HdPageableBufferManager (also the one offered in HdMemoryManager)
using DefaultBufferManager = HdPageableBufferManager<HdPagingStrategies::HybridStrategy,
    HdPagingStrategies::LRUSelectionStrategy>;

// Memory-focused combinations
using PressureBasedLargestBufferManager =
    HdPageableBufferManager<HdPagingStrategies::PressureBasedStrategy,
        HdPagingStrategies::LargestFirstSelectionStrategy>;
using PressureBasedLRUBufferManager =
    HdPageableBufferManager<HdPagingStrategies::PressureBasedStrategy,
        HdPagingStrategies::LRUSelectionStrategy>;

// Performance-focused combinations
using ConservativeFIFOBufferManager =
    HdPageableBufferManager<HdPagingStrategies::ConservativeStrategy,
        HdPagingStrategies::FIFOSelectionStrategy>;
using ConservativeOldestBufferManager =
    HdPageableBufferManager<HdPagingStrategies::ConservativeStrategy,
        HdPagingStrategies::OldestFirstSelectionStrategy>;

// Strategy-specific combinations
using AgeBasedBufferManager = HdPageableBufferManager<HdPagingStrategies::AgeBasedStrategy,
    HdPagingStrategies::OldestFirstSelectionStrategy>;
using FIFOBufferManager     = HdPageableBufferManager<HdPagingStrategies::HybridStrategy,
        HdPagingStrategies::FIFOSelectionStrategy>;

} // namespace HVT_NS
