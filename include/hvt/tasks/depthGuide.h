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
#pragma once

#include <hvt/api.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/gf/vec2i.h>
#include <pxr/imaging/hgi/texture.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
// clang-format on

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE
class Hgi;
PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS
{

/// A full-image depth guide for tiled renders: an R32F texture holding, per texel, the raw depth
/// of the closest surface of its scale x scale block of image pixels, in the row order of the
/// backend's textures (see DepthGuideBuilder and SSAOTaskParams::depthGuide).
///
/// The guide is shared through DepthGuidePtr: the texture lives as long as any holder, e.g. the
/// parameters of a task, so no holder can be left with a destroyed texture.
/// \note The Hgi instance must outlive every guide it created.
class HVT_API DepthGuide
{
public:
    /// Constructor.
    /// \param hgi The Hgi instance that destroys the texture with the guide, or null when the
    /// guide does not own the texture (the caller then keeps it alive).
    /// \param texture The guide texture.
    /// \param imageSize The full image size the guide covers, in pixels.
    /// \param scale The downsampling scale; the guide is ceil(imageSize / scale) texels.
    DepthGuide(PXR_NS::Hgi* hgi, PXR_NS::HgiTextureHandle const& texture,
        PXR_NS::GfVec2i const& imageSize, int scale);

    /// Destructor; destroys the texture when the guide owns it.
    ~DepthGuide();

    DepthGuide(DepthGuide const&)            = delete;
    DepthGuide& operator=(DepthGuide const&) = delete;

    /// Returns the guide texture.
    PXR_NS::HgiTextureHandle const& GetTexture() const { return _texture; }

    /// Returns the full image size the guide covers, in pixels.
    PXR_NS::GfVec2i const& GetImageSize() const { return _imageSize; }

    /// Returns the downsampling scale; 1 is the full resolution.
    int GetScale() const { return _scale; }

private:
    PXR_NS::Hgi* _hgi { nullptr };
    PXR_NS::HgiTextureHandle _texture;
    PXR_NS::GfVec2i _imageSize { 0, 0 };
    int _scale { 1 };
};

/// A shared, read-only depth guide.
using DepthGuidePtr = std::shared_ptr<DepthGuide const>;

} // namespace HVT_NS
