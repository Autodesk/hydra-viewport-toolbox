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
#include <hvt/pageableBuffer/pageableBuffer.h>
#include <hvt/pageableBuffer/pageableBufferManager.h>
#include <hvt/pageableBuffer/pageableStrategies.h>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/retainedDataSource.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <typeindex>
#include <vector>

namespace HVT_NS
{

/// Debug-only atomic counter that compiles away to zero-cost in release builds.
#if !defined(NDEBUG)
using HvtDebugCounter = std::atomic<size_t>;
#else
struct HvtDebugCounter
{
    constexpr void operator++() noexcept {}
    constexpr void operator++(int) noexcept {}
    constexpr size_t load(
        std::memory_order = std::memory_order_seq_cst) const noexcept { return 0; }
};
#endif

// Paging status types for metrics/observability
enum class HVT_API HdPagingStatus
{
    Resident,  ///< Data is in memory and immediately available
    PagedOut,  ///< Data has been paged out to disk
    Loading,   ///< Data is currently being loaded from disk
    Saving,    ///< Data is currently being saved to disk
    Invalid    ///< Data is in an invalid state
};

// Custom serializer interface for extensibility
class HVT_API IHdValueSerializer
{
public:
    virtual ~IHdValueSerializer() = default;

    /// Check if this serializer can handle the given type
    virtual bool CanSerialize(const std::type_index& type) const = 0;

    /// Serialize a VtValue to bytes
    virtual std::vector<uint8_t> Serialize(const PXR_NS::VtValue& value) const = 0;

    /// Deserialize bytes to a VtValue
    virtual PXR_NS::VtValue Deserialize(
        const std::vector<uint8_t>& data, const PXR_NS::TfToken& typeHint) const = 0;

    /// Estimate memory usage for a value
    virtual size_t EstimateSize(const PXR_NS::VtValue& value) const = 0;
};

// Default serializer supporting common USD types
class HVT_API HdDefaultValueSerializer : public IHdValueSerializer
{
public:
    bool CanSerialize(const std::type_index& type) const override;
    std::vector<uint8_t> Serialize(const PXR_NS::VtValue& value) const override;
    PXR_NS::VtValue Deserialize(
        const std::vector<uint8_t>& data, const PXR_NS::TfToken& typeHint) const override;

    /// Zero-copy deserialization from a raw data pointer.
    PXR_NS::VtValue DeserializeFromSpan(
        const uint8_t* data, size_t size, const PXR_NS::TfToken& typeHint) const;
    size_t EstimateSize(const PXR_NS::VtValue& value) const override;
};

/// Pageable data source memory manager with a background cleanup thread.
/// Supports metrics-based observability and custom serializers.
class HVT_API HdPageableDataSourceManager
{
public:
    /// Configuration options for the manager
    struct Config
    {
        std::filesystem::path pageFileDirectory =
            std::filesystem::temp_directory_path() / "hvt_temp_pages";
        size_t sceneMemoryLimit         = static_cast<size_t>(2) * ONE_GiB;
        size_t rendererMemoryLimit      = static_cast<size_t>(1) * ONE_GiB;
        float freeCrawlPercentage       = 10.0f;
        int freeCrawlIntervalMs         = 100;
        bool enableBackgroundCleanup    = true;
        int ageLimit                    = 20;
        unsigned int numThreads         = 2;
    };

    HdPageableDataSourceManager();
    HdPageableDataSourceManager(const Config& config);
    HdPageableDataSourceManager(std::filesystem::path pageFileDirectory, size_t sceneMemoryLimit,
        size_t rendererMemoryLimit);
    ~HdPageableDataSourceManager();

    /// Create or retrieve cached buffer. Thread-safe.
    std::shared_ptr<HdPageableBufferCore> GetOrCreateBuffer(const PXR_NS::SdfPath& primPath,
        const PXR_NS::VtValue& data, const PXR_NS::TfToken& dataType);

