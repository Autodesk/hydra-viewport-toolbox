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

#include <atomic>

namespace HVT_NS
{

class HdMemoryMonitor
{
public:
    ~HdMemoryMonitor() = default;

    // Memory tracking
    void AddSceneMemory(size_t size);
    void ReduceSceneMemory(size_t size);
    void AddRendererMemory(size_t size);
    void ReduceRendererMemory(size_t size);

    // Memory limits and usage
    size_t GetUsedSceneMemory() const { return mUsedSceneMemory; }
    size_t GetUsedRendererMemory() const { return mUsedRendererMemory; }
    size_t GetSceneMemoryLimit() const { return mSceneMemoryLimit; }
    size_t GetRendererMemoryLimit() const { return mRendererMemoryLimit; }

    // Memory pressure calculation
    float GetSceneMemoryPressure() const;
    float GetRendererMemoryPressure() const;

    // Thresholds (percentages to memory limits)
    static constexpr float LOW_MEMORY_THRESHOLD             = 0.9f;
    static constexpr float RENDERER_PAGING_THRESHOLD        = 0.5f;
    static constexpr float SCENE_PAGING_THRESHOLD           = 0.8f;
    static constexpr float HIGH_RENDERER_PRESSURE_THRESHOLD = 0.95f;
    static constexpr float HIGH_SCENE_PRESSURE_THRESHOLD    = 0.95f;

    // Statistics (development purpose only)
    void PrintMemoryStats() const;

private:
    // By design, only HdPageableBufferManager can create and hold it.
    HdMemoryMonitor(size_t sceneMemoryLimit, size_t rendererMemoryLimit);

    // Disable copy and move
    HdMemoryMonitor(const HdMemoryMonitor&) = delete;
    HdMemoryMonitor(HdMemoryMonitor&&)      = delete;

    std::atomic<size_t> mUsedSceneMemory { 0 };
    std::atomic<size_t> mUsedRendererMemory { 0 };

    const size_t mSceneMemoryLimit    = 2ULL * 1024 * 1024 * 1024 /* 2GB */;
    const size_t mRendererMemoryLimit = 1ULL * 1024 * 1024 * 1024 /* 1GB */;

    template <typename, typename>
    friend class HdPageableBufferManager;
};

} // namespace HVT_NS