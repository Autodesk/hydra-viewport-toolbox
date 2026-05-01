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
#include <hvt/pageableBuffer/pageableDataSource.h>
#include <hvt/pageableBuffer/pageableStrategies.h>

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/retainedDataSource.h>

#include <atomic>
#include <map>
#include <shared_mutex>
#include <vector>

namespace HVT_NS
{

/// Memory-managed retained container data source with implicit paging.
/// Implements HdContainerDataSource with automatic paging support.
class HVT_API HdPageableRetainedContainerDataSource : public PXR_NS::HdRetainedContainerDataSource,
                                                      public HdPageableBufferBase<>
{
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableRetainedContainerDataSource);

    /// Create from existing name/data source pairs with paging support
    static Handle New(size_t count, const PXR_NS::TfToken* names,
        const PXR_NS::HdDataSourceBaseHandle* values, const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Create from map of token to VtValue pairs
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

    /// Check if element is resident
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
    HdPageableRetainedContainerDataSource(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage, bool enableImplicitPaging);

    std::vector<uint8_t> SerializePacked() const;
    bool DeserializePacked(const std::vector<uint8_t>& packedData);

    mutable std::shared_mutex mElementsMutex;
    std::map<PXR_NS::TfToken, std::shared_ptr<HdPageableValue>> mPageableElements;
    std::map<PXR_NS::TfToken, HdContainerPageEntry> mContainerPageEntries;

    std::shared_ptr<IHdValueSerializer> mSerializer;

    const bool mEnableImplicitPaging { true };
    mutable HvtDebugCounter mAccessCount {};
    mutable HvtDebugCounter mPageInCount {};
    mutable HvtDebugCounter mPageOutCount {};
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableRetainedContainerDataSource);

/// Memory-managed retained small vector data source with implicit paging.
/// Implements HdVectorDataSource with automatic paging support.
class HVT_API HdPageableRetainedSmallVectorDataSource
    : public PXR_NS::HdRetainedSmallVectorDataSource,
      public HdPageableBufferBase<>
{
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableRetainedSmallVectorDataSource);

    /// Create from existing data sources with paging support
    static Handle New(size_t count, const PXR_NS::HdDataSourceBaseHandle* values,
        const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Create from vector of VtValues
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

    /// Check if element is resident
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
    HdPageableRetainedSmallVectorDataSource(const PXR_NS::SdfPath& primPath,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage,
        bool enableImplicitPaging = true);

    std::vector<uint8_t> SerializePacked() const;
    bool DeserializePacked(const std::vector<uint8_t>& packedData);

    mutable std::shared_mutex mElementsMutex;
    std::vector<std::shared_ptr<HdPageableValue>> mPageableElements;
    std::vector<HdContainerPageEntry> mElements;

    std::shared_ptr<IHdValueSerializer> mSerializer;

    const bool mEnableImplicitPaging { true };
    mutable HvtDebugCounter mAccessCount {};
    mutable HvtDebugCounter mPageInCount {};
    mutable HvtDebugCounter mPageOutCount {};
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableRetainedSmallVectorDataSource);

/// Memory-managed retained sampled data source for time-sampled values.
/// Implements HdSampledDataSource with implicit paging and observability.
class HVT_API HdPageableRetainedSampledDataSource : public PXR_NS::HdRetainedSampledDataSource,
                                                    public HdPageableBufferBase<>
{
public:
    HD_DECLARE_DATASOURCE_ABSTRACT(HdPageableRetainedSampledDataSource);

    /// Interpolation mode for time samples (matches HdPageableSampledDataSource)
    using InterpolationMode = HdPageableSampledDataSource::InterpolationMode;

    /// Create with single value and memory management
    static Handle New(const PXR_NS::VtValue& value, const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Create time-sampled with memory management
    static Handle New(const std::map<PXR_NS::HdSampledDataSource::Time, PXR_NS::VtValue>& samples,
        const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// HdSampledDataSource interface. These may trigger implicit paging.
    PXR_NS::VtValue GetValue(Time shutterOffset) override;
    bool GetContributingSampleTimesForInterval(
        Time startTime, Time endTime, std::vector<Time>* outSampleTimes) override;

    /// Get value without triggering implicit paging
    PXR_NS::VtValue GetValueIfResident(Time shutterOffset) const;

    /// Configuration
    void SetInterpolationMode(InterpolationMode mode) { mInterpolationMode = mode; }
    InterpolationMode GetInterpolationMode() const { return mInterpolationMode; }

    /// Check if sample is resident
    bool IsSampleResident(Time time) const;

    /// Get all available sample times
    std::vector<Time> GetAllSampleTimes() const;

    bool IsImplicitPagingEnabled() const { return mEnableImplicitPaging; }

    /// Observability metrics
    size_t GetAccessCount() const { return mAccessCount.load(); }
    size_t GetPageInCount() const { return mPageInCount.load(); }
    size_t GetPageOutCount() const { return mPageOutCount.load(); }

private:
    HdPageableRetainedSampledDataSource(const PXR_NS::VtValue& value,
        const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage,
        bool enableImplicitPaging = true);

    HdPageableRetainedSampledDataSource(
        const std::map<Time, PXR_NS::VtValue>& samples, const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage,
        bool enableImplicitPaging = true);

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
};
HD_DECLARE_DATASOURCE_HANDLES(HdPageableRetainedSampledDataSource);

/// Memory-managed retained typed sampled data source.
/// Implements HdTypedSampledDataSource with implicit paging support.
template <typename T>
class HVT_API HdPageableRetainedTypedSampledDataSource
    : public PXR_NS::HdRetainedTypedSampledDataSource<T>,
      public HdPageableBufferBase<>
{
public:
    using Handle = std::shared_ptr<HdPageableRetainedTypedSampledDataSource<T>>;
    static Handle Cast(const PXR_NS::HdDataSourceBase::Handle& v)
    {
        return std::dynamic_pointer_cast<HdPageableRetainedTypedSampledDataSource<T>>(v);
    }
    using Type = T;
    using Time = typename PXR_NS::HdTypedSampledDataSource<T>::Time;

    /// Create typed sampled data source
    static typename HdPageableRetainedTypedSampledDataSource<T>::Handle New(const T& value,
        const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Create time-sampled typed data source
    static typename HdPageableRetainedTypedSampledDataSource<T>::Handle New(
        const std::map<Time, T>& samples, const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage = HdBufferUsage::Static,
        bool enableImplicitPaging = true);

    /// Returns the typed value. It may trigger implicit paging.
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
    HdPageableRetainedTypedSampledDataSource(const T& value, const PXR_NS::SdfPath& primPath,
        const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage,
        bool enableImplicitPaging = true);

    HdPageableRetainedTypedSampledDataSource(const std::map<Time, T>& samples,
        const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback,
        HdBufferUsage usage,
        bool enableImplicitPaging = true);

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
    mutable HvtDebugCounter mAccessCount {};
    mutable HvtDebugCounter mPageInCount {};
};

template <typename T>
HdPageableRetainedTypedSampledDataSource<T>::HdPageableRetainedTypedSampledDataSource(
    const T& value, const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
    DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    PXR_NS::HdRetainedTypedSampledDataSource<T>(value),
    HdPageableBufferBase<>(primPath,
        HdPageableValue::EstimateMemoryUsage(PXR_NS::VtValue(value)),
        usage, pageFileManager, memoryMonitor, destructionCallback),
    mEnableImplicitPaging(enableImplicitPaging),
    mPrimPath(primPath),
    mAttributeName(attributeName)
{
    auto key = HdPageableDataSourceUtils::SampledGetBufferKey(0.0f, primPath, attributeName);
    auto buffer = std::make_shared<HdPageableValue>(PXR_NS::SdfPath(key), Size(), usage,
        pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback,
        PXR_NS::VtValue(value), attributeName, mEnableImplicitPaging);
    mSamples.push_back({ 0.0f, buffer });
}

template <typename T>
HdPageableRetainedTypedSampledDataSource<T>::HdPageableRetainedTypedSampledDataSource(
    const std::map<Time, T>& samples, const PXR_NS::SdfPath& primPath,
    const PXR_NS::TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
    DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    PXR_NS::HdRetainedTypedSampledDataSource<T>(samples.empty() ? T{} : samples.begin()->second),
    HdPageableBufferBase<>(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback),
    mEnableImplicitPaging(enableImplicitPaging),
    mPrimPath(primPath),
    mAttributeName(attributeName)
{
    for (const auto& [time, value] : samples)
    {
        auto key = HdPageableDataSourceUtils::SampledGetBufferKey(time, primPath, attributeName);
        size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(PXR_NS::VtValue(value));
        auto buffer = std::make_shared<HdPageableValue>(PXR_NS::SdfPath(key), estimatedSize, usage,
            pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback,
            PXR_NS::VtValue(value), attributeName, mEnableImplicitPaging);
        mSamples.push_back({ time, buffer });
    }
    std::sort(mSamples.begin(), mSamples.end(),
        [](const TypedSample& a, const TypedSample& b) { return a.time < b.time; });
}

template <typename T>
typename HdPageableRetainedTypedSampledDataSource<T>::Handle
HdPageableRetainedTypedSampledDataSource<T>::New(const T& value,
    const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
    DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableRetainedTypedSampledDataSource<T>(value, primPath, attributeName,
        pageFileManager, memoryMonitor, destructionCallback, usage, enableImplicitPaging));
}

template <typename T>
typename HdPageableRetainedTypedSampledDataSource<T>::Handle
HdPageableRetainedTypedSampledDataSource<T>::New(const std::map<Time, T>& samples,
    const PXR_NS::SdfPath& primPath, const PXR_NS::TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
    DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableRetainedTypedSampledDataSource<T>(samples, primPath, attributeName,
        pageFileManager, memoryMonitor, destructionCallback, usage, enableImplicitPaging));
}

template <typename T>
PXR_NS::VtValue HdPageableRetainedTypedSampledDataSource<T>::GetValue(Time shutterOffset)
{
    return HdPageableDataSourceUtils::SampledGetValue(shutterOffset, mSamples, mSamplesMutex,
        mEnableImplicitPaging, HdPageableSampledDataSource::InterpolationMode::None,
        mAccessCount, mPageInCount);
}

template <typename T>
T HdPageableRetainedTypedSampledDataSource<T>::GetTypedValue(Time shutterOffset)
{
    PXR_NS::VtValue v = GetValue(shutterOffset);
    if (v.IsHolding<T>())
        return v.UncheckedGet<T>();
    return T{};
}

template <typename T>
bool HdPageableRetainedTypedSampledDataSource<T>::GetContributingSampleTimesForInterval(
    Time startTime, Time endTime, std::vector<Time>* outSampleTimes)
{
    return HdPageableDataSourceUtils::SampledGetContributingTimes(
        startTime, endTime, outSampleTimes, mSamples, mSamplesMutex);
}

template <typename T>
T HdPageableRetainedTypedSampledDataSource<T>::GetTypedValueIfResident(Time shutterOffset) const
{
    std::shared_lock<std::shared_mutex> readLock(mSamplesMutex);
    const auto* sample = HdPageableDataSourceUtils::SampledFindSample(shutterOffset, mSamples);
    if (sample && sample->buffer->IsDataResident())
    {
        PXR_NS::VtValue v = sample->buffer->GetValueIfResident();
        if (v.IsHolding<T>())
            return v.UncheckedGet<T>();
    }
    return T{};
}

template <typename T>
bool HdPageableRetainedTypedSampledDataSource<T>::IsResident() const
{
    std::shared_lock<std::shared_mutex> readLock(mSamplesMutex);
    for (const auto& sample : mSamples)
    {
        if (!sample.buffer->IsDataResident())
            return false;
    }
    return !mSamples.empty();
}

} // namespace HVT_NS