    /// Frame management for age-based eviction
    void AdvanceFrame(unsigned int advanceCount = 1) { mBufferManager->AdvanceFrame(advanceCount); }
    unsigned int GetCurrentFrame() const noexcept { return mBufferManager->GetCurrentFrame(); }

    /// Configuration
    int GetAgeLimit() const noexcept { return mBufferManager->GetAgeLimit(); }
    void SetFreeCrawlPercentage(float percentage) noexcept { mFreeCrawlPercentage = percentage; }
    float GetFreeCrawlPercentage() const noexcept { return mFreeCrawlPercentage; }
    void SetFreeCrawlInterval(int interval) noexcept { mFreeCrawlInterval = interval; }
    int GetFreeCrawlInterval() const noexcept { return mFreeCrawlInterval; }
    void SetBackgroundCleanupEnabled(bool enabled) noexcept { mBackgroundCleanupEnabled = enabled; }
    bool IsBackgroundCleanupEnabled() const noexcept { return mBackgroundCleanupEnabled; }

    /// Access to internal managers for utility functions
    std::unique_ptr<HdPageFileManager>& GetPageFileManager()
    {
        return mBufferManager->GetPageFileManager();
    }
    std::unique_ptr<HdMemoryMonitor>& GetMemoryMonitor()
    {
        return mBufferManager->GetMemoryMonitor();
    }

    /// Customization: Set custom serializer (replaces default)
    void SetSerializer(std::shared_ptr<IHdValueSerializer> serializer);
    const std::shared_ptr<IHdValueSerializer>& GetSerializer() const { return mSerializer; }

    /// Metrics and status query methods
    size_t GetTotalBufferCount() const { return mBufferManager->GetBufferCount(); }
    size_t GetResidentBufferCount() const;
    size_t GetPagedOutBufferCount() const;
    size_t GetTotalMemoryUsage() const;
    float GetMemoryPressure() const;
    
    /// Statistics (development purpose only)
    void PrintMemoryStatistics() const { mBufferManager->PrintCacheStats(); }

private:
    std::unique_ptr<DefaultBufferManager> mBufferManager;
    std::atomic<bool> mBackgroundCleanupEnabled { true };
    std::atomic<float> mFreeCrawlPercentage { 10.0f };
    std::atomic<int> mFreeCrawlInterval { 100 }; ///< milliseconds
    std::thread mCleanupThread;

    // Customization
    std::shared_ptr<IHdValueSerializer> mSerializer;

    void BackgroundCleanupLoop();
    void InitializeDefaults();
};

/// Utility functions for creating memory-managed data sources
namespace HdPageableDataSourceUtils
{
/// Create memory-managed data source from VtValue
HVT_API
PXR_NS::HdDataSourceBaseHandle CreateFromValue(const PXR_NS::VtValue& value,
    const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& name,
    const std::shared_ptr<HdPageableDataSourceManager>& memoryManager);

/// Create memory-managed container from map
HVT_API
PXR_NS::HdContainerDataSourceHandle CreateContainer(
    const std::map<PXR_NS::TfToken, PXR_NS::VtValue>& values, const PXR_NS::SdfPath& primPath,
    const std::shared_ptr<HdPageableDataSourceManager>& memoryManager);

/// Create memory-managed vector from vector
HVT_API
PXR_NS::HdVectorDataSourceHandle CreateVector(const std::vector<PXR_NS::VtValue>& values,
    const PXR_NS::SdfPath& primPath, const std::shared_ptr<HdPageableDataSourceManager>& memoryManager);

/// Create time-sampled data source
HVT_API
PXR_NS::HdSampledDataSourceHandle CreateTimeSampled(
    const std::map<PXR_NS::HdSampledDataSource::Time, PXR_NS::VtValue>& samples,
    const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& name,
    const std::shared_ptr<HdPageableDataSourceManager>& memoryManager);

/// Create memory-managed block from VtValue
HVT_API
PXR_NS::HdBlockDataSourceHandle CreateBlock(const PXR_NS::VtValue& value,
    const PXR_NS::SdfPath& primPath, const std::shared_ptr<HdPageableDataSourceManager>& memoryManager);
} // namespace HdPageableDataSourceUtils

/// Memory-aware VtValue with thread-safe access and metrics.
/// The data is copy-on-read when paged out, ensuring consistency.
/// Once data is paged out, accessing via GetValue() triggers automatic page-in.
class HVT_API HdPageableValue : public HdPageableBufferBase<>
{
public:
    HdPageableValue(const PXR_NS::SdfPath& path, size_t estimatedSize, HdBufferUsage usage,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback, const PXR_NS::VtValue& data,
        const PXR_NS::TfToken& dataType,
        bool enableImplicitPaging = true,
        const IHdValueSerializer* serializer = nullptr);

