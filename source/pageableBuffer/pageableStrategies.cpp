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

#include <hvt/pageableBuffer/pageableStrategies.h>

namespace HVT_NS::HdPagingStrategies
{

// Paging Strategies //////////////////////////////////////////////////////////

HdPagingDecision AgeBasedStrategy::operator()(const HdPageableBufferBase& /*buffer*/, const HdPagingContext& context) const {
    HdPagingDecision decision;
    
    // Don't page dynamic buffers or mapped buffers
    if (context.bufferUsage == HdBufferUsage::Dynamic) {
        return decision;
    }
    
    if (context.isOverAge) {
        decision.shouldPage = true;
        decision.forceOperation = false;
        
        // Prioritize memory to disk for aged buffers
        auto state = static_cast<int>(context.bufferState);
        if (state & static_cast<int>(HdBufferState::SceneBuffer)) {
            decision.action = HdPagingDecision::Action::SwapSceneToDisk;
        } else if (state & static_cast<int>(HdBufferState::RendererBuffer)) {
            decision.action = HdPagingDecision::Action::SwapRendererToDisk;
        }
    }
    
    return decision;
}

HdPagingDecision PressureBasedStrategy::operator()(const HdPageableBufferBase& buffer, const HdPagingContext& context) const {
    HdPagingDecision decision;
    
    // Don't page dynamic buffers or mapped buffers
    if (context.bufferUsage == HdBufferUsage::Dynamic) {
        return decision;
    }
    const bool highScenePressure = context.scenePressure > HdMemoryMonitor::HIGH_SCENE_PRESSURE_THRESHOLD;
    const bool highDevicePressure = context.rendererPressure >= HdMemoryMonitor::HIGH_RENDERER_PRESSURE_THRESHOLD;
    
    // Scene memory pressure response
    if (context.scenePressure > HdMemoryMonitor::SCENE_PAGING_THRESHOLD) {
        decision.shouldPage = true;
        decision.forceOperation = highScenePressure;
        decision.action = HdPagingDecision::Action::SwapSceneToDisk;
    }
    // Renderer memory pressure response  
    else if (context.rendererPressure >= HdMemoryMonitor::RENDERER_PAGING_THRESHOLD) {
        decision.shouldPage = true;
        decision.forceOperation = highDevicePressure;
        
        bool hasValidDisk = buffer.HasValidDiskBuffer();
        
        if (hasValidDisk || highScenePressure) {
            decision.action = HdPagingDecision::Action::SwapRendererToDisk;
        } else {
            decision.action = HdPagingDecision::Action::SwapToSceneMemory;
        }
    }
    
    return decision;
}

HdPagingDecision ConservativeStrategy::operator()(const HdPageableBufferBase& /*buffer*/, const HdPagingContext& context) const {
    HdPagingDecision decision;
    
    // Don't page dynamic buffers or mapped buffers
    if (context.bufferUsage == HdBufferUsage::Dynamic) {
        return decision;
    }
    
    // Conservative strategy: page only at high thresholds with age consideration
    const bool veryHighScenePressure = context.scenePressure > HdMemoryMonitor::HIGH_SCENE_PRESSURE_THRESHOLD;
    const bool veryHighDevicePressure = context.rendererPressure > HdMemoryMonitor::HIGH_RENDERER_PRESSURE_THRESHOLD;
    const bool veryOld = context.bufferAge > context.ageLimit * 2; // Extra conservative age check
    
    if (veryHighScenePressure && veryOld) {
        decision.shouldPage = true;
        decision.forceOperation = false; // Never force in conservative mode
        decision.action = HdPagingDecision::Action::SwapSceneToDisk;
    }
    else if (veryHighDevicePressure && veryOld) {
        decision.shouldPage = true;
        decision.forceOperation = false;
        decision.action = HdPagingDecision::Action::SwapToSceneMemory; // Conservative: prefer memory over disk
    }
    
    return decision;
}

HdPagingDecision HybridStrategy::operator()(const HdPageableBufferBase& buffer, const HdPagingContext& context) const {
    HdPagingDecision decision;
    
    // Don't page dynamic buffers or mapped buffers
    if (context.bufferUsage == HdBufferUsage::Dynamic) {
        return decision;
    }
    
    const bool highScenePressure = context.scenePressure > HdMemoryMonitor::HIGH_SCENE_PRESSURE_THRESHOLD;
    const bool highDevicePressure = context.rendererPressure >= HdMemoryMonitor::HIGH_RENDERER_PRESSURE_THRESHOLD;
    
    // Use age-based strategy when pressure is moderate but buffer is old
    if (!highScenePressure && !highDevicePressure && context.isOverAge) {
        return AgeBasedStrategy{}(buffer, context);
    }
    
    // Use pressure-based strategy when under memory pressure
    if (context.scenePressure > HdMemoryMonitor::SCENE_PAGING_THRESHOLD || 
        context.rendererPressure >= HdMemoryMonitor::RENDERER_PAGING_THRESHOLD) {
        return PressureBasedStrategy{}(buffer, context);
    }
    
    return decision;
}

} // namespace HVT_NS::HdPagingStrategies