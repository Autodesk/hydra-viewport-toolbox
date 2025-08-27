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
#include <hvt/pageableBuffer/pageableMemoryMonitor.h>

#include <vector>
#include <memory>
#include <algorithm>
#include <iterator>

namespace HVT_NS
{

// TODO: clear up unused params
// Paging strategy context information
struct HVT_API HdPagingContext {
    int currentFrame = 0;
    int bufferAge = 0;
    int ageLimit = 20; ///< Frames before resource is considered old
    float scenePressure = 0.0f;
    float rendererPressure = 0.0f;
    bool isOverAge = false;
    HdBufferUsage bufferUsage = HdBufferUsage::Static;
    HdBufferState bufferState = HdBufferState::Unknown;
};

// Paging decision result
struct HVT_API HdPagingDecision {
    bool shouldPage = false;
    bool forceOperation = false;
    enum class Action {
        None,
        SwapSceneToDisk,
        SwapRendererToDisk, 
        SwapToSceneMemory,
        ReleaseRendererBuffer
    } action = Action::None;
};

// HdPageableBufferBase selection context information
struct HVT_API HdSelectionContext {
    int currentFrame = 0;
    float scenePressure = 0.0f;
    float rendererPressure = 0.0f;
    size_t requestedCount = 0;
    size_t totalBufferCount = 0;
};

namespace HdPagingStrategies {

// Paging Strategies //////////////////////////////////////////////////////////

struct HVT_API AgeBasedStrategy {
    HdPagingDecision operator()(const HdPageableBufferBase& /*buffer*/, const HdPagingContext& context) const;
};

struct HVT_API PressureBasedStrategy {
    HdPagingDecision operator()(const HdPageableBufferBase& buffer, const HdPagingContext& context) const;
};

struct HVT_API ConservativeStrategy {
    HdPagingDecision operator()(const HdPageableBufferBase& /*buffer*/, const HdPagingContext& context) const;
};

struct HVT_API HybridStrategy {
    HdPagingDecision operator()(const HdPageableBufferBase& buffer, const HdPagingContext& context) const;
};

// Buffer Selection Strategies ////////////////////////////////////////////////

struct HVT_API LRUSelectionStrategy {
    template<typename InputIterator>
    std::vector<std::shared_ptr<HdPageableBufferBase>> operator()(
        InputIterator first, InputIterator last, 
        const HdSelectionContext& context) const;
};

struct HVT_API FIFOSelectionStrategy {
    template<typename InputIterator>
    std::vector<std::shared_ptr<HdPageableBufferBase>> operator()(
        InputIterator first, InputIterator last, 
        const HdSelectionContext& context) const;
};

struct HVT_API OldestFirstSelectionStrategy {
    template<typename InputIterator>
    std::vector<std::shared_ptr<HdPageableBufferBase>> operator()(
        InputIterator first, InputIterator last, 
        const HdSelectionContext& context) const;
};

struct HVT_API LargestFirstSelectionStrategy {
    template<typename InputIterator>
    std::vector<std::shared_ptr<HdPageableBufferBase>> operator()(
        InputIterator first, InputIterator last, 
        const HdSelectionContext& context) const;
};

// Buffer Selection Strategies Implementation /////////////////////////////////
template<typename InputIterator>
std::vector<std::shared_ptr<HdPageableBufferBase>> LRUSelectionStrategy::operator()(
    InputIterator first, InputIterator last, 
    const HdSelectionContext& context) const {
    
    // Extract non-null values from iterator pairs
    std::vector<std::shared_ptr<HdPageableBufferBase>> validBuffers;
    for (auto it = first; it != last; ++it) {
        auto buffer = [&]() {
            // Handle both container iterators and map iterators
            if constexpr (std::is_same_v<typename std::iterator_traits<InputIterator>::value_type::second_type, std::shared_ptr<HdPageableBufferBase>>) {
                return it->second; // Map iterator
            } else {
                return *it; // Container iterator
            }
        }();
        
        if (buffer != nullptr) {
            validBuffers.push_back(buffer);
        }
    }
    
    // Sort by frame stamp
    std::sort(validBuffers.begin(), validBuffers.end(), 
        [](const auto& a, const auto& b) {
            return a->FrameStamp() < b->FrameStamp();
        });
    
    // Take the requested number of least recently used buffers
    size_t count = std::min(context.requestedCount, validBuffers.size());
    validBuffers.resize(count);
    
    return validBuffers;
}

template<typename InputIterator>
std::vector<std::shared_ptr<HdPageableBufferBase>> FIFOSelectionStrategy::operator()(
    InputIterator first, InputIterator last, 
    const HdSelectionContext& context) const {
    
    // Extract non-null values and take first N (insertion order)
    std::vector<std::shared_ptr<HdPageableBufferBase>> validBuffers;
    for (auto it = first; it != last; ++it) {
        auto buffer = [&]() {
            if constexpr (std::is_same_v<typename std::iterator_traits<InputIterator>::value_type::second_type, std::shared_ptr<HdPageableBufferBase>>) {
                return it->second;
            } else {
                return *it;
            }
        }();
        
        if (buffer != nullptr) {
            validBuffers.push_back(buffer);
            if (validBuffers.size() >= context.requestedCount) {
                break;
            }
        }
    }
    
    return validBuffers;
}

template<typename InputIterator>
std::vector<std::shared_ptr<HdPageableBufferBase>> OldestFirstSelectionStrategy::operator()(
    InputIterator first, InputIterator last, 
    const HdSelectionContext& context) const {
    
    // Extract non-null values from iterator pairs
    std::vector<std::shared_ptr<HdPageableBufferBase>> validBuffers;
    for (auto it = first; it != last; ++it) {
        auto buffer = [&]() {
            if constexpr (std::is_same_v<typename std::iterator_traits<InputIterator>::value_type::second_type, std::shared_ptr<HdPageableBufferBase>>) {
                return it->second;
            } else {
                return *it;
            }
        }();
        
        if (buffer != nullptr) {
            validBuffers.push_back(buffer);
        }
    }
    
    // Sort by age
    std::sort(validBuffers.begin(), validBuffers.end(),
        [currentFrame = context.currentFrame](const auto& a, const auto& b) {
            int ageA = currentFrame - a->FrameStamp();
            int ageB = currentFrame - b->FrameStamp();
            return ageA > ageB; // Oldest first
        });
    
    // Take the requested number of oldest buffers
    size_t count = std::min(context.requestedCount, validBuffers.size());
    validBuffers.resize(count);
    
    return validBuffers;
}

template<typename InputIterator>
std::vector<std::shared_ptr<HdPageableBufferBase>> LargestFirstSelectionStrategy::operator()(
    InputIterator first, InputIterator last, 
    const HdSelectionContext& context) const {
    
    // Extract non-null values from iterator pairs
    std::vector<std::shared_ptr<HdPageableBufferBase>> validBuffers;
    for (auto it = first; it != last; ++it) {
        auto buffer = [&]() {
            if constexpr (std::is_same_v<typename std::iterator_traits<InputIterator>::value_type::second_type, std::shared_ptr<HdPageableBufferBase>>) {
                return it->second;
            } else {
                return *it;
            }
        }();
        
        if (buffer != nullptr) {
            validBuffers.push_back(buffer);
        }
    }
    
    // Sort by size
    std::sort(validBuffers.begin(), validBuffers.end(),
        [](const auto& a, const auto& b) {
            return a->Size() > b->Size(); // Largest first
        });
    
    // Take the requested number of largest buffers
    size_t count = std::min(context.requestedCount, validBuffers.size());
    validBuffers.resize(count);
    
    return validBuffers;
}

} // namespace HdPagingStrategies

} // namespace HVT_NS