    /// Get the original VtValue data (triggers implicit page-in if needed).
    /// Thread-safe. Returns empty VtValue if page-in fails.
    /// @param outPagedIn Optional output to indicate if data was paged in
    PXR_NS::VtValue GetValue(bool* outPagedIn = nullptr);

    /// Get the value without triggering implicit paging.
    /// Returns empty VtValue if data is not resident.
    PXR_NS::VtValue GetValueIfResident() const;

    /// Check if implicit paging will occur on next GetValue()
    bool WillPageOnAccess() const;

    /// Get data type for Hydra consumption
    PXR_NS::TfToken GetDataType() const { return mDataType; }

    /// Check if data is immediately available (thread-safe)
    bool IsDataResident() const;

    bool IsImplicitPagingEnabled() const { return mEnableImplicitPaging; }

    /// Metrics and status
    HdPagingStatus GetStatus() const;
    size_t GetAccessCount() const { return mAccessCount.load(); }
    size_t GetPageInCount() const { return mPageInCount.load(); }
    size_t GetPageOutCount() const { return mPageOutCount.load(); }

    /// Direct value manipulation for packed container/vector paging.
    /// These bypass individual disk I/O -- the owning container manages disk.
    void SetResidentValue(const PXR_NS::VtValue& value);
    void ClearResidentValue();

    /// HdPageableBufferBase<> methods /////////////////////////////////////////

    bool SwapSceneToDisk(bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::SceneBuffer) |
            static_cast<int>(HdBufferState::RendererBuffer))) override;
    bool SwapToSceneMemory(
        bool force = false, HdBufferState releaseBuffer = HdBufferState::DiskBuffer) override;

    [[nodiscard]] PXR_NS::TfSpan<const std::byte> GetSceneMemorySpan() const noexcept override;
    [[nodiscard]] PXR_NS::TfSpan<std::byte> GetSceneMemorySpan() noexcept override;

    /// Utilities (use custom serializer if set)
    static size_t EstimateMemoryUsage(const PXR_NS::VtValue& value) noexcept;
    size_t EstimateMemoryUsage() const noexcept;
    std::vector<uint8_t> SerializeVtValue(const PXR_NS::VtValue& value) const noexcept;
    PXR_NS::VtValue DeserializeVtValue(const std::vector<uint8_t>& data) noexcept;

private:
    mutable std::shared_mutex mDataMutex; ///< Protects mSourceValue and mSerializedCache
    PXR_NS::VtValue mSourceValue;
    mutable std::vector<uint8_t> mSerializedCache; ///< Thread-safe cached serialization
    PXR_NS::TfToken mDataType;

    const IHdValueSerializer* mSerializer { nullptr }; // nullptr means use default serializer
    const bool mEnableImplicitPaging { true };

    // Metrics counters
    mutable HvtDebugCounter mAccessCount {};
    mutable HvtDebugCounter mPageInCount {};
    mutable HvtDebugCounter mPageOutCount {};
    mutable std::atomic<HdPagingStatus> mCurrentStatus { HdPagingStatus::Resident };

    // Internal helpers
    void UpdateSerializedCache() const;
};

