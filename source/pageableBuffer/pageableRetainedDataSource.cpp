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
#include <hvt/pageableBuffer/pageableRetainedDataSource.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

// HdPageableRetainedContainerDataSource Implementation ///////////////////////

HdPageableRetainedContainerDataSource::Handle HdPageableRetainedContainerDataSource::New(
    size_t count, const TfToken* names, const HdDataSourceBaseHandle* values,
    const SdfPath& primPath, const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    auto result = Handle(new HdPageableRetainedContainerDataSource(primPath, pageFileManager,
        memoryMonitor, destructionCallback, usage, enableImplicitPaging));

    for (size_t i = 0; i < count; ++i)
    {
        const TfToken& token = names[i];

        // Try to extract VtValue from sampled data source
        if (auto sampledDs = HdSampledDataSource::Cast(values[i]))
        {
            VtValue value        = sampledDs->GetValue(0.0f);
            auto elementPath     = primPath.AppendProperty(token);
            size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(value);

            auto pageableValue = std::make_shared<HdPageableValue>(elementPath, estimatedSize,
                usage, pageFileManager, memoryMonitor,
                HdPageableDataSourceUtils::kNoOpDestructionCallback, value, token,
                result->mEnableImplicitPaging, result->mSerializer.get());

            result->mPageableElements[token] = pageableValue;
            result->mContainerPageEntries[token] =
                HdContainerPageEntry { typeid(value), token, 0, estimatedSize };
        }
    }

    return result;
}

HdPageableRetainedContainerDataSource::Handle HdPageableRetainedContainerDataSource::New(
    const std::map<TfToken, VtValue>& values, const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    auto result = Handle(new HdPageableRetainedContainerDataSource(primPath, pageFileManager,
        memoryMonitor, destructionCallback, usage, enableImplicitPaging));

    // Create pageable values for each element
    for (const auto& [token, value] : values)
    {
        auto elementPath     = primPath.AppendProperty(token);
        size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(value);

        auto pageableValue = std::make_shared<HdPageableValue>(elementPath, estimatedSize, usage,
            pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback,
            value, token, result->mEnableImplicitPaging, result->mSerializer.get());

        result->mPageableElements[token] = pageableValue;
        result->mContainerPageEntries[token] =
            HdContainerPageEntry { typeid(value), token, 0, estimatedSize };
    }

    return result;
}

HdPageableRetainedContainerDataSource::Handle HdPageableRetainedContainerDataSource::New(
    const SdfPath& primPath, const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableRetainedContainerDataSource(primPath, pageFileManager,
        memoryMonitor, destructionCallback, usage, enableImplicitPaging));
}

HdPageableRetainedContainerDataSource::HdPageableRetainedContainerDataSource(
    const SdfPath& primPath, const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    HdPageableBufferBase<>(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback),
    mSerializer(std::make_shared<HdDefaultValueSerializer>()),
    mEnableImplicitPaging(enableImplicitPaging)
{
}

TfTokenVector HdPageableRetainedContainerDataSource::GetNames()
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    TfTokenVector names;
    names.reserve(mPageableElements.size());
    for (const auto& [token, _] : mPageableElements)
    {
        names.push_back(token);
    }
    return names;
}

HdDataSourceBaseHandle HdPageableRetainedContainerDataSource::Get(const TfToken& name)
{
    return HdPageableDataSourceUtils::ContainerGet(
        name, mPageableElements, mContainerPageEntries, mElementsMutex, mEnableImplicitPaging,
        HasValidDiskBuffer(), mPageEntry, mPageFileManager, *mSerializer,
        mAccessCount, mPageInCount);
}

std::map<TfToken, HdContainerPageEntry> HdPageableRetainedContainerDataSource::GetMemoryBreakdown()
    const
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    return mContainerPageEntries;
}

bool HdPageableRetainedContainerDataSource::IsElementResident(const TfToken& name) const
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    auto it = mPageableElements.find(name);
    return it != mPageableElements.end() && it->second->IsDataResident();
}

