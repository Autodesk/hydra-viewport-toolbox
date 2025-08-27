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

#include <hvt/pageableBuffer/pageableBufferManager.h>

namespace HVT_NS
{

// Common strategy combinations
// This ensures the most common template combinations are compiled.
template class HdPageableBufferManager<HdPagingStrategies::HybridStrategy, HdPagingStrategies::LRUSelectionStrategy>;
template class HdPageableBufferManager<HdPagingStrategies::PressureBasedStrategy, HdPagingStrategies::LargestFirstSelectionStrategy>;
template class HdPageableBufferManager<HdPagingStrategies::ConservativeStrategy, HdPagingStrategies::OldestFirstSelectionStrategy>;
template class HdPageableBufferManager<HdPagingStrategies::AgeBasedStrategy, HdPagingStrategies::OldestFirstSelectionStrategy>;
template class HdPageableBufferManager<HdPagingStrategies::PressureBasedStrategy, HdPagingStrategies::LRUSelectionStrategy>;
template class HdPageableBufferManager<HdPagingStrategies::HybridStrategy, HdPagingStrategies::FIFOSelectionStrategy>;

} // namespace HVT_NS
