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

#include <hvt/engine/renderBufferSettingsProvider.h>

#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/usd/sdf/path.h>

namespace HVT_NS
{

/// Abstract interface for a render buffer management backend.
///
/// The public RenderBufferManager is a thin shell owning a
/// std::unique_ptr<RenderBufferManagerImpl>. The scene-index (SI) and scene-delegate (SD)
/// implementations both derive from this interface, so the backend can be selected at runtime
/// without changing the public API.
///
/// It extends RenderBufferSettingsProvider so that the settings consumed by tasks (AOV params,
/// presentation params, etc.) remain accessible through the same interface.
class RenderBufferManagerImpl : public RenderBufferSettingsProvider
{
public:
    ~RenderBufferManagerImpl() override = default;

    /// Sets the size of the render buffer and MSAA settings, update render buffer descriptors.
    virtual void SetBufferSizeAndMsaa(
        const PXR_NS::GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled) = 0;

    /// Get the render buffer by its name.
    virtual PXR_NS::HdRenderBuffer* GetRenderOutput(
        PXR_NS::TfToken const& name, PXR_NS::SdfPath const& controllerId) = 0;

    /// Updates render output parameters and creates new render buffers if needed.
    virtual bool SetRenderOutputs(PXR_NS::TfToken const& outputToVisualize,
        PXR_NS::TfTokenVector const& outputs, RenderBufferBindings const& inputs,
        PXR_NS::GfVec4d const& viewport, PXR_NS::SdfPath const& controllerId) = 0;

    /// Get the render outputs.
    virtual PXR_NS::TfTokenVector const& GetRenderOutputs() const = 0;

    /// Updates the render output clear color.
    virtual void SetRenderOutputClearColor(PXR_NS::TfToken const& name,
        PXR_NS::SdfPath const& controllerId, PXR_NS::VtValue const& clearValue) = 0;

    /// Set the framebuffer to present the render to.
    virtual void SetPresentationOutput(
        PXR_NS::TfToken const& api, PXR_NS::VtValue const& framebufferHandle) = 0;

    /// Set interop destination handle to present to and composition parameters.
    virtual void SetInteropPresentation(
        PXR_NS::VtValue const& destinationInteropHandle, PXR_NS::VtValue const& composition) = 0;

    /// Set vsync and window destination handle to present to.
    virtual void SetWindowPresentation(PXR_NS::VtValue const& windowHandle, bool vsync) = 0;
};

} // namespace HVT_NS
