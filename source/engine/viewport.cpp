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

#include <hvt/engine/viewport.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

Viewport::Viewport(GfVec4i const& screenSize, GfVec2i const& renderBufferSize) :
    _screenSize(screenSize), _renderBufferSize(renderBufferSize)
{
}

bool Viewport::Resize(GfVec4i const& screenSize, GfVec2i const& renderBufferSize)
{
    _screenSize       = screenSize;
    _renderBufferSize = renderBufferSize;
    return true;
}

} // namespace HVT_NS
