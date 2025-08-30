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
#include <hvt/pageableBuffer/pageableMemoryMonitor.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

std::string FormatBytes(size_t bytes)
{
    if (bytes >= ONE_GiB)
    {
        return TfStringPrintf(
            "%.2f GiB", static_cast<double>(bytes) / static_cast<double>(ONE_GiB));
    }
    else if (bytes >= ONE_MiB)
    {
        return TfStringPrintf("%.2f MiB", static_cast<double>(bytes) / static_cast<double>(ONE_MiB));
    }
    else if (bytes >= ONE_KiB)
    {
        return TfStringPrintf("%.2f KiB", static_cast<double>(bytes) / static_cast<double>(ONE_KiB));
    }
    else
    {
        return TfStringPrintf("%zu bytes", bytes);
    }
}

HdMemoryMonitor::HdMemoryMonitor(size_t sceneMemoryLimit, size_t rendererMemoryLimit) :
    mSceneMemoryLimit(sceneMemoryLimit), mRendererMemoryLimit(rendererMemoryLimit)
{
}

void HdMemoryMonitor::AddSceneMemory(size_t size)
{
    mUsedSceneMemory += size;
}

void HdMemoryMonitor::ReduceSceneMemory(size_t size)
{
    size_t current = mUsedSceneMemory.load();
    if (current < size)
    {
        // Set to zero if trying to subtract more than available
        mUsedSceneMemory = 0;
    }
    else
    {
        mUsedSceneMemory -= size;
    }
}

void HdMemoryMonitor::AddRendererMemory(size_t size)
{
    mUsedRendererMemory += size;
}

void HdMemoryMonitor::ReduceRendererMemory(size_t size)
{
    size_t current = mUsedRendererMemory.load();
    if (current < size)
    {
        mUsedRendererMemory = 0;
    }
    else
    {
        mUsedRendererMemory -= size;
    }
}

float HdMemoryMonitor::GetSceneMemoryPressure() const
{
    return static_cast<float>(mUsedSceneMemory.load()) / static_cast<float>(mSceneMemoryLimit);
}

float HdMemoryMonitor::GetRendererMemoryPressure() const
{
    return static_cast<float>(mUsedRendererMemory.load()) /
        static_cast<float>(mRendererMemoryLimit);
}

void HdMemoryMonitor::PrintMemoryStats() const
{
    size_t usedScene       = mUsedSceneMemory.load();
    size_t usedRenderer    = mUsedRendererMemory.load();
    float scenePressure    = GetSceneMemoryPressure();
    float hardwarePressure = GetRendererMemoryPressure();

    // clang-format off
    TF_STATUS(
        "\n=== Memory Statistics ===\n"
        "Scene Memory:\n"
        "  Used: %s / %s\n"
        "  Pressure: %.1f%%\n"
        "Renderer Memory:\n"
        "  Used: %s / %s\n"
        "  Pressure: %.1f%%\n"
        "Thresholds:\n"
        "  Renderer Paging: %.1f%%\n"
        "  Scene Paging: %.1f%%\n"
        "  Low Memory: %.1f%%\n"
        "=========================\n",
        FormatBytes(usedScene).c_str(), FormatBytes(mSceneMemoryLimit).c_str(),
        scenePressure * 100,
        FormatBytes(usedRenderer).c_str(), FormatBytes(mRendererMemoryLimit).c_str(),
        hardwarePressure * 100,
        RENDERER_PAGING_THRESHOLD * 100, SCENE_PAGING_THRESHOLD * 100, LOW_MEMORY_THRESHOLD * 100);
    // clang-format on
}

} // namespace HVT_NS
