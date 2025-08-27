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
#include <hvt/pageableBuffer/pageableBuffer.h>
#include <hvt/pageableBuffer/pageableBufferManager.h>
#include <hvt/pageableBuffer/pageableStrategies.h>

#include <pxr/base/vt/value.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/base/vt/value.h>
#include <pxr/base/tf/token.h>

namespace HVT_NS
{

/// Forward declarations
class HdMemoryManager;

/// Memory-aware VtValue.
// TODO: We should either forbid users from modifying the source VtArray once it is paged out.
//     or, remove the HdPageableValue design but make it more higher level and applicable to data source only.
class HVT_API HdPageableValue : public HdPageableBufferBase
{
public:
    HdPageableValue(const PXR_NS::SdfPath& path, 
                   size_t estimatedSize,
                   HdBufferUsage usage,
                   const std::unique_ptr<HdPageFileManager>& pageFileManager,
                   const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
                   DestructionCallback destructionCallback,
                   const PXR_NS::VtValue& data,
                   const PXR_NS::TfToken& dataType);

    /// Get the original VtValue data (triggers load if needed)
    PXR_NS::VtValue GetValue();
    
    /// Get data type for Hydra consumption
    PXR_NS::TfToken GetDataType() const { return mDataType; }
    
    /// Check if data is immediately available
    bool IsDataResident() const { return HdPageableBufferBase::HasSceneBuffer(); }

    /// HdPageableBufferBase methods //////////////////////////////////////////

    bool SwapSceneToDisk(bool force = false) override;
    bool SwapToSceneMemory(bool force = false, HdBufferState releaseBuffer = HdBufferState::DiskBuffer) override;

    [[nodiscard]] PXR_NS::TfSpan<const std::byte> GetSceneMemorySpan() const noexcept override;
    [[nodiscard]] PXR_NS::TfSpan<std::byte> GetSceneMemorySpan() noexcept override;

    /// Utilities
    static size_t EstimateMemoryUsage(const PXR_NS::VtValue& value) noexcept;
    std::vector<uint8_t> SerializeVtValue(const PXR_NS::VtValue& value) const noexcept;
    PXR_NS::VtValue DeserializeVtValue(const std::vector<uint8_t>& data) noexcept;

private:
    mutable PXR_NS::VtValue mSourceValue;
    PXR_NS::TfToken mDataType;
};

struct HVT_API HdContainerPageEntry {
    std::type_index type;
    size_t offset;
    size_t size;
};

/// Memory-managed container data source.
class HVT_API HdPageableContainerDataSource : public PXR_NS::HdContainerDataSource, public HdPageableBufferBase {
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableContainerDataSource);

    static Handle New(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static);

    virtual std::map<PXR_NS::TfToken, HdContainerPageEntry> GetMemoryBreakdown() const;

private:
    HdPageableContainerDataSource(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static);
    
    std::map<PXR_NS::TfToken, HdContainerPageEntry> mContainerPageEntries;
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableContainerDataSource);
    
/// Memory-managed vector data source.
class HVT_API HdPageableVectorDataSource : public PXR_NS::HdVectorDataSource, public HdPageableBufferBase {
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableVectorDataSource);
    
    static Handle New(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static);

    virtual std::vector<HdContainerPageEntry> GetMemoryBreakdown() const;

private:
    HdPageableVectorDataSource(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static);
    
    std::vector<HdContainerPageEntry> mElements;
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableVectorDataSource);

// TODO ????
/// Memory-managed sampled data source for time-sampled values.
class HVT_API HdPageableSampledDataSource : public PXR_NS::HdSampledDataSource, public HdPageableBufferBase {
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableSampledDataSource);
    
    /// Create with memory management
    static Handle New(const PXR_NS::VtValue& value, 
                     const PXR_NS::SdfPath& primPath,
                     const PXR_NS::TfToken& attributeName,
                     const std::unique_ptr<HdPageFileManager>& pageFileManager,
                     const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
                     DestructionCallback destructionCallback,
                     HdBufferUsage usage = HdBufferUsage::Static);
    
    /// Create time-sampled with memory management
    static Handle New(const std::map<HdSampledDataSource::Time, PXR_NS::VtValue>& samples,
                     const PXR_NS::SdfPath& primPath, 
                     const PXR_NS::TfToken& attributeName,
                     const std::unique_ptr<HdPageFileManager>& pageFileManager,
                     const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
                     DestructionCallback destructionCallback,
                     HdBufferUsage usage = HdBufferUsage::Static);

    /// HdSampledDataSource interface
    PXR_NS::VtValue GetValue(Time shutterOffset) override;
    bool GetContributingSampleTimesForInterval(Time startTime, Time endTime,
                                               std::vector<Time>* outSampleTimes) override;