struct HVT_API HdContainerPageEntry
{
    std::type_index type { typeid(void) };
    PXR_NS::TfToken typeHint; ///< Type hint token for serialization/deserialization
    size_t offset = 0; ///< Offset in the disk buffer
    size_t size   = 0; ///< Size of the element in the disk buffer
};

/// Memory-managed container data source with packed disk paging and implicit paging support.
/// All elements are packed into a single disk buffer for efficient I/O.
/// Automatically pages in data when Get() is called on paged-out elements.
class HVT_API HdPageableContainerDataSource : public PXR_NS::HdContainerDataSource,
                                              public HdPageableBufferBase<>
{
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableContainerDataSource);

    /// Create from a map of token to VtValue pairs
    static Handle New(const std::map<PXR_NS::TfToken, PXR_NS::VtValue>& values,
        const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Create empty container
    static Handle New(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// HdContainerDataSource interface. These may trigger implicit paging.
    PXR_NS::TfTokenVector GetNames() override;
    PXR_NS::HdDataSourceBaseHandle Get(const PXR_NS::TfToken& name) override;

    /// Memory breakdown for monitoring
    std::map<PXR_NS::TfToken, HdContainerPageEntry> GetMemoryBreakdown() const;

    /// Check if a specific element is resident
    bool IsElementResident(const PXR_NS::TfToken& name) const;

    /// Explicitly page in/out specific elements
    bool PageInElement(const PXR_NS::TfToken& name);
    bool PageOutElement(const PXR_NS::TfToken& name);

    /// Packed paging overrides
    bool SwapSceneToDisk(bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::SceneBuffer) |
            static_cast<int>(HdBufferState::RendererBuffer))) override;
    bool SwapToSceneMemory(
        bool force = false, HdBufferState releaseBuffer = HdBufferState::DiskBuffer) override;

    bool IsImplicitPagingEnabled() const { return mEnableImplicitPaging; }

    /// Observability metrics
    size_t GetAccessCount() const { return mAccessCount.load(); }
    size_t GetPageInCount() const { return mPageInCount.load(); }
    size_t GetPageOutCount() const { return mPageOutCount.load(); }

private:
    HdPageableContainerDataSource(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage,
        bool enableImplicitPaging);

    mutable std::shared_mutex mElementsMutex;
    std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>> mElements;
    std::map<PXR_NS::TfToken, HdContainerPageEntry> mContainerPageEntries;

    std::shared_ptr<IHdValueSerializer> mSerializer;

    const bool mEnableImplicitPaging { true };
    mutable HvtDebugCounter mAccessCount {};
    mutable HvtDebugCounter mPageInCount {};
    mutable HvtDebugCounter mPageOutCount {};
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableContainerDataSource);

/// Memory-managed vector data source with packed disk paging and implicit paging support.
/// All elements are packed into a single disk buffer for efficient I/O.
/// Automatically pages in data when GetElement() is called on paged-out elements.
class HVT_API HdPageableVectorDataSource : public PXR_NS::HdVectorDataSource,
                                           public HdPageableBufferBase<>
{
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableVectorDataSource);

    /// Create from a vector of VtValues
    static Handle New(const std::vector<PXR_NS::VtValue>& values, const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Create empty vector
    static Handle New(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// HdVectorDataSource interface. These may trigger implicit paging.
    size_t GetNumElements() override;
    PXR_NS::HdDataSourceBaseHandle GetElement(size_t element) override;

    /// Memory breakdown for monitoring
    std::vector<HdContainerPageEntry> GetMemoryBreakdown() const;

    /// Check if a specific element is resident
    bool IsElementResident(size_t index) const;

    /// Explicitly page in/out specific elements
    bool PageInElement(size_t index);
    bool PageOutElement(size_t index);

    /// Packed paging overrides
    bool SwapSceneToDisk(bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::SceneBuffer) |
            static_cast<int>(HdBufferState::RendererBuffer))) override;
    bool SwapToSceneMemory(
        bool force = false, HdBufferState releaseBuffer = HdBufferState::DiskBuffer) override;

    bool IsImplicitPagingEnabled() const { return mEnableImplicitPaging; }

    /// Observability metrics
    size_t GetAccessCount() const { return mAccessCount.load(); }
    size_t GetPageInCount() const { return mPageInCount.load(); }
    size_t GetPageOutCount() const { return mPageOutCount.load(); }

