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

#include <hvt/tasks/depthGuide.h>

#include <pxr/imaging/hgi/hgi.h>

#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

DepthGuide::DepthGuide(
    Hgi* hgi, HgiTextureHandle const& texture, GfVec2i const& imageSize, int scale) :
    _hgi(hgi), _texture(texture), _imageSize(imageSize), _scale(std::max(1, scale))
{
}

DepthGuide::~DepthGuide()
{
    if (_hgi && _texture)
    {
        _hgi->DestroyTexture(&_texture);
    }
}

} // namespace HVT_NS