private:
    HdPageableSampledDataSource(const PXR_NS::VtValue& value,
                             const PXR_NS::SdfPath& primPath,
                             const PXR_NS::TfToken& attributeName,
                             const std::unique_ptr<HdPageFileManager>& pageFileManager,
                             const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
                             DestructionCallback destructionCallback,
                             HdBufferUsage usage = HdBufferUsage::Static);
    
    HdPageableSampledDataSource(const std::map<Time, PXR_NS::VtValue>& samples,
                             const PXR_NS::SdfPath& primPath,
                             const PXR_NS::TfToken& attributeName,
                             const std::unique_ptr<HdPageFileManager>& pageFileManager,
                             const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
                             DestructionCallback destructionCallback,
                             HdBufferUsage usage = HdBufferUsage::Static);
    
    /// Memory-managed sample storage
    struct MemorySample {
        Time time;
        std::shared_ptr<HdPageableValue> buffer;
    };
    
    std::vector<MemorySample> mSamples;
    PXR_NS::SdfPath mPrimPath;
    PXR_NS::TfToken mAttributeName;
    
    /// Get buffer key for caching
    std::string GetBufferKey(Time time) const;
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableSampledDataSource);

/// Memory-managed block data source.
class HVT_API HdPageableBlockDataSource : public PXR_NS::HdBlockDataSource, public HdPageableBufferBase {
public:
    HD_DECLARE_DATASOURCE(HdPageableBlockDataSource);

    static Handle New(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static);

private:
    HdPageableBlockDataSource(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static)
        : HdBlockDataSource()
        , HdPageableBufferBase(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback) {}
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableBlockDataSource);

/// Default memory manager with a background cleanup thread for Hydra DataSources
class HVT_API HdMemoryManager {
public:
    HdMemoryManager(std::filesystem::path pageFileDirectory,
        size_t sceneMemoryLimit, size_t rendererMemoryLimit);
    ~HdMemoryManager();
    
    /// Create or retrieve cached buffer????
    std::shared_ptr<HdPageableBufferBase> GetOrCreateBuffer(const PXR_NS::SdfPath& primPath,
                                                      const PXR_NS::VtValue& data,
                                                      const PXR_NS::TfToken& dataType);
    
    /// Frame management for age-based eviction
    void AdvanceFrame(uint advanceCount = 1) { mBufferManager.AdvanceFrame(advanceCount); }
    constexpr uint GetCurrentFrame() const noexcept { return mBufferManager.GetCurrentFrame(); }
    
    /// Configuration
    constexpr int GetAgeLimit() const noexcept { return mBufferManager.GetAgeLimit(); }
    void SetFreeCrawlPercentage(float percentage) noexcept { mFreeCrawlPercentage = percentage; }
    constexpr float GetFreeCrawlPercentage() const noexcept { return mFreeCrawlPercentage; }
    void SetFreeCrawlInterval(int interval) noexcept { mFreeCrawlInterval = interval; }
    constexpr int GetFreeCrawlInterval() const noexcept { return mFreeCrawlInterval; }
    
    /// Access to internal managers for utility functions
    std::unique_ptr<HdPageFileManager>& GetPageFileManager() { return mBufferManager.GetPageFileManager(); }
    std::unique_ptr<HdMemoryMonitor>& GetMemoryMonitor() { return mBufferManager.GetMemoryMonitor(); }
    
    /// Statistics (development purpose only)
    constexpr size_t GetTotalBufferCount() const { return mBufferManager.GetBufferCount(); }
    void PrintMemoryStatistics() const { mBufferManager.PrintCacheStats(); }

private:
    DefaultBufferManager mBufferManager;
    std::atomic<bool> mBackgroundCleanupEnabled{true};
    std::atomic<float> mFreeCrawlPercentage{10.0f};
    std::atomic<int> mFreeCrawlInterval{100}; ///< milliseconds
    std::thread mCleanupThread;
    
    void BackgroundCleanupLoop();
};

/// Utility functions for creating memory-managed data sources
namespace HdPageableDataSourceUtils {
    /// Create memory-managed data source from VtValue
    HVT_API
    PXR_NS::HdDataSourceBaseHandle CreateFromValue(const PXR_NS::VtValue& value,
                                          const PXR_NS::SdfPath& primPath,
                                          const PXR_NS::TfToken& name,
                                          const std::shared_ptr<HdMemoryManager>& memoryManager);
    
    /// Create memory-managed container from map
    HVT_API
    PXR_NS::HdContainerDataSourceHandle CreateContainer(
        const std::map<PXR_NS::TfToken, PXR_NS::VtValue>& values,
        const PXR_NS::SdfPath& primPath,
        const std::shared_ptr<HdMemoryManager>& memoryManager);

    /// Create memory-managed vector from vector
    HVT_API
    PXR_NS::HdVectorDataSourceHandle CreateVector(
        const std::vector<PXR_NS::VtValue>& values,
        const PXR_NS::SdfPath& primPath,
        const std::shared_ptr<HdMemoryManager>& memoryManager);
    
    /// Create time-sampled data source
    HVT_API
    PXR_NS::HdSampledDataSourceHandle CreateTimeSampled(
        const std::map<PXR_NS::HdSampledDataSource::Time, PXR_NS::VtValue>& samples,
        const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& name,
        const std::shared_ptr<HdMemoryManager>& memoryManager);
    
    /// Create memory-managed block from VtValue
    HVT_API
    PXR_NS::HdBlockDataSourceHandle CreateBlock(
        const PXR_NS::VtValue& value,
        const PXR_NS::SdfPath& primPath,
        const std::shared_ptr<HdMemoryManager>& memoryManager);
}

} // namespace HVT_NS