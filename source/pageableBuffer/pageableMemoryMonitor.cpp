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

HdMemoryMonitor::HdMemoryMonitor(size_t sceneMemoryLimit, size_t rendererMemoryLimit)
    : mSceneMemoryLimit(sceneMemoryLimit)
    , mRendererMemoryLimit(rendererMemoryLimit)
{
}

void HdMemoryMonitor::AddSceneMemory(size_t size) {
    mUsedSceneMemory += size;
}

void HdMemoryMonitor::ReduceSceneMemory(size_t size) {
    size_t current = mUsedSceneMemory.load();
    while (current >= size) {
        if (mUsedSceneMemory.compare_exchange_weak(current, current - size)) {
            break;
        }
    }
}

void HdMemoryMonitor::AddRendererMemory(size_t size) {
    mUsedRendererMemory += size;
}

void HdMemoryMonitor::ReduceRendererMemory(size_t size) {
    size_t current = mUsedRendererMemory.load();
    while (current >= size) {
        if (mUsedRendererMemory.compare_exchange_weak(current, current - size)) {
            break;
        }
    }
}

float HdMemoryMonitor::GetSceneMemoryPressure() const {
    return static_cast<float>(mUsedSceneMemory.load()) / static_cast<float>(mSceneMemoryLimit);
}

float HdMemoryMonitor::GetRendererMemoryPressure() const {
    return static_cast<float>(mUsedRendererMemory.load()) / static_cast<float>(mRendererMemoryLimit);
}

void HdMemoryMonitor::PrintMemoryStats() const {
    // Helper function to format byte sizes
    auto formatBytes = [](size_t bytes) -> std::string {
        if (bytes >= 1024ULL * 1024 * 1024) {
            return TfStringPrintf("%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        } else if (bytes >= 1024ULL * 1024) {
            return TfStringPrintf("%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ULL) {
            return TfStringPrintf("%.2f KB", static_cast<double>(bytes) / 1024.0);
        } else {
            return TfStringPrintf("%zu bytes", bytes);
        }
    };
    
    size_t usedScene = mUsedSceneMemory.load();
    size_t usedRenderer = mUsedRendererMemory.load();
    float scenePressure = GetSceneMemoryPressure();
    float hardwarePressure = GetRendererMemoryPressure();
    
    TF_STATUS("=== Memory Statistics ===\n"
             "Scene Memory:\n"
             "  Used: %s / %s (%.1f%%)\n"
             "  Pressure: %.1f%%\n"
             "Renderer Memory:\n"
             "  Used: %s / %s (%.1f%%)\n"
             "  Pressure: %.1f%%\n"
             "Thresholds:\n"
             "  Renderer Paging: %.1f%%\n"
             "  Scene Paging: %.1f%%\n"
             "  Low Memory: %.1f%%\n"
             "=========================\n",
             formatBytes(usedScene).c_str(), formatBytes(mSceneMemoryLimit).c_str(), scenePressure * 100,
             scenePressure * 100,
             formatBytes(usedRenderer).c_str(), formatBytes(mRendererMemoryLimit).c_str(), hardwarePressure * 100,
             hardwarePressure * 100,
             RENDERER_PAGING_THRESHOLD * 100,
             SCENE_PAGING_THRESHOLD * 100,
             LOW_MEMORY_THRESHOLD * 100);
}

} // namespace HVT_NS