bool HdPageableRetainedContainerDataSource::PageInElement(const TfToken& name)
{
    return HdPageableDataSourceUtils::ContainerPageIn(
        name, mPageableElements, mContainerPageEntries, mElementsMutex, HasValidDiskBuffer(),
        mPageEntry, mPageFileManager, *mSerializer, mPageInCount);
}

bool HdPageableRetainedContainerDataSource::PageOutElement(const TfToken& name)
{
    return HdPageableDataSourceUtils::ContainerPageOut(name, mPageableElements, mElementsMutex,
        HasValidDiskBuffer(), mPageEntry, mPageFileManager, mBufferState, *mSerializer,
        mPageOutCount);
}

bool HdPageableRetainedContainerDataSource::SwapSceneToDisk(
    bool force, HdBufferState /*releaseBuffer*/)
{
    return HdPageableDataSourceUtils::ContainerSwapToDisk(mPageableElements, force, mElementsMutex,
        mPageEntry, mPageFileManager, mBufferState, *mSerializer, mPageOutCount);
}

bool HdPageableRetainedContainerDataSource::SwapToSceneMemory(
    bool /*force*/, HdBufferState /*releaseBuffer*/)
{
    return HdPageableDataSourceUtils::ContainerSwapToMemory(
        mPageableElements, mContainerPageEntries, mElementsMutex, HasValidDiskBuffer(), mPageEntry,
        mPageFileManager, mBufferState, *mSerializer, mPageInCount);
}

std::vector<uint8_t> HdPageableRetainedContainerDataSource::SerializePacked() const
{
    return HdPageableDataSourceUtils::SerializeContainerPacked(mPageableElements, *mSerializer);
}

bool HdPageableRetainedContainerDataSource::DeserializePacked(
    const std::vector<uint8_t>& packedData)
{
    return HdPageableDataSourceUtils::DeserializeContainerPacked(
        packedData, mPageableElements, mContainerPageEntries, *mSerializer);
}

// HdPageableRetainedSmallVectorDataSource Implementation /////////////////////

HdPageableRetainedSmallVectorDataSource::Handle HdPageableRetainedSmallVectorDataSource::New(
    size_t count, const HdDataSourceBaseHandle* values, const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    auto result = Handle(new HdPageableRetainedSmallVectorDataSource(primPath, pageFileManager,
        memoryMonitor, destructionCallback, usage, enableImplicitPaging));

    for (size_t i = 0; i < count; ++i)
    {
        // Try to extract VtValue from sampled data source
        if (auto sampledDs = HdSampledDataSource::Cast(values[i]))
        {
            VtValue value        = sampledDs->GetValue(0.0f);
            auto elementPath     = SdfPath(TfStringPrintf("%s_%zu", primPath.GetText(), i));
            size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(value);

            TfToken dataType(TfStringPrintf("element_%zu", i));
            auto pageableValue = std::make_shared<HdPageableValue>(elementPath, estimatedSize,
                usage, pageFileManager, memoryMonitor,
                HdPageableDataSourceUtils::kNoOpDestructionCallback, value, dataType,
                result->mEnableImplicitPaging, result->mSerializer.get());

            result->mPageableElements.push_back(pageableValue);
            result->mElements.push_back(
                HdContainerPageEntry { typeid(value), dataType, i, estimatedSize });
        }
    }

    return result;
}

HdPageableRetainedSmallVectorDataSource::Handle HdPageableRetainedSmallVectorDataSource::New(
    const std::vector<VtValue>& values, const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    auto result = Handle(new HdPageableRetainedSmallVectorDataSource(primPath, pageFileManager,
        memoryMonitor, destructionCallback, usage, enableImplicitPaging));

    // Create pageable values for each element
    for (size_t index = 0; index < values.size(); ++index)
    {
        const auto& value    = values[index];
        auto elementPath     = SdfPath(TfStringPrintf("%s_%zu", primPath.GetText(), index));
        size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(value);

        TfToken dataType(TfStringPrintf("element_%zu", index));
        auto pageableValue = std::make_shared<HdPageableValue>(elementPath, estimatedSize, usage,
            pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback,
            value, dataType, result->mEnableImplicitPaging, result->mSerializer.get());

        result->mPageableElements.push_back(pageableValue);
        result->mElements.push_back(
            HdContainerPageEntry { typeid(value), dataType, index, estimatedSize });
    }

    return result;
}

