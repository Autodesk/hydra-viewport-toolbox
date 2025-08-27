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
#include <hvt/pageableBuffer/pageableDataSource.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

// HdPageableValue Implementation /////////////////////////////////////////////

HdPageableValue::HdPageableValue(const SdfPath& path, size_t estimatedSize, HdBufferUsage usage,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    const VtValue& data, const TfToken& dataType) :
    HdPageableBufferBase(
        path, estimatedSize, usage, pageFileManager, memoryMonitor, destructionCallback),
    mSourceValue(data),
    mDataType(dataType)
{
    // Initially state in scene memory.
    HdPageableBufferBase::CreateSceneBuffer();
}

VtValue HdPageableValue::GetValue()
{
    // If data is resident in memory, return cached value
    if (IsDataResident())
    {
        return mSourceValue;
    }

    // Load from disk if needed
    if (static_cast<int>(GetBufferState()) & static_cast<int>(HdBufferState::DiskBuffer))
    {
        [[maybe_unused]] bool result = PageToSceneMemory();
    }

    return mSourceValue;
}

bool HdPageableValue::SwapToSceneMemory(bool force, HdBufferState releaseBuffer)
{
    if (HdPageableBufferBase::SwapToSceneMemory(force, releaseBuffer))
    {
        // Load data from disk into scene memory
        if (mPageHandle && HasValidDiskBuffer())
        {
            std::vector<uint8_t> buffer(mPageHandle->Size());
            if (mPageFileManager->LoadPage(*mPageHandle, buffer.data()))
            {
                mSourceValue = DeserializeVtValue(buffer);
            }
        }
        return true;
    }
    return false;
}

bool HdPageableValue::SwapSceneToDisk(bool force)
{
    if (mSourceValue.IsEmpty() && !force)
    {
        return false;
    }

    if (HdPageableBufferBase::SwapSceneToDisk(force))
    {
        // Clear memory copy to save memory
        mSourceValue = VtValue();
        return true;
    }

    return false;
}

// Utility methods.

TfSpan<const std::byte> HdPageableValue::GetSceneMemorySpan() const noexcept
{
    // TODO...
    static std::vector<uint8_t> buffer; // Make static to avoid returning span to local
    buffer = SerializeVtValue(mSourceValue);
    return TfSpan<const std::byte>(
        reinterpret_cast<const std::byte*>(buffer.data()), buffer.size());
}

TfSpan<std::byte> HdPageableValue::GetSceneMemorySpan() noexcept
{
    // TODO...
    static std::vector<uint8_t> buffer; // Make static to avoid returning span to local
    buffer = SerializeVtValue(mSourceValue);
    return TfSpan<std::byte>(reinterpret_cast<std::byte*>(buffer.data()), buffer.size());
}

std::vector<uint8_t> HdPageableValue::SerializeVtValue(const VtValue& value) const noexcept
{
    // Serialize the paged data into VtValue.
    std::vector<uint8_t> result;

    if (value.IsHolding<VtFloatArray>())
    {
        auto array      = value.UncheckedGet<VtFloatArray>();
        size_t byteSize = array.size() * sizeof(float);
        result.resize(byteSize);
        std::memcpy(result.data(), array.cdata(), byteSize);
    }
    // TODO: other types...

    return result;
}

VtValue HdPageableValue::DeserializeVtValue(const std::vector<uint8_t>& data) noexcept
{
    // Simplified deserialization - would reconstruct based on mDataType
    if (mDataType == HdTokens->points)
    {
        size_t floatCount = data.size() / sizeof(float);
        VtFloatArray array(floatCount);
        std::memcpy(array.data(), data.data(), data.size());
        return VtValue(array);
    }

    return VtValue();
}

size_t HdPageableValue::EstimateMemoryUsage(const VtValue& value) noexcept
{
    if (value.IsHolding<VtFloatArray>())
    {
        return value.UncheckedGet<VtFloatArray>().size() * sizeof(float);
    }
    else if (value.IsHolding<VtIntArray>())
    {
        return value.UncheckedGet<VtIntArray>().size() * sizeof(int);
    }
    else if (value.IsHolding<VtVec3fArray>())
    {
        return value.UncheckedGet<VtVec3fArray>().size() * sizeof(GfVec3f);
    }
    // Add other types as needed

    return 1024; // Default estimate
}

