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

#include <hvt/tasks/tileFramingUtils.h>

#include <pxr/imaging/hgi/tokens.h>

#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

void ComputeImagePixelOffset(CameraUtilFraming const& framing, GfVec2i const& bufferSize,
    bool isTopDown, GfVec2i* outOffset, GfVec2i* outImageSize)
{
    *outOffset    = GfVec2i(0, 0);
    *outImageSize = bufferSize;

    if (!framing.IsValid())
    {
        return;
    }

    const GfRange2f& displayWindow = framing.displayWindow;
    const GfVec2f& displayMin      = displayWindow.GetMin();
    const GfVec2f displaySize      = displayWindow.GetSize();

    const GfVec2i imageSize(static_cast<int>(std::lround(displaySize[0])),
        static_cast<int>(std::lround(displaySize[1])));

    const int originX = -static_cast<int>(std::lround(displayMin[0]));
    const int originY = -static_cast<int>(std::lround(displayMin[1]));

    *outImageSize = imageSize;
    *outOffset    = GfVec2i(originX, isTopDown ? originY : imageSize[1] - bufferSize[1] - originY);
}

bool IsTopDownTextureRowOrder(Hgi const* hgi)
{
    return hgi != nullptr && hgi->GetAPIName() == HgiTokens->Metal;
}

} // namespace HVT_NS