HdPageableRetainedSmallVectorDataSource::Handle HdPageableRetainedSmallVectorDataSource::New(
    const SdfPath& primPath, const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableRetainedSmallVectorDataSource(primPath, pageFileManager,
        memoryMonitor, destructionCallback, usage, enableImplicitPaging));
}

HdPageableRetainedSmallVectorDataSource::HdPageableRetainedSmallVectorDataSource(
    const SdfPath& primPath, const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    HdRetainedSmallVectorDataSource(0, nullptr),
    HdPageableBufferBase<>(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback),
    mSerializer(std::make_shared<HdDefaultValueSerializer>()),
    mEnableImplicitPaging(enableImplicitPaging)
{
}

size_t HdPageableRetainedSmallVectorDataSource::GetNumElements()
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    return mPageableElements.size();
}

HdDataSourceBaseHandle HdPageableRetainedSmallVectorDataSource::GetElement(size_t element)
{
    return HdPageableDataSourceUtils::VectorGetElement(
        element, mPageableElements, mElements, mElementsMutex, mEnableImplicitPaging,
        HasValidDiskBuffer(), mPageEntry, mPageFileManager, *mSerializer,
        mAccessCount, mPageInCount);
}

std::vector<HdContainerPageEntry> HdPageableRetainedSmallVectorDataSource::GetMemoryBreakdown()
    const
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    return mElements;
}

bool HdPageableRetainedSmallVectorDataSource::IsElementResident(size_t index) const
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    return index < mPageableElements.size() && mPageableElements[index]->IsDataResident();
}

bool HdPageableRetainedSmallVectorDataSource::PageInElement(size_t index)
{
    return HdPageableDataSourceUtils::VectorPageIn(index, mPageableElements, mElements,
        mElementsMutex, HasValidDiskBuffer(), mPageEntry, mPageFileManager, *mSerializer,
        mPageInCount);
}

bool HdPageableRetainedSmallVectorDataSource::PageOutElement(size_t index)
{
    return HdPageableDataSourceUtils::VectorPageOut(index, mPageableElements, mElementsMutex,
        HasValidDiskBuffer(), mPageEntry, mPageFileManager, mBufferState, *mSerializer,
        mPageOutCount);
}

bool HdPageableRetainedSmallVectorDataSource::SwapSceneToDisk(
    bool force, HdBufferState /*releaseBuffer*/)
{
    return HdPageableDataSourceUtils::VectorSwapToDisk(mPageableElements, force, mElementsMutex,
        mPageEntry, mPageFileManager, mBufferState, *mSerializer, mPageOutCount);
}

bool HdPageableRetainedSmallVectorDataSource::SwapToSceneMemory(
    bool /*force*/, HdBufferState /*releaseBuffer*/)
{
    return HdPageableDataSourceUtils::VectorSwapToMemory(
        mPageableElements, mElements, mElementsMutex, HasValidDiskBuffer(), mPageEntry,
        mPageFileManager, mBufferState, *mSerializer, mPageInCount);
}

std::vector<uint8_t> HdPageableRetainedSmallVectorDataSource::SerializePacked() const
{
    return HdPageableDataSourceUtils::SerializeVectorPacked(mPageableElements, *mSerializer);
}

bool HdPageableRetainedSmallVectorDataSource::DeserializePacked(
    const std::vector<uint8_t>& packedData)
{
    return HdPageableDataSourceUtils::DeserializeVectorPacked(
        packedData, mPageableElements, mElements, *mSerializer);
}

// HdPageableRetainedSampledDataSource Implementation /////////////////////////

HdPageableRetainedSampledDataSource::Handle HdPageableRetainedSampledDataSource::New(
    const VtValue& value, const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableRetainedSampledDataSource(value, primPath, attributeName,
        pageFileManager, memoryMonitor, destructionCallback, usage, enableImplicitPaging));
}

