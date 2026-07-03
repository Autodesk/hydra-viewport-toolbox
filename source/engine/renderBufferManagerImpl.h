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

#include "copyDepthShader.h"

#include <hvt/engine/renderBufferSettingsProvider.h>

#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

/// Abstract base class for a render buffer management backend.
///
/// The public RenderBufferManager is a thin shell owning a
/// std::unique_ptr<RenderBufferManagerImpl>. The scene-index (SI) and scene-delegate (SD)
/// implementations both derive from this class, so the backend can be selected at runtime
/// without changing the public API.
///
/// This class holds the member data and method implementations that are common to both backends.
/// Subclasses implement only the backend-specific operations: inserting, removing, and updating
/// render buffer Bprims.
class RenderBufferManagerImpl : public RenderBufferSettingsProvider
{
public:
    ~RenderBufferManagerImpl() override = default;

    void SetBufferSizeAndMsaa(
        PXR_NS::GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled);

    PXR_NS::HdRenderBuffer* GetRenderOutput(
        PXR_NS::TfToken const& name, PXR_NS::SdfPath const& controllerId);

    bool SetRenderOutputs(PXR_NS::TfToken const& outputToVisualize,
        PXR_NS::TfTokenVector const& outputs, RenderBufferBindings const& inputs,
        PXR_NS::GfVec4d const& viewport, PXR_NS::SdfPath const& controllerId);

    PXR_NS::TfTokenVector const& GetRenderOutputs() const { return _aovOutputs; }

    void SetRenderOutputClearColor(PXR_NS::TfToken const& name,
        PXR_NS::SdfPath const& controllerId, PXR_NS::VtValue const& clearValue);

    void SetPresentationOutput(
        PXR_NS::TfToken const& api, PXR_NS::VtValue const& framebufferHandle)
    {
        _presentParams.windowHandle      = PXR_NS::VtValue();
        _presentParams.api               = api;
        _presentParams.framebufferHandle = framebufferHandle;
    }

    void SetInteropPresentation(
        PXR_NS::VtValue const& destinationInteropHandle, PXR_NS::VtValue const& composition)
    {
        _presentParams.windowHandle      = PXR_NS::VtValue();
        _presentParams.framebufferHandle = destinationInteropHandle;
        _presentParams.compositionParams = composition;
    }

    void SetWindowPresentation(PXR_NS::VtValue const& windowHandle, bool vsync)
    {
        _presentParams.windowHandle      = windowHandle;
        _presentParams.windowVsync       = vsync;
        _presentParams.framebufferHandle = PXR_NS::VtValue();
    }

    bool IsAovSupported() const override;
    bool IsProgressiveRenderingEnabled() const override { return _isProgressiveRenderingEnabled; }
    PXR_NS::TfToken const& GetViewportAov() const override { return _viewportAov; }
    PXR_NS::GfVec2i const& GetRenderBufferSize() const override;
    AovParams const& GetAovParamCache() const override { return _aovTaskCache; }
    PresentationParams const& GetPresentationParams() const override { return _presentParams; }

protected:
    RenderBufferManagerImpl(PXR_NS::HdRenderIndex* pRenderIndex);

    /// Inserts a new render buffer Bprim into the backend.
    virtual void InsertRenderBuffer(PXR_NS::SdfPath const& id,
        PXR_NS::HdRenderBufferDescriptor const& desc, size_t msaaSampleCount) = 0;

    /// Removes render buffer Bprims from the backend.
    virtual void RemoveRenderBuffers(PXR_NS::SdfPathVector const& ids) = 0;

    /// Updates the dimensions and MSAA settings of all existing render buffer Bprims.
    virtual void UpdateRenderBufferDescriptors(PXR_NS::GfVec3i const& dimensions,
        bool multiSampled, size_t msaaSampleCount, bool descriptorSpecsChanged,
        bool msaaSampleCountChanged) = 0;

    PXR_NS::GfVec2i _renderBufferSize { 0, 0 };
    bool _enableMultisampling { true };
    size_t _msaaSampleCount { 4 };
    bool _isProgressiveRenderingEnabled { false };
    PXR_NS::SdfPathVector _aovBufferIds;
    PXR_NS::TfTokenVector _aovOutputs;
    RenderBufferBindings _aovInputs;
    PXR_NS::TfToken _viewportAov;
    AovParams _aovTaskCache;
    PresentationParams _presentParams;
    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyColorShader;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyColorShaderNoDepth;
    std::unique_ptr<CopyDepthShader> _copyDepthShader;

private:
    void SetViewportRenderOutput(
        PXR_NS::TfToken const& name, PXR_NS::SdfPath const& controllerId);

    void PrepareBuffersFromInputs(RenderBufferBinding const& colorInput,
        RenderBufferBinding const& depthInput, PXR_NS::HdRenderBufferDescriptor const& desc,
        PXR_NS::SdfPath const& controllerId);

    void PrepareDepthOnlyFromInput(RenderBufferBinding const& inputDepthAov,
        PXR_NS::HdRenderBufferDescriptor const& desc, PXR_NS::SdfPath const& controllerId);
};

} // namespace HVT_NS