private:
    HdPageableVectorDataSource(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage,
        bool enableImplicitPaging);

    mutable std::shared_mutex mElementsMutex;
    std::vector<std::shared_ptr<HdPageableValue>> mElements;
    std::vector<HdContainerPageEntry> mElementPageEntries;

    std::shared_ptr<IHdValueSerializer> mSerializer;

    const bool mEnableImplicitPaging { true };
    mutable HvtDebugCounter mAccessCount {};
    mutable HvtDebugCounter mPageInCount {};
    mutable HvtDebugCounter mPageOutCount {};
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableVectorDataSource);

/// Memory-managed sampled data source for time-sampled values.
/// Supports implicit paging with thread-safe access.
/// Provides optional interpolation between time samples.
class HVT_API HdPageableSampledDataSource : public PXR_NS::HdSampledDataSource,
                                            public HdPageableBufferBase<>
{
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableSampledDataSource);

    /// Interpolation mode for time samples
    enum class InterpolationMode
    {
        None,   ///< Return nearest sample without interpolation
        Linear, ///< Linear interpolation between samples (for supported types)
        Held    ///< Return previous sample value (step function)
    };

    /// Create with memory management (single constant value)
    static Handle New(const PXR_NS::VtValue& value, const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Create time-sampled with memory management
    static Handle New(const std::map<HdSampledDataSource::Time, PXR_NS::VtValue>& samples,
        const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Interpolate between two values (for supported types)
    static PXR_NS::VtValue InterpolateValues(
        const PXR_NS::VtValue& v1, const PXR_NS::VtValue& v2, float t);

    /// HdSampledDataSource interface. These may trigger implicit paging.
    PXR_NS::VtValue GetValue(Time shutterOffset) override;
    bool GetContributingSampleTimesForInterval(
        Time startTime, Time endTime, std::vector<Time>* outSampleTimes) override;

    /// Get value without triggering implicit paging
    PXR_NS::VtValue GetValueIfResident(Time shutterOffset) const;

    /// Configuration
    void SetInterpolationMode(InterpolationMode mode) { mInterpolationMode = mode; }
    InterpolationMode GetInterpolationMode() const { return mInterpolationMode; }

    /// Check if a specific time sample is resident
    bool IsSampleResident(Time time) const;

    /// Get all available sample times. It may trigger implicit paging.
    std::vector<Time> GetAllSampleTimes() const;

    bool IsImplicitPagingEnabled() const { return mEnableImplicitPaging; }

    /// Observability metrics
    size_t GetAccessCount() const { return mAccessCount.load(); }
    size_t GetPageInCount() const { return mPageInCount.load(); }
    size_t GetPageOutCount() const { return mPageOutCount.load(); }

private:
    HdPageableSampledDataSource(const PXR_NS::VtValue& value, const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage, bool enableImplicitPaging);

    HdPageableSampledDataSource(const std::map<Time, PXR_NS::VtValue>& samples,
        const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage, bool enableImplicitPaging);

    /// Memory-managed sample storage
    struct MemorySample
    {
        Time time;
        std::shared_ptr<HdPageableValue> buffer;
    };

    mutable std::shared_mutex mSamplesMutex;
    std::vector<MemorySample> mSamples;
    PXR_NS::SdfPath mPrimPath;
    PXR_NS::TfToken mAttributeName;
    InterpolationMode mInterpolationMode { InterpolationMode::None };

    const bool mEnableImplicitPaging { true };
    mutable HvtDebugCounter mAccessCount {};
    mutable HvtDebugCounter mPageInCount {};
    mutable HvtDebugCounter mPageOutCount {};

    /// Get buffer key for caching
    std::string GetBufferKey(Time time) const;

    /// Helper method: find sample for given time
    const MemorySample* FindSample(Time time) const;

    /// Helper method: get value with or without implicit paging based on flag
    PXR_NS::VtValue GetSampleValue(const MemorySample& sample) const;
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableSampledDataSource);

/// Memory-managed data source for concretely-typed sampled values.
/// Provides type-safe access with implicit paging support.
template <typename T>
class HVT_API HdPageableTypedSampledDataSource : public PXR_NS::HdTypedSampledDataSource<T>,
                                                 public HdPageableBufferBase<>
{
public:
    using Handle = std::shared_ptr<HdPageableTypedSampledDataSource<T>>;
    static Handle Cast(const PXR_NS::HdDataSourceBase::Handle& v)
    {
        return std::dynamic_pointer_cast<HdPageableTypedSampledDataSource<T>>(v);
    }
    using Type = T;
    using Time = typename PXR_NS::HdTypedSampledDataSource<T>::Time;

    /// Create typed sampled data source
    static typename HdPageableTypedSampledDataSource<T>::Handle New(const T& value,
        const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Create time-sampled typed data source
    static typename HdPageableTypedSampledDataSource<T>::Handle New(
        const std::map<Time, T>& samples, const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Returns the value of this data source at frame-relative time
    /// \p shutterOffset, as type \p T. Triggers implicit paging.
    T GetTypedValue(Time shutterOffset) override;

    /// HdTypedSampledDataSource interface. These may trigger implicit paging.
    PXR_NS::VtValue GetValue(Time shutterOffset) override;
    bool GetContributingSampleTimesForInterval(
        Time startTime, Time endTime, std::vector<Time>* outSampleTimes) override;

    /// Get typed value without triggering implicit paging
    T GetTypedValueIfResident(Time shutterOffset) const;

    /// Check if data is resident
    bool IsResident() const;

    bool IsImplicitPagingEnabled() const { return mEnableImplicitPaging; }

private:
    HdPageableTypedSampledDataSource(const T& value, const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage, bool enableImplicitPaging);

    HdPageableTypedSampledDataSource(const std::map<Time, T>& samples,
        const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage, bool enableImplicitPaging);

    struct TypedSample
    {
        Time time;
        std::shared_ptr<HdPageableValue> buffer;
    };

    const bool mEnableImplicitPaging { true };
    mutable std::shared_mutex mSamplesMutex;
    std::vector<TypedSample> mSamples;
    PXR_NS::SdfPath mPrimPath;
    PXR_NS::TfToken mAttributeName;
};

/// Memory-managed block data source for representing absent/blocked data.
/// Block data sources indicate that data is intentionally absent.
class HVT_API HdPageableBlockDataSource : public PXR_NS::HdBlockDataSource,
                                          public HdPageableBufferBase<>
{
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableBlockDataSource)

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
        HdBufferUsage usage) :
        HdBlockDataSource(),
        HdPageableBufferBase<>(primPath, 0, usage, pageFileManager, memoryMonitor,
            std::move(destructionCallback))
    {
    }
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableBlockDataSource);

////////////////////////////////////////////////////////////////////////////////
// HdPageableDataSourceUtils — shared implementation helpers for Container,
// Vector, and Sampled data sources (non-retained and retained variants).
////////////////////////////////////////////////////////////////////////////////
namespace HdPageableDataSourceUtils
{

HVT_API extern const HdPageableBufferBase<>::DestructionCallback kNoOpDestructionCallback;

// Container packed serialization
HVT_API std::vector<uint8_t> SerializeContainerPacked(
    const std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>>& elements,
    const IHdValueSerializer& serializer);
HVT_API bool DeserializeContainerPacked(
    const std::vector<uint8_t>& packedData,
    std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::map<PXR_NS::TfToken, HdContainerPageEntry>& pageEntries,
    const IHdValueSerializer& serializer);

// Vector packed serialization
HVT_API std::vector<uint8_t> SerializeVectorPacked(
    const std::vector<std::shared_ptr<HdPageableValue>>& elements,
    const IHdValueSerializer& serializer);
HVT_API bool DeserializeVectorPacked(
    const std::vector<uint8_t>& packedData,
    std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::vector<HdContainerPageEntry>& pageEntries,
    const IHdValueSerializer& serializer);

// Container paging operations
HVT_API PXR_NS::HdDataSourceBaseHandle ContainerGet(const PXR_NS::TfToken& name,
    std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::map<PXR_NS::TfToken, HdContainerPageEntry>& pageEntries,
    std::shared_mutex& mutex, bool enableImplicitPaging, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, const IHdValueSerializer& serializer,
    HvtDebugCounter& accessCount, HvtDebugCounter& pageInCount);
HVT_API bool ContainerPageIn(const PXR_NS::TfToken& name,
    std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::map<PXR_NS::TfToken, HdContainerPageEntry>& pageEntries,
    std::shared_mutex& mutex, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageInCount);
HVT_API bool ContainerPageOut(const PXR_NS::TfToken& name,
    std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::shared_mutex& mutex, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager,
    HdBufferState& bufferState, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageOutCount);
HVT_API bool ContainerSwapToDisk(
    std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>>& elements, bool force,
    std::shared_mutex& mutex, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager,
    HdBufferState& bufferState, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageOutCount);
HVT_API bool ContainerSwapToMemory(
    std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::map<PXR_NS::TfToken, HdContainerPageEntry>& pageEntries,
    std::shared_mutex& mutex, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager,
    HdBufferState& bufferState, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageInCount);

// Vector paging operations
HVT_API PXR_NS::HdDataSourceBaseHandle VectorGetElement(size_t element,
    std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::vector<HdContainerPageEntry>& pageEntries,
    std::shared_mutex& mutex, bool enableImplicitPaging, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, const IHdValueSerializer& serializer,
    HvtDebugCounter& accessCount, HvtDebugCounter& pageInCount);
HVT_API bool VectorPageIn(size_t index,
    std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::vector<HdContainerPageEntry>& pageEntries,
    std::shared_mutex& mutex, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageInCount);
HVT_API bool VectorPageOut(size_t index,
    std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::shared_mutex& mutex, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager,
    HdBufferState& bufferState, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageOutCount);
HVT_API bool VectorSwapToDisk(
    std::vector<std::shared_ptr<HdPageableValue>>& elements, bool force,
    std::shared_mutex& mutex, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager,
    HdBufferState& bufferState, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageOutCount);
HVT_API bool VectorSwapToMemory(
    std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::vector<HdContainerPageEntry>& pageEntries,
    std::shared_mutex& mutex, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager,
    HdBufferState& bufferState, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageInCount);

// Sampled helpers
HVT_API std::string SampledGetBufferKey(PXR_NS::HdSampledDataSource::Time time,
    const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName);

template<typename MemorySample>
PXR_NS::VtValue GetSampleValue(
    const MemorySample& sample, bool enableImplicitPaging, HvtDebugCounter& pageInCount)
{
    if (enableImplicitPaging)
    {
        bool pagedIn = false;
        PXR_NS::VtValue result = sample.buffer->GetValue(&pagedIn);
        if (pagedIn)
        {
            ++pageInCount;
        }
        return result;
    }
    return sample.buffer->GetValueIfResident();
}

template<typename SampleVec>
PXR_NS::VtValue SampledGetValue(
    PXR_NS::HdSampledDataSource::Time shutterOffset,
    SampleVec& samples, std::shared_mutex& samplesMutex,
    bool enableImplicitPaging,
    HdPageableSampledDataSource::InterpolationMode interpolationMode,
    HvtDebugCounter& accessCount, HvtDebugCounter& pageInCount)
{
    ++accessCount;
    std::shared_lock<std::shared_mutex> readLock(samplesMutex);
    if (samples.empty())
        return PXR_NS::VtValue();
    if (samples.size() == 1)
        return GetSampleValue(samples[0], enableImplicitPaging, pageInCount);

    // Find the sample closest to the shutter offset
    using Time = PXR_NS::HdSampledDataSource::Time;
    auto it = std::lower_bound(samples.begin(), samples.end(), shutterOffset,
        [](const auto& s, Time t) { return s.time < t; });
    if (it == samples.end())
        return GetSampleValue(samples.back(), enableImplicitPaging, pageInCount);
    if (it == samples.begin())
        return GetSampleValue(samples.front(), enableImplicitPaging, pageInCount);

    // Interpolate between the two closest samples
    switch (interpolationMode)
    {
    case HdPageableSampledDataSource::InterpolationMode::Held:
        return GetSampleValue(*std::prev(it), enableImplicitPaging, pageInCount);
    case HdPageableSampledDataSource::InterpolationMode::Linear:
    {
        auto prevIt = std::prev(it);
        PXR_NS::VtValue v1 = GetSampleValue(*prevIt, enableImplicitPaging, pageInCount);
        PXR_NS::VtValue v2 = GetSampleValue(*it, enableImplicitPaging, pageInCount);
        float t = static_cast<float>(
            (shutterOffset - prevIt->time) / (it->time - prevIt->time));
        return HdPageableSampledDataSource::InterpolateValues(v1, v2, t);
    }
    default:
        return GetSampleValue(*it, enableImplicitPaging, pageInCount);
    }
}

template<typename SampleVec>
const typename SampleVec::value_type* SampledFindSample(
    PXR_NS::HdSampledDataSource::Time time, const SampleVec& samples)
{
    using Time = PXR_NS::HdSampledDataSource::Time;
    auto it = std::lower_bound(samples.begin(), samples.end(), time,
        [](const auto& s, Time t) { return s.time < t; });

    // Check if the sample is the closest to the shutter offset
    if (it != samples.end() && std::abs(it->time - time) < 1e-6)
        return &(*it);
    // Otherwise, return the previous sample if it is closer to the shutter offset
    if (it != samples.begin())
    {
        auto prevIt = std::prev(it);
        if (it == samples.end() ||
            std::abs(prevIt->time - time) < std::abs(it->time - time))
            return &(*prevIt);
    }
    return it != samples.end() ? &(*it) : nullptr;
}

template<typename SampleVec>
bool SampledGetContributingTimes(
    PXR_NS::HdSampledDataSource::Time startTime,
    PXR_NS::HdSampledDataSource::Time endTime,
    std::vector<PXR_NS::HdSampledDataSource::Time>* outSampleTimes,
    const SampleVec& samples, std::shared_mutex& samplesMutex)
{
    if (!outSampleTimes)
        return false;

    std::shared_lock<std::shared_mutex> readLock(samplesMutex);
    outSampleTimes->clear();

    // Return all samples that are within the start and end time
    for (const auto& sample : samples)
    {
        if (sample.time >= startTime && sample.time <= endTime)
            outSampleTimes->push_back(sample.time);
    }
    return !outSampleTimes->empty();
}

template<typename SampleVec>
std::vector<PXR_NS::HdSampledDataSource::Time> SampledGetAllTimes(
    const SampleVec& samples, std::shared_mutex& samplesMutex)
{
    std::shared_lock<std::shared_mutex> readLock(samplesMutex);
    std::vector<PXR_NS::HdSampledDataSource::Time> times;
    times.reserve(samples.size());
    for (const auto& s : samples)
        times.push_back(s.time);
    return times;
}

} // namespace HdPageableDataSourceUtils

} // namespace HVT_NS