// HdPageableSampledDataSource Implementation /////////////////////////////////

HdPageableSampledDataSource::Handle HdPageableSampledDataSource::New(const VtValue& value,
    const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage)
{
    return Handle(new HdPageableSampledDataSource(value, primPath, attributeName, pageFileManager,
        memoryMonitor, destructionCallback, usage));
}

HdPageableSampledDataSource::Handle HdPageableSampledDataSource::New(
    const std::map<Time, VtValue>& samples, const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage)
{
    return Handle(new HdPageableSampledDataSource(samples, primPath, attributeName, pageFileManager,
        memoryMonitor, destructionCallback, usage));
}

HdPageableSampledDataSource::HdPageableSampledDataSource(const VtValue& value,
    const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage) :
    HdPageableBufferBase(primPath, HdPageableValue::EstimateMemoryUsage(value), usage,
        pageFileManager, memoryMonitor, destructionCallback),
    mPrimPath(primPath),
    mAttributeName(attributeName)
{

    // Create single sample at default time
    auto bufferKey = GetBufferKey(0.0);
    auto buffer    = std::make_shared<HdPageableValue>(
        SdfPath(bufferKey), Size(), usage, pageFileManager, memoryMonitor, [](const SdfPath&) {},
        value, attributeName);

    mSamples.push_back({ 0.0, buffer });
}

HdPageableSampledDataSource::HdPageableSampledDataSource(const std::map<Time, VtValue>& samples,
    const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage) :
    HdPageableBufferBase(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback),
    mPrimPath(primPath),
    mAttributeName(attributeName)
{
    for (const auto& [time, value] : samples)
    {
        auto bufferKey = GetBufferKey(time);
        auto buffer    = std::make_shared<HdPageableValue>(
            SdfPath(bufferKey), Size(), usage, pageFileManager, memoryMonitor,
            [](const SdfPath&) {}, value, attributeName);
        mSamples.push_back({ time, buffer });
    }
}

VtValue HdPageableSampledDataSource::GetValue(Time shutterOffset)
{
    // Find closest sample(s)
    if (mSamples.empty())
    {
        return VtValue();
    }

    if (mSamples.size() == 1)
    {
        return mSamples[0].buffer->GetValue();
    }

    // Find surrounding samples for interpolation
    auto it = std::lower_bound(mSamples.begin(), mSamples.end(), shutterOffset,
        [](const MemorySample& sample, Time time) { return sample.time < time; });

    if (it == mSamples.end())
    {
        return mSamples.back().buffer->GetValue();
    }

    if (it == mSamples.begin())
    {
        return mSamples.front().buffer->GetValue();
    }

    // For now, return exact match or closest
    // TODO: Implement interpolation for time-sampled data
    return it->buffer->GetValue();
}

bool HdPageableSampledDataSource::GetContributingSampleTimesForInterval(
    Time startTime, Time endTime, std::vector<Time>* outSampleTimes)
{

    if (!outSampleTimes)
    {
        return false;
    }

    outSampleTimes->clear();

    for (const auto& sample : mSamples)
    {
        if (sample.time >= startTime && sample.time <= endTime)
        {
            outSampleTimes->push_back(sample.time);
        }
    }

    return !outSampleTimes->empty();
}

std::string HdPageableSampledDataSource::GetBufferKey(Time time) const
{
    return TfStringPrintf("%s:%s:%f", mPrimPath.GetText(), mAttributeName.GetText(), time);
}

// HdMemoryManager Implementation /////////////////////////////////////////////

HdMemoryManager::HdMemoryManager(
    std::filesystem::path pageFileDirectory, size_t sceneMemoryLimit, size_t rendererMemoryLimit) :
    mBufferManager({ .pageFileDirectory = pageFileDirectory,
        .sceneMemoryLimit               = sceneMemoryLimit,
        .rendererMemoryLimit            = rendererMemoryLimit,
        .ageLimit                       = 20,
        .numThreads                     = 2 })
{
    mCleanupThread = std::thread(&HdMemoryManager::BackgroundCleanupLoop, this);
}

HdMemoryManager::~HdMemoryManager()
{
    mBackgroundCleanupEnabled = false;
    if (mCleanupThread.joinable())
    {
        mCleanupThread.join();
    }
}

std::shared_ptr<HdPageableBufferBase> HdMemoryManager::GetOrCreateBuffer(
    const PXR_NS::SdfPath& primPath, const VtValue& data, const TfToken& dataType)
{

    // Check if buffer already exists
    auto existingBuffer = mBufferManager.FindBuffer(primPath);
    if (existingBuffer)
    {
        return existingBuffer;
    }

    // Create new HdPageableValue buffer
    size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(data);

    auto& pageFileManager    = GetPageFileManager();
    auto& memoryMonitor      = GetMemoryMonitor();
    auto destructionCallback = [this](const PXR_NS::SdfPath& path)
    { mBufferManager.RemoveBuffer(path); };

    auto buffer = std::make_shared<HdPageableValue>(primPath, estimatedSize, HdBufferUsage::Static,
        pageFileManager, memoryMonitor, destructionCallback, data, dataType);

    // Register with buffer manager
    mBufferManager.AddBuffer(primPath, buffer);

    return buffer;
}

void HdMemoryManager::BackgroundCleanupLoop()
{
    using namespace std::chrono_literals;

    while (mBackgroundCleanupEnabled)
    {
        // Sleep with periodic checks for stop request
        for (int i = 0; i < 10 && mBackgroundCleanupEnabled; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(mFreeCrawlInterval.load()));
        }

        if (!mBackgroundCleanupEnabled)
        {
            break;
        }

        // Perform periodic free crawl
        auto& monitor          = mBufferManager.GetMemoryMonitor();
        float scenePressure    = monitor->GetSceneMemoryPressure();
        float rendererPressure = monitor->GetRendererMemoryPressure();

        if (scenePressure > HdMemoryMonitor::LOW_MEMORY_THRESHOLD ||
            rendererPressure > HdMemoryMonitor::LOW_MEMORY_THRESHOLD)
        {
            mBufferManager.FreeCrawl(mFreeCrawlPercentage);
        }
    }
}

// Utility Functions //////////////////////////////////////////////////////////

namespace HdPageableDataSourceUtils
{

HdDataSourceBaseHandle CreateFromValue(const VtValue& value, const SdfPath& primPath,
    const TfToken& name, const std::shared_ptr<HdMemoryManager>& memoryManager)
{
    // TODO: ...
    // Create destruction callback
    auto destructionCallback = [](const PXR_NS::SdfPath& /*path*/)
    {
        // Managed through buffer manager
    };

    if (value.IsArrayValued())
    {
        // Array data
        return HdPageableSampledDataSource::New(value, primPath, name,
            memoryManager->GetPageFileManager(), memoryManager->GetMemoryMonitor(),
            destructionCallback);
    }
    else
    {
        // Scalar values
        return HdPageableSampledDataSource::New(value, primPath, name,
            memoryManager->GetPageFileManager(), memoryManager->GetMemoryMonitor(),
            destructionCallback);
    }
}

PXR_NS::HdContainerDataSourceHandle CreateContainer(const std::map<TfToken, VtValue>& /*values*/,
    const SdfPath& /*primPath*/, const std::shared_ptr<HdMemoryManager>& /*memoryManager*/)
{
    // TODO: ...
    // return HdPageableContainerDataSource::New(primPath);
    return nullptr;
}

PXR_NS::HdVectorDataSourceHandle CreateVector(const std::vector<VtValue>& /*values*/,
    const SdfPath& /*primPath*/, const std::shared_ptr<HdMemoryManager>& /*memoryManager*/)
{
    // TODO: ...
    // return HdPageableVectorDataSource::New(primPath);
    return nullptr;
}

PXR_NS::HdSampledDataSourceHandle CreateTimeSampled(
    const std::map<HdSampledDataSource::Time, VtValue>& /*samples*/, const SdfPath& /*primPath*/,
    const TfToken& /*name*/, const std::shared_ptr<HdMemoryManager>& /*memoryManager*/)
{
    // TODO: ...
    // return HdPageableSampledDataSource::New(samples, primPath, name);
    return nullptr;
}

PXR_NS::HdBlockDataSourceHandle CreateBlock(const VtValue& /*value*/, const SdfPath& /*primPath*/,
    const std::shared_ptr<HdMemoryManager>& /*memoryManager*/)
{
    // TODO: ...
    // return HdPageableBlockDataSource::New(primPath);
    return nullptr;
}

} // namespace HdPageableDataSourceUtils

} // namespace HVT_NS