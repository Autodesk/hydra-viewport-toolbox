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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif _MSC_VER
#pragma warning(push)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hgi/texture.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <memory>
#include <utility>
#include <vector>

namespace HVT_NS
{

struct RenderBufferBinding
{
    PXR_NS::TfToken aovName;
    PXR_NS::HgiTextureHandle texture;
    PXR_NS::HdRenderBuffer* buffer;
    std::string rendererName;

    RenderBufferBinding() = default;

    /// Compares the property values.
    bool operator==(RenderBufferBinding const& other) const
    {
        if (aovName != other.aovName || texture != other.texture || buffer != other.buffer ||
            rendererName != other.rendererName)
            return false;

        return true;
    }

    /// Compares the property values, and negates.
    bool operator!=(RenderBufferBinding const& other) const { return !(*this == other); }
};
using RenderBufferBindings = std::vector<RenderBufferBinding>;

using RenderBufferSettingsProviderWeakPtr = std::weak_ptr<class RenderBufferSettingsProvider>;

struct HVT_API AovParams
{
    /// Buffer information used by AovInputTask
    /// @{
    PXR_NS::SdfPath aovBufferPath;
    PXR_NS::SdfPath depthBufferPath;
    PXR_NS::SdfPath neyeBufferPath;
    PXR_NS::HdRenderBuffer* aovBuffer   = nullptr;
    PXR_NS::HdRenderBuffer* depthBuffer = nullptr;
    PXR_NS::HdRenderBuffer* neyeBuffer  = nullptr;
    /// @}

    /// The framebuffer that the AOVs are presented into. This is a VtValue that encodes a
    /// framebuffer in a dstApi specific way.
    ///
    /// E.g., a uint32_t (aka GLuint) for framebuffer object for dstApi==OpenGL.
    /// For backwards compatibility, the currently bound framebuffer is used
    /// when the VtValue is empty.
    PXR_NS::VtValue presentFramebuffer;
    PXR_NS::TfToken presentApi;

    /// AOV Bindings for render tasks
    ///
    /// @{
    PXR_NS::HdRenderPassAovBindingVector aovBindingsClear;
    PXR_NS::HdRenderPassAovBindingVector aovBindingsNoClear;
    PXR_NS::HdRenderPassAovBindingVector aovInputBindings;
    /// \note This value is derived from RenderBufferManagerImpl::_aovInputs: is size() zero?
    /// \note This could be removed, _aovInputs could be consulted instead.
    bool hasNoAovInputs = false;
    /// @}

    /// The output clear values, per render buffer Id
    PXR_NS::TfHashMap<PXR_NS::SdfPath, PXR_NS::VtValue, PXR_NS::SdfPath::Hash> outputClearValues;
};

/// Interface for accessing render buffer settings.
/// \note This interface is intended to be used by task commit functions.
class HVT_API RenderBufferSettingsProvider
{
public:
    virtual ~RenderBufferSettingsProvider() = default;

    /// Returns true if AOVs (RenderBuffer Bprim type) are supported by the Render Index.
    virtual bool IsAovSupported() const = 0;

    /// Returns true if progressive rendering is enabled.
    virtual bool IsProgressiveRenderingEnabled() const = 0;

    /// Get the AOV token associated with the viewport.
    virtual PXR_NS::TfToken const& GetViewportAov() const = 0;

    /// Get the render buffer size.
    virtual PXR_NS::GfVec2i const& GetRenderBufferSize() const = 0;

    /// Get the AOV parameters cache, which contains data transferred to the TaskManager before
    /// executing tasks.
    virtual AovParams const& GetAovParamCache() const = 0;
};

} // namespace HVT_NS