HdPageableRetainedSampledDataSource::Handle HdPageableRetainedSampledDataSource::New(
    const std::map<HdSampledDataSource::Time, VtValue>& samples, const SdfPath& primPath,
    const TfToken& attributeName, const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableRetainedSampledDataSource(samples, primPath, attributeName,
        pageFileManager, memoryMonitor, destructionCallback, usage, enableImplicitPaging));
}

HdPageableRetainedSampledDataSource::HdPageableRetainedSampledDataSource(const VtValue& value,
    const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    HdRetainedSampledDataSource(VtValue {}),
    HdPageableBufferBase<>(primPath, HdPageableValue::EstimateMemoryUsage(value), usage,
        pageFileManager, memoryMonitor, destructionCallback),
    mPrimPath(primPath),
    mAttributeName(attributeName),
    mEnableImplicitPaging(enableImplicitPaging)
{
    auto buffer = std::make_shared<HdPageableValue>(SdfPath(GetBufferKey(0.0)), Size(), usage,
        pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback, value,
        attributeName, mEnableImplicitPaging);

    mSamples.push_back({ 0.0, std::move(buffer) });
}

HdPageableRetainedSampledDataSource::HdPageableRetainedSampledDataSource(
    const std::map<Time, VtValue>& samples, const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    HdRetainedSampledDataSource(VtValue {}),
    HdPageableBufferBase<>(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback),
    mPrimPath(primPath),
    mAttributeName(attributeName),
    mEnableImplicitPaging(enableImplicitPaging)
{
    // Create pageable values for each sample and sort by time
    for (const auto& [time, value] : samples)
    {
        size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(value);
        auto buffer =
            std::make_shared<HdPageableValue>(SdfPath(GetBufferKey(time)), estimatedSize, usage,
                pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback,
                value, attributeName, mEnableImplicitPaging);

        mSamples.push_back({ time, std::move(buffer) });
    }

    std::sort(mSamples.begin(), mSamples.end(),
        [](const MemorySample& a, const MemorySample& b) { return a.time < b.time; });
}

VtValue HdPageableRetainedSampledDataSource::GetValue(Time shutterOffset)
{
    return HdPageableDataSourceUtils::SampledGetValue(
        shutterOffset, mSamples, mSamplesMutex, mEnableImplicitPaging, mInterpolationMode,
        mAccessCount, mPageInCount);
}

VtValue HdPageableRetainedSampledDataSource::GetValueIfResident(Time shutterOffset) const
{
    std::shared_lock<std::shared_mutex> readLock(mSamplesMutex);
    const auto* sample = HdPageableDataSourceUtils::SampledFindSample(shutterOffset, mSamples);
    if (sample && sample->buffer->IsDataResident())
        return sample->buffer->GetValueIfResident();
    return {};
}

bool HdPageableRetainedSampledDataSource::GetContributingSampleTimesForInterval(
    Time startTime, Time endTime, std::vector<Time>* outSampleTimes)
{
    return HdPageableDataSourceUtils::SampledGetContributingTimes(
        startTime, endTime, outSampleTimes, mSamples, mSamplesMutex);
}

bool HdPageableRetainedSampledDataSource::IsSampleResident(Time time) const
{
    std::shared_lock<std::shared_mutex> readLock(mSamplesMutex);
    const auto* sample = HdPageableDataSourceUtils::SampledFindSample(time, mSamples);
    return sample && sample->buffer->IsDataResident();
}

std::vector<HdPageableRetainedSampledDataSource::Time> HdPageableRetainedSampledDataSource::
    GetAllSampleTimes() const
{
    return HdPageableDataSourceUtils::SampledGetAllTimes(mSamples, mSamplesMutex);
}

std::string HdPageableRetainedSampledDataSource::GetBufferKey(Time time) const
{
    return HdPageableDataSourceUtils::SampledGetBufferKey(time, mPrimPath, mAttributeName);
}

} // namespace HVT_